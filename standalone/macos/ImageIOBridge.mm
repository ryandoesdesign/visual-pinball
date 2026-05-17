// ImageIO-backed image decoding. Uses the harness-proven pattern of
// pulling raw decoded bytes off CGImage's data provider rather than
// rendering through a CGContext — the latter forces premultiplied
// alpha and was the source of the green/purple regression that
// rolled back the previous Phase 2 attempt. See
// `tools/imageio_harness/PHASE0_NOTES.md` for the evidence.

#import "ImageIOBridge.h"

#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <CoreGraphics/CoreGraphics.h>

#include <cstdlib>
#include <cstring>

extern "C" void vpx_imageio_free(vpx_imageio_image_t* img) {
   if (!img) return;
   if (img->pixels) std::free(img->pixels);
   img->pixels = nullptr;
   img->pixels_size = 0;
}

extern "C" void vpx_imageio_free_buffer(void* p) {
   if (p) std::free(p);
}

// Build a CGImage from caller-supplied bytes. channels = 1 / 3 / 4.
// Lifetime: returned CGImage owns a copy of the bytes via CFData; caller
// retains ownership of src_bytes and may free it after this returns.
static CGImageRef make_cgimage(uint32_t w, uint32_t h, uint32_t channels,
                               const uint8_t* src) {
   if (channels != 1 && channels != 3 && channels != 4) return nullptr;
   const size_t row = static_cast<size_t>(w) * channels;
   CFDataRef data = CFDataCreate(kCFAllocatorDefault, src,
      static_cast<CFIndex>(row * h));
   if (!data) return nullptr;
   CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
   CFRelease(data);
   if (!provider) return nullptr;

   CGColorSpaceRef cs = (channels == 1)
      ? CGColorSpaceCreateWithName(kCGColorSpaceGenericGray)
      : CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
   if (!cs) { CGDataProviderRelease(provider); return nullptr; }

   CGBitmapInfo info;
   if (channels == 1)      info = kCGImageAlphaNone;
   else if (channels == 3) info = kCGImageAlphaNone | kCGBitmapByteOrder32Big;
   else                    info = kCGImageAlphaLast | kCGBitmapByteOrder32Big;

   CGImageRef img = CGImageCreate(
      w, h, 8, channels * 8, row, cs, info,
      provider, nullptr, false, kCGRenderingIntentDefault);
   CGColorSpaceRelease(cs);
   CGDataProviderRelease(provider);
   return img;
}

extern "C" int vpx_imageio_encode_png_to_memory(uint32_t w, uint32_t h, uint32_t channels,
                                                const uint8_t* src,
                                                uint8_t** out_bytes, size_t* out_size) {
   if (out_bytes) *out_bytes = nullptr;
   if (out_size) *out_size = 0;
   if (!src || !out_bytes || !out_size) return 0;

   CGImageRef img = make_cgimage(w, h, channels, src);
   if (!img) return 0;

   CFMutableDataRef out = CFDataCreateMutable(kCFAllocatorDefault, 0);
   if (!out) { CGImageRelease(img); return 0; }
   CGImageDestinationRef dest = CGImageDestinationCreateWithData(
      out, (CFStringRef)@"public.png", 1, nullptr);
   if (!dest) { CFRelease(out); CGImageRelease(img); return 0; }
   CGImageDestinationAddImage(dest, img, nullptr);
   const bool ok = CGImageDestinationFinalize(dest);
   CFRelease(dest);
   CGImageRelease(img);

   if (!ok) { CFRelease(out); return 0; }
   const CFIndex n = CFDataGetLength(out);
   uint8_t* buf = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(n)));
   if (!buf) { CFRelease(out); return 0; }
   std::memcpy(buf, CFDataGetBytePtr(out), static_cast<size_t>(n));
   CFRelease(out);
   *out_bytes = buf;
   *out_size = static_cast<size_t>(n);
   return 1;
}

extern "C" int vpx_imageio_save_png_to_file(uint32_t w, uint32_t h, uint32_t channels,
                                            const uint8_t* src,
                                            const char* out_path) {
   if (!src || !out_path) return 0;
   CGImageRef img = make_cgimage(w, h, channels, src);
   if (!img) return 0;

   CFURLRef url = CFURLCreateFromFileSystemRepresentation(
      kCFAllocatorDefault, reinterpret_cast<const UInt8*>(out_path),
      static_cast<CFIndex>(std::strlen(out_path)), false);
   if (!url) { CGImageRelease(img); return 0; }
   CGImageDestinationRef dest = CGImageDestinationCreateWithURL(
      url, (CFStringRef)@"public.png", 1, nullptr);
   CFRelease(url);
   if (!dest) { CGImageRelease(img); return 0; }
   CGImageDestinationAddImage(dest, img, nullptr);
   const bool ok = CGImageDestinationFinalize(dest);
   CFRelease(dest);
   CGImageRelease(img);
   return ok ? 1 : 0;
}

static int decode_8bit(CGImageRef img, const uint8_t* base, size_t bytesPerRow,
                       CGImageAlphaInfo alpha, CGBitmapInfo bmi,
                       size_t bpp, vpx_imageio_image_t* out) {
   const size_t w = CGImageGetWidth(img);
   const size_t h = CGImageGetHeight(img);

   // Always emit RGBA8. Even when the source has no alpha channel we
   // produce a 4-byte buffer with alpha=0xFF — matches the practical
   // behaviour of the old FreeImage path for the WebP/PNG/JPEG bundled
   // assets (FreeImage's WebP decoder appears to return 32bpp DIBs
   // whose `IsTransparent` flag varies per file, and the renderer's
   // upload path is stable on 4-byte stride textures). Going wider
   // also means the bridge never has to worry about whether a "no
   // alpha" source loses some downstream feature (alpha-as-mask,
   // shader expecting RGBA sampler, etc.).
   const bool premul = (alpha == kCGImageAlphaPremultipliedLast)
                    || (alpha == kCGImageAlphaPremultipliedFirst);
   const bool alphaFirst = (alpha == kCGImageAlphaFirst)
                        || (alpha == kCGImageAlphaPremultipliedFirst)
                        || (alpha == kCGImageAlphaNoneSkipFirst);
   const bool littleEndian = (bmi & kCGBitmapByteOrderMask) == kCGBitmapByteOrder32Little;

   const size_t srcStride = (bpp == 24) ? 3 : 4;
   const size_t pxs = w * h * 4;
   uint8_t* dst = static_cast<uint8_t*>(std::malloc(pxs));
   if (!dst) return 0;

   for (size_t y = 0; y < h; ++y) {
      const uint8_t* srow = base + y * bytesPerRow;
      uint8_t* drow = dst + y * w * 4;
      for (size_t x = 0; x < w; ++x) {
         uint8_t r, g, b, a;
         const uint8_t* p = srow + x * srcStride;
         if (srcStride == 3) {
            r = p[0]; g = p[1]; b = p[2]; a = 0xFF;
         } else if (littleEndian) {
            if (alphaFirst) { a = p[3]; r = p[2]; g = p[1]; b = p[0]; }
            else            { r = p[3]; g = p[2]; b = p[1]; a = p[0]; }
         } else {
            if (alphaFirst) { a = p[0]; r = p[1]; g = p[2]; b = p[3]; }
            else            { r = p[0]; g = p[1]; b = p[2]; a = p[3]; }
         }
         if (premul && a != 0 && a != 255) {
            r = static_cast<uint8_t>((r * 255 + a/2) / a);
            g = static_cast<uint8_t>((g * 255 + a/2) / a);
            b = static_cast<uint8_t>((b * 255 + a/2) / a);
         }
         drow[x*4 + 0] = r;
         drow[x*4 + 1] = g;
         drow[x*4 + 2] = b;
         drow[x*4 + 3] = a;
      }
   }

   out->width = static_cast<uint32_t>(w);
   out->height = static_cast<uint32_t>(h);
   out->format = VPX_IMAGEIO_FMT_RGBA8;
   out->pixels = dst;
   out->pixels_size = pxs;
   return 1;
}

// Fallback for layouts the data-provider path doesn't cover: paletted
// PNG (colorType=3), grayscale, 16-bit-integer PNG, or anything else CG
// decodes to a bitmap shape that's not 8-bit RGB/RGBA. Re-renders via
// CGContext into 8-bit premultiplied RGBA, then un-premultiplies so the
// canonical contract (straight alpha) holds. Slower than the
// data-provider path but format-agnostic.
static int decode_via_cgcontext(CGImageRef img, vpx_imageio_image_t* out) {
   const size_t w = CGImageGetWidth(img);
   const size_t h = CGImageGetHeight(img);

   CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
   if (!cs) return 0;

   const size_t pxs = w * h * 4;
   uint8_t* dst = static_cast<uint8_t*>(std::malloc(pxs));
   if (!dst) { CGColorSpaceRelease(cs); return 0; }

   CGContextRef ctx = CGBitmapContextCreate(
      dst, w, h, 8, w * 4, cs,
      kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
   CGColorSpaceRelease(cs);
   if (!ctx) { std::free(dst); return 0; }

   // CG bitmap contexts use bottom-up y; flip so memory row 0 = top of image.
   CGContextTranslateCTM(ctx, 0, h);
   CGContextScaleCTM(ctx, 1, -1);
   CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), img);
   CGContextRelease(ctx);

   // Convert premultiplied → straight alpha. Lossy at low alpha, but our
   // bundled assets don't use partial transparency in this fallback path
   // (the affected images are typically paletted PNGs with no tRNS).
   for (size_t i = 0; i < pxs; i += 4) {
      const uint8_t a = dst[i + 3];
      if (a != 0 && a != 255) {
         dst[i + 0] = static_cast<uint8_t>((dst[i + 0] * 255 + a/2) / a);
         dst[i + 1] = static_cast<uint8_t>((dst[i + 1] * 255 + a/2) / a);
         dst[i + 2] = static_cast<uint8_t>((dst[i + 2] * 255 + a/2) / a);
      }
   }

   out->width = static_cast<uint32_t>(w);
   out->height = static_cast<uint32_t>(h);
   out->format = VPX_IMAGEIO_FMT_RGBA8;
   out->pixels = dst;
   out->pixels_size = pxs;
   return 1;
}

template <typename T, vpx_imageio_format_t Fmt>
static int decode_float(CGImageRef img, const uint8_t* base, size_t bytesPerRow,
                        size_t bpp, vpx_imageio_image_t* out) {
   const size_t w = CGImageGetWidth(img);
   const size_t h = CGImageGetHeight(img);
   const size_t componentsIn = bpp / (8 * sizeof(T));   // 3 (RGB) or 4 (RGBA)
   const size_t pxs = w * h * 3 * sizeof(T);
   T* dst = static_cast<T*>(std::malloc(pxs));
   if (!dst) return 0;

   for (size_t y = 0; y < h; ++y) {
      const T* srow = reinterpret_cast<const T*>(base + y * bytesPerRow);
      T* drow = dst + y * w * 3;
      for (size_t x = 0; x < w; ++x) {
         drow[x*3 + 0] = srow[x*componentsIn + 0];
         drow[x*3 + 1] = srow[x*componentsIn + 1];
         drow[x*3 + 2] = srow[x*componentsIn + 2];
      }
   }

   out->width = static_cast<uint32_t>(w);
   out->height = static_cast<uint32_t>(h);
   out->format = Fmt;
   out->pixels = reinterpret_cast<uint8_t*>(dst);
   out->pixels_size = pxs;
   return 1;
}

extern "C" int vpx_imageio_decode(const void* data, size_t size, vpx_imageio_image_t* out) {
   std::memset(out, 0, sizeof(*out));
   if (!data || size == 0) return 0;

   CFDataRef cfdata = CFDataCreateWithBytesNoCopy(
      kCFAllocatorDefault, static_cast<const UInt8*>(data),
      static_cast<CFIndex>(size), kCFAllocatorNull);
   if (!cfdata) return 0;

   CGImageSourceRef srcRef = CGImageSourceCreateWithData(cfdata, nullptr);
   CFRelease(cfdata);
   if (!srcRef) return 0;

   NSDictionary* opts = @{
      (NSString*)kCGImageSourceShouldAllowFloat: @YES,
      (NSString*)kCGImageSourceShouldCache: @YES,
   };
   CGImageRef img = CGImageSourceCreateImageAtIndex(srcRef, 0, (CFDictionaryRef)opts);
   CFRelease(srcRef);
   if (!img) return 0;

   const size_t bpc = CGImageGetBitsPerComponent(img);
   const size_t bpp = CGImageGetBitsPerPixel(img);
   const size_t bytesPerRow = CGImageGetBytesPerRow(img);
   const CGBitmapInfo bmi = CGImageGetBitmapInfo(img);
   const CGImageAlphaInfo alpha = static_cast<CGImageAlphaInfo>(
      bmi & kCGBitmapAlphaInfoMask);
   const bool isFloat = (bmi & kCGBitmapFloatComponents) != 0;

   CFDataRef pixels = CGDataProviderCopyData(CGImageGetDataProvider(img));
   if (!pixels) { CGImageRelease(img); return 0; }
   const uint8_t* base = CFDataGetBytePtr(pixels);

   int ok = 0;
   if (!isFloat && bpc == 8 && (bpp == 24 || bpp == 32))
      ok = decode_8bit(img, base, bytesPerRow, alpha, bmi, bpp, out);
   else if (isFloat && bpc == 16 && (bpp == 48 || bpp == 64))
      ok = decode_float<uint16_t, VPX_IMAGEIO_FMT_RGB_FP16>(img, base, bytesPerRow, bpp, out);
   else if (isFloat && bpc == 32 && (bpp == 96 || bpp == 128))
      ok = decode_float<float,    VPX_IMAGEIO_FMT_RGB_FP32>(img, base, bytesPerRow, bpp, out);
   else
      // Paletted PNG, grayscale, 16-bit integer, anything else CG decodes
      // to a bitmap shape we don't recognise directly.
      ok = decode_via_cgcontext(img, out);

   CFRelease(pixels);
   CGImageRelease(img);
   return ok;
}
