// ImageIO decode path. Obj-C++, NO FreeImage headers — pulling those in
// would collide with objc.h's BOOL=bool typedef.
//
// Strategy: pull the decoded pixels directly off the CGImage's data
// provider, then normalise whatever native layout we get into the
// canonical (top-down, straight-alpha, RGBA) buffer. The "note" field
// reports the native layout so we can see what ImageIO actually hands
// us for each format we test.

#include "bridge.h"

#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <CoreGraphics/CoreGraphics.h>

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <algorithm>

static void set_note(CanonicalRaw* c, const char* s) {
   std::snprintf(c->note, sizeof(c->note), "%s", s);
}

extern "C" CanonicalRaw decode_imageio(const uint8_t* data, size_t size) {
   CanonicalRaw out = {};

   CFDataRef cfdata = CFDataCreateWithBytesNoCopy(
      kCFAllocatorDefault, data,
      static_cast<CFIndex>(size), kCFAllocatorNull);
   if (!cfdata) { set_note(&out, "CFDataCreate failed"); return out; }

   CGImageSourceRef srcRef = CGImageSourceCreateWithData(cfdata, nullptr);
   CFRelease(cfdata);
   if (!srcRef) { set_note(&out, "CGImageSourceCreateWithData failed"); return out; }

   CGImageRef img = CGImageSourceCreateImageAtIndex(srcRef, 0, nullptr);
   CFRelease(srcRef);
   if (!img) { set_note(&out, "CGImageSourceCreateImageAtIndex failed"); return out; }

   const size_t w = CGImageGetWidth(img);
   const size_t h = CGImageGetHeight(img);
   const size_t bpc = CGImageGetBitsPerComponent(img);
   const size_t bpp = CGImageGetBitsPerPixel(img);
   const size_t bytesPerRow = CGImageGetBytesPerRow(img);
   const CGBitmapInfo bmi = CGImageGetBitmapInfo(img);
   const CGImageAlphaInfo alpha = static_cast<CGImageAlphaInfo>(
      bmi & kCGBitmapAlphaInfoMask);
   const CGBitmapInfo order = bmi & kCGBitmapByteOrderMask;

   char nbuf[256];
   std::snprintf(nbuf, sizeof(nbuf),
      "ImageIO native %zux%zu bpc=%zu bpp=%zu pitch=%zu alpha=%u order=0x%x",
      w, h, bpc, bpp, bytesPerRow,
      static_cast<unsigned>(alpha), static_cast<unsigned>(order));
   set_note(&out, nbuf);

   // Pixel layouts we accept this pass:
   //   8bpc/32bpp with alpha=Last|First|PremultipliedLast|PremultipliedFirst|
   //                       NoneSkipLast|NoneSkipFirst
   //   8bpc/24bpp with alpha=None (typical JPEG)
   const bool isAlpha32 = (bpc == 8) && (bpp == 32);
   const bool isNoAlpha24 = (bpc == 8) && (bpp == 24) && (alpha == kCGImageAlphaNone);
   if (!isAlpha32 && !isNoAlpha24) {
      CGImageRelease(img);
      std::snprintf(out.note + std::strlen(out.note),
         sizeof(out.note) - std::strlen(out.note),
         "; unsupported 8-bit layout for this pass");
      return out;
   }

   CFDataRef pixels = CGDataProviderCopyData(CGImageGetDataProvider(img));
   if (!pixels) {
      CGImageRelease(img);
      std::snprintf(out.note + std::strlen(out.note),
         sizeof(out.note) - std::strlen(out.note),
         "; CGDataProviderCopyData failed");
      return out;
   }

   const uint8_t* in = CFDataGetBytePtr(pixels);
   const size_t bytes = w * h * 4;
   uint8_t* dst = static_cast<uint8_t*>(std::malloc(bytes));
   if (!dst) {
      CFRelease(pixels);
      CGImageRelease(img);
      return out;
   }

   const bool premultiplied = (alpha == kCGImageAlphaPremultipliedLast)
                           || (alpha == kCGImageAlphaPremultipliedFirst);
   const bool alphaFirst = (alpha == kCGImageAlphaPremultipliedFirst)
                        || (alpha == kCGImageAlphaFirst)
                        || (alpha == kCGImageAlphaNoneSkipFirst);
   const bool alphaIsSkipped = (alpha == kCGImageAlphaNoneSkipLast)
                            || (alpha == kCGImageAlphaNoneSkipFirst);
   const bool littleEndian = (order == kCGBitmapByteOrder32Little);
   const size_t srcStride = isNoAlpha24 ? 3 : 4;

   for (size_t y = 0; y < h; ++y) {
      const uint8_t* srow = in + y * bytesPerRow;
      uint8_t* drow = dst + y * w * 4;
      for (size_t x = 0; x < w; ++x) {
         uint8_t r, g, b, a;
         const uint8_t* p = srow + x * srcStride;
         if (isNoAlpha24) {
            r = p[0]; g = p[1]; b = p[2]; a = 0xFF;
         } else if (littleEndian) {
            if (alphaFirst) { a = p[3]; r = p[2]; g = p[1]; b = p[0]; }
            else            { r = p[3]; g = p[2]; b = p[1]; a = p[0]; }
         } else {
            if (alphaFirst) { a = p[0]; r = p[1]; g = p[2]; b = p[3]; }
            else            { r = p[0]; g = p[1]; b = p[2]; a = p[3]; }
         }
         if (alphaIsSkipped) a = 0xFF;  // skipped byte is undefined; treat opaque
         if (premultiplied && a != 0 && a != 255) {
            r = static_cast<uint8_t>(std::min(255, (r * 255 + a/2) / a));
            g = static_cast<uint8_t>(std::min(255, (g * 255 + a/2) / a));
            b = static_cast<uint8_t>(std::min(255, (b * 255 + a/2) / a));
         }
         drow[x*4 + 0] = r;
         drow[x*4 + 1] = g;
         drow[x*4 + 2] = b;
         drow[x*4 + 3] = a;
      }
   }

   CFRelease(pixels);
   CGImageRelease(img);

   out.width = static_cast<uint32_t>(w);
   out.height = static_cast<uint32_t>(h);
   out.rgba = dst;
   out.ok = 1;
   return out;
}

static void set_note_f(CanonicalRGB32F* c, const char* s) {
   std::snprintf(c->note, sizeof(c->note), "%s", s);
}

static void append_note_f(CanonicalRGB32F* c, const char* s) {
   std::snprintf(c->note + std::strlen(c->note),
      sizeof(c->note) - std::strlen(c->note), "%s", s);
}

extern "C" CanonicalRGB32F decode_imageio_rgb32f(const uint8_t* data, size_t size) {
   CanonicalRGB32F out = {};

   CFDataRef cfdata = CFDataCreateWithBytesNoCopy(
      kCFAllocatorDefault, data,
      static_cast<CFIndex>(size), kCFAllocatorNull);
   if (!cfdata) { set_note_f(&out, "CFDataCreate failed"); return out; }

   CGImageSourceRef srcRef = CGImageSourceCreateWithData(cfdata, nullptr);
   CFRelease(cfdata);
   if (!srcRef) { set_note_f(&out, "CGImageSourceCreateWithData failed"); return out; }

   // Ask for a CGImage at full precision. For HDR sources (EXR, .hdr)
   // we want the decoder to keep the float values, not tonemap to 8-bit.
   NSDictionary* opts = @{
      (NSString*)kCGImageSourceShouldAllowFloat: @YES,
      (NSString*)kCGImageSourceShouldCache: @YES,
   };
   CGImageRef img = CGImageSourceCreateImageAtIndex(srcRef, 0, (CFDictionaryRef)opts);
   CFRelease(srcRef);
   if (!img) { set_note_f(&out, "CGImageSourceCreateImageAtIndex failed (HDR likely unsupported)"); return out; }

   const size_t w = CGImageGetWidth(img);
   const size_t h = CGImageGetHeight(img);
   const size_t bpc = CGImageGetBitsPerComponent(img);
   const size_t bpp = CGImageGetBitsPerPixel(img);
   const size_t bytesPerRow = CGImageGetBytesPerRow(img);
   const CGBitmapInfo bmi = CGImageGetBitmapInfo(img);
   const CGImageAlphaInfo alpha = static_cast<CGImageAlphaInfo>(
      bmi & kCGBitmapAlphaInfoMask);
   const bool isFloat = (bmi & kCGBitmapFloatComponents) != 0;

   char nbuf[256];
   std::snprintf(nbuf, sizeof(nbuf),
      "ImageIO native %zux%zu bpc=%zu bpp=%zu pitch=%zu alpha=%u float=%d",
      w, h, bpc, bpp, bytesPerRow, static_cast<unsigned>(alpha), isFloat ? 1 : 0);
   set_note_f(&out, nbuf);

   if (!isFloat) {
      CGImageRelease(img);
      append_note_f(&out, "; not a float image");
      return out;
   }
   if (bpc != 32) {
      // FP16 path could be added with half→float conversion if needed.
      CGImageRelease(img);
      append_note_f(&out, "; bpc!=32 (likely half-float) unsupported this pass");
      return out;
   }

   const size_t channels = bpp / 32;  // 3 (RGB) or 4 (RGBA)
   if (channels != 3 && channels != 4) {
      CGImageRelease(img);
      append_note_f(&out, "; unexpected channel count");
      return out;
   }

   CFDataRef pixels = CGDataProviderCopyData(CGImageGetDataProvider(img));
   if (!pixels) {
      CGImageRelease(img);
      append_note_f(&out, "; CGDataProviderCopyData failed");
      return out;
   }

   const uint8_t* base = CFDataGetBytePtr(pixels);
   float* dst = static_cast<float*>(std::malloc(w * h * 3 * sizeof(float)));
   if (!dst) {
      CFRelease(pixels);
      CGImageRelease(img);
      return out;
   }

   for (size_t y = 0; y < h; ++y) {
      const float* srow = reinterpret_cast<const float*>(base + y * bytesPerRow);
      float* drow = dst + y * w * 3;
      for (size_t x = 0; x < w; ++x) {
         drow[x*3 + 0] = srow[x*channels + 0];
         drow[x*3 + 1] = srow[x*channels + 1];
         drow[x*3 + 2] = srow[x*channels + 2];
      }
   }

   CFRelease(pixels);
   CGImageRelease(img);

   out.width = static_cast<uint32_t>(w);
   out.height = static_cast<uint32_t>(h);
   out.rgb = dst;
   out.ok = 1;
   return out;
}
