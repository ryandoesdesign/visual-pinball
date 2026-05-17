// license:GPLv3+

#include "core/stdafx.h"
#include "Texture.h"

#include "math/math.h"
#include "renderer/Renderer.h"
#include "ui/win/WinEditor.h"
#include "utils/BiffReader.h"
#include "utils/lzwreader.h"

#include <SDL3_image/SDL_image.h>
#include <SDL3/SDL_surface.h>
#include "standalone/FreeImage.h"
#include "standalone/macos/ImageIOBridge.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG // only use the SSE2-JPG path from stbi, as all others are not faster than FreeImage //!! can remove stbi again if at some point FreeImage incorporates libjpeg-turbo or something similar
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#include "stb_image.h"

#include <fstream>
#include <iostream>

#define QOI_API static
#define QOI_IMPLEMENTATION
#define QOI_NO_STDIO
#include "qoi/qoi.h"
static inline int GetPixelSize(const BaseTexture::Format format)
{
   switch (format)
   {
   case BaseTexture::BW: return 1;
   case BaseTexture::BW_FP32: return 4;
   case BaseTexture::RGB: return 3;
   case BaseTexture::RGBA: return 4;
   case BaseTexture::SRGB: return 3;
   case BaseTexture::SRGBA: return 4;
   case BaseTexture::SRGB565: return 2;
   case BaseTexture::RGB_FP16: return 2 * 3;
   case BaseTexture::RGBA_FP16: return 2 * 4;
   case BaseTexture::RGB_FP32: return 4 * 3;
   case BaseTexture::RGBA_FP32: return 4 * 4;
   default: assert(false); return 0;
   }
}

BaseTexture::BaseTexture(const unsigned int w, const unsigned int h, const Format format)
   : m_realWidth(w)
   , m_realHeight(h)
   , m_format(format)
   , m_width(w)
   , m_height(h)
   , m_liveHash(((size_t)this) ^ usec() ^ ((uint64_t)w << 16) ^ ((uint64_t)h << 32) ^ format)
   , m_data(reinterpret_cast<uint8_t*>(SDL_aligned_alloc(16, w * h * GetPixelSize(format))))
{
}

BaseTexture::~BaseTexture()
{
   SDL_aligned_free(m_data);
}

unsigned int BaseTexture::pitch() const
{
   return m_width * GetPixelSize(m_format);
}

std::shared_ptr<BaseTexture> BaseTexture::Create(const unsigned int w, const unsigned int h, const Format format) noexcept
{
   BaseTexture* tex = nullptr;
   try
   {
      tex = new BaseTexture(w, h, format);
   }
   // failed to get mem?
   catch (...)
   {
      delete tex;
      return nullptr;
   }
   auto result = std::shared_ptr<BaseTexture>(tex);
   tex->m_selfPointer = result;
   return result;
}

std::shared_ptr<BaseTexture> BaseTexture::CreateFromFile(const std::filesystem::path& filename, unsigned int maxTexDimension, bool resizeOnLowMem) noexcept
{
   if (filename.empty())
      return nullptr;
   PinBinary ppb;
   ppb.ReadFromFile(filename);
   return CreateFromData(ppb.m_buffer.data(), ppb.m_buffer.size(), true, maxTexDimension, resizeOnLowMem);
}

// Apple's ImageIO doesn't support OpenEXR DWA-A or DWA-B compression. Tables
// in the wild (Medieval Madness, others) embed multi-megabyte DWA-compressed
// EXR normal/env maps that ImageIO refuses to decode. Detect the EXR magic
// and route the decode through FreeImage instead — FreeImage handles every
// EXR compression variant. Phase 5 (drop FreeImage entirely) will swap this
// for tinyexr or similar.
static bool isExrHeader(const void* data, size_t size)
{
   if (size < 4) return false;
   const uint8_t* b = static_cast<const uint8_t*>(data);
   return b[0] == 0x76 && b[1] == 0x2f && b[2] == 0x31 && b[3] == 0x01;
}

static std::shared_ptr<BaseTexture> CreateFromExrViaFreeImage(const void* data, size_t size) noexcept
{
   FIMEMORY* mem = FreeImage_OpenMemory(reinterpret_cast<BYTE*>(const_cast<void*>(data)),
                                        static_cast<DWORD>(size));
   if (!mem) return nullptr;
   FIBITMAP* dib = FreeImage_LoadFromMemory(FIF_EXR, mem, 0);
   FreeImage_CloseMemory(mem);
   if (!dib) return nullptr;

   const FREE_IMAGE_TYPE t = FreeImage_GetImageType(dib);
   FIBITMAP* rgbf = (t == FIT_RGBF) ? dib : FreeImage_ConvertToRGBF(dib);
   if (!rgbf) { FreeImage_Unload(dib); return nullptr; }
   if (rgbf != dib) FreeImage_Unload(dib);

   const unsigned int w = FreeImage_GetWidth(rgbf);
   const unsigned int h = FreeImage_GetHeight(rgbf);
   const unsigned int pitch = FreeImage_GetPitch(rgbf);
   const uint8_t* const bits = static_cast<const uint8_t*>(FreeImage_GetBits(rgbf));

   // Scan range so HDR values that fit FP16 land in the cheaper format.
   // Mirrors the policy from the old CreateFromFreeImage path.
   float minval = FLT_MAX, maxval = -FLT_MAX;
   for (unsigned int y = 0; y < h; ++y)
   {
      const float* const row = reinterpret_cast<const float*>(bits + static_cast<size_t>(y) * pitch);
      for (size_t i = 0; i < static_cast<size_t>(w) * 3; ++i)
      {
         if (row[i] < minval) minval = row[i];
         if (row[i] > maxval) maxval = row[i];
      }
   }
   const bool fitFp16 = (maxval <= 65504.f && minval >= -65504.f);
   const bool signedHalf = minval < 0.f;
   const BaseTexture::Format fmt = fitFp16 ? BaseTexture::RGB_FP16 : BaseTexture::RGB_FP32;

   auto tex = BaseTexture::Create(w, h, fmt);
   if (!tex) { FreeImage_Unload(rgbf); return nullptr; }

   if (fmt == BaseTexture::RGB_FP16)
   {
      uint16_t* const dst = static_cast<uint16_t*>(tex->data());
      for (unsigned int y = 0; y < h; ++y)
      {
         const float* const srow = reinterpret_cast<const float*>(bits + static_cast<size_t>(y) * pitch);
         uint16_t* const drow = dst + static_cast<size_t>(h - 1 - y) * w * 3;
         if (signedHalf)
            float2half_noF16MaxInfNaN(drow, srow, static_cast<size_t>(w) * 3);
         else
            float2half_pos_noF16MaxInfNaN(drow, srow, static_cast<size_t>(w) * 3);
      }
   }
   else
   {
      float* const dst = static_cast<float*>(tex->data());
      for (unsigned int y = 0; y < h; ++y)
      {
         const float* const srow = reinterpret_cast<const float*>(bits + static_cast<size_t>(y) * pitch);
         float* const drow = dst + static_cast<size_t>(h - 1 - y) * w * 3;
         memcpy(drow, srow, static_cast<size_t>(w) * 3 * sizeof(float));
      }
   }
   tex->SetIsOpaque(true);
   tex->m_realWidth = w;
   tex->m_realHeight = h;

   FreeImage_Unload(rgbf);
   return tex;
}

std::shared_ptr<BaseTexture> BaseTexture::CreateFromData(const void* data, const size_t size, const bool isImageData, unsigned int maxTexDimension, bool resizeOnLowMem) noexcept
{
   std::shared_ptr<BaseTexture> tex;

   if (data == nullptr || size == 0)
      return nullptr;
   
   // Try to load using fast JPG path via stbi if no texture resize must be triggered
   if (maxTexDimension == 0 && !resizeOnLowMem)
   {
      int x, y, channels_in_file = 0;
      const int ok = stbi_info_from_memory(static_cast<stbi_uc const *>(data), static_cast<int>(size), &x, &y, &channels_in_file); // Request stbi to convert image to BW, SRGB or SRGBA
      assert(channels_in_file != 2);
      assert(channels_in_file <= 4); // 2 or >4 should never happen for JPEGs (4 also not, but we handle it anyway)
      unsigned char * const __restrict stbi_data = (ok && channels_in_file != 2 && channels_in_file <= 4) ?
          stbi_load_from_memory(static_cast<stbi_uc const *>(data), static_cast<int>(size), &x, &y, &channels_in_file, channels_in_file) :
          nullptr;
      if (stbi_data) // will only enter this path for JPG files
      {
         Format format = channels_in_file == 4 ? BaseTexture::SRGBA : ((channels_in_file == 1) ? BaseTexture::BW : BaseTexture::SRGB);
         if (!isImageData)
         {
            switch (format)
            {
            case BaseTexture::SRGB: format = BaseTexture::RGB; break;
            case BaseTexture::SRGBA: format = BaseTexture::RGBA; break;
            default: break;
            }
         }
         tex = BaseTexture::Create(x, y, format);
         if (tex)
         {
            uint8_t* const __restrict pdst = static_cast<uint8_t*>(tex->data());
            const uint8_t* const __restrict psrc = (uint8_t*)stbi_data;
            memcpy(pdst, psrc, x * y * channels_in_file);
            stbi_image_free(stbi_data);
            return tex;
         }
         stbi_image_free(stbi_data);
      }
   }

   if (tex == nullptr)
   {
      // ImageIO handles every format the previous FreeImage path did
      // (PNG, JPEG, WebP, BMP, TIFF, GIF, EXR, Radiance .hdr, ...) plus
      // HEIC/AVIF/JPEG-XL. Pixel-layout equivalence with the old
      // FreeImage output is verified by tools/imageio_harness/.
      // TODO(phase2-followup): maxTexDimension / resizeOnLowMem are
      // currently ignored — the resize-on-low-mem retry loop and the
      // user-settable max-texture cap from the FreeImage path have
      // not been ported. Decoded resolution is whatever ImageIO
      // returns. None of our bundled assets need a cap; if a user
      // ball/decal image trips this, we re-introduce CGContext-based
      // downscale here.
      vpx_imageio_image_t img;
      if (!vpx_imageio_decode(data, size, &img))
         return isExrHeader(data, size) ? CreateFromExrViaFreeImage(data, size) : nullptr;

      Format format;
      size_t bytesPerPixel = 0;
      switch (img.format) {
      case VPX_IMAGEIO_FMT_RGB8:
         format = isImageData ? SRGB : RGB;
         bytesPerPixel = 3;
         break;
      case VPX_IMAGEIO_FMT_RGBA8:
         format = isImageData ? SRGBA : RGBA;
         bytesPerPixel = 4;
         break;
      case VPX_IMAGEIO_FMT_RGB_FP16:
         format = RGB_FP16;
         bytesPerPixel = 2 * 3;
         break;
      case VPX_IMAGEIO_FMT_RGB_FP32:
         format = RGB_FP32;
         bytesPerPixel = 4 * 3;
         break;
      default:
         vpx_imageio_free(&img);
         return nullptr;
      }

      tex = BaseTexture::Create(img.width, img.height, format);
      if (tex)
      {
         memcpy(tex->data(), img.pixels,
                static_cast<size_t>(img.width) * img.height * bytesPerPixel);
         tex->m_realWidth = img.width;
         tex->m_realHeight = img.height;
      }
      vpx_imageio_free(&img);
   }

   return tex;
}

std::shared_ptr<BaseTexture> BaseTexture::CreateFromHBitmap(const HBITMAP hbmp, unsigned int maxTexDim, bool with_alpha) noexcept
{
      return nullptr;
}

void BaseTexture::Update(std::shared_ptr<BaseTexture>& tex, const unsigned int width, const unsigned int height, const Format texFormat, const void* image)
{
   const int pixelSize = GetPixelSize(texFormat);
   string name;
   if (tex != nullptr)
   {
      name = tex->GetName();
      if ((tex->m_width == width) && (tex->m_height == height) && (tex->m_format == texFormat))
      {
         assert(tex->pitch() * tex->height() == width * height * pixelSize);
         if (tex->data() != image && image)
            memcpy(tex->data(), image, width * height * pixelSize);
         tex->m_aliases.clear();
         if (g_pplayer)
            g_pplayer->m_renderer->m_renderDevice->m_texMan.SetDirty(tex.get());
         return;
      }
      if (g_pplayer)
         g_pplayer->m_renderer->m_renderDevice->m_texMan.UnloadTexture(tex.get());
   }
   tex = BaseTexture::Create(width, height, texFormat);
   if (tex)
   {
      tex->SetName(name);
      if (image)
         memcpy(tex->data(), image, (size_t)width * height * pixelSize);
   }
}

void BaseTexture::FlipY()
{
   const int pitch = this->pitch();
   vector<uint8_t> buf(pitch);
   uint8_t* __restrict bits = buf.data();
   for (unsigned int i = 0; i < m_height / 2; i++)
   {
      memcpy(bits, m_data + i * pitch, pitch);
      memcpy(m_data + i * pitch, m_data + (m_height - 1 - i) * pitch, pitch);
      memcpy(m_data + (m_height - 1 - i) * pitch, bits, pitch);
   }
   m_aliases.clear();
}

bool BaseTexture::Save(const std::filesystem::path& filepath) const
{
   if ((m_format != SRGBA) && (m_format != SRGB))
      return false;

   const string ext = lowerCase(filepath.extension().string());
   bool success = false;

   // Create parent directory if needed
   std::filesystem::create_directories(filepath.parent_path());

   if (ext == ".bmp")
   {
      if (SDL_Surface* pSurface = ToSDLSurface(); pSurface)
      {
         success = SDL_SaveBMP(pSurface, filepath.string().c_str());
         SDL_DestroySurface(pSurface);
      }
   }
   else if (ext == ".qoi")
   {
      qoi_desc desc { .width = m_width, .height = m_height, .channels = static_cast<unsigned char>(m_format == SRGB ? 3 :4), .colorspace = QOI_SRGB };
      int size;
      void* encoded = qoi_encode(m_data, &desc, &size);
      if (encoded)
      {
         try
         {
            if (std::ofstream file(filepath, std::ios::binary | std::ios::trunc); file)
            {
                  file.write(reinterpret_cast<const char*>(encoded), size);
                  file.close();
                  success = true;
            }
         }
         catch (const std::filesystem::filesystem_error& e)
         {
            PLOGE << "Failed to save file " << filepath.string().c_str() << ": " << e.what();
         }
         QOI_FREE(encoded);
      }
   }
   else
   {
      if (SDL_Surface* pSurface = ToSDLSurface(); pSurface)
      {
         if (ext == ".png")
            success = IMG_SavePNG(pSurface, filepath.string().c_str());
         else if (ext == ".jpg" || ext == ".jpeg")
            success = IMG_SaveJPG(pSurface, filepath.string().c_str(), 75);
         // Needs latest SDL3_image for WEBP support
         //else if (ext == ".webp")
         //   success = IMG_SaveWEBP(pSurface, filepath.string().c_str(), 75);
         SDL_DestroySurface(pSurface);
      }

   }

   return success;
}

std::shared_ptr<BaseTexture> BaseTexture::GetAlias(Format format) const
{
   auto it = m_aliases.find(format);
   if (it == m_aliases.end())
   {
      std::shared_ptr<BaseTexture> alias = Convert(format);
      if (!m_isOpaqueDirty)
         alias->SetIsOpaque(IsOpaque());
      if (!m_isMD5Dirty)
         alias->SetMD5Hash(GetMD5Hash());
      m_aliases[format] = alias;
      return alias;
   }
   else
   {
      return it->second;
   }
}

std::shared_ptr<BaseTexture> BaseTexture::Convert(Format format) const
{
   std::shared_ptr<BaseTexture> tex = nullptr;

   switch (m_format)
   {
   case RGB:
      switch (format)
      {
      case RGBA:
         tex = BaseTexture::Create(m_width, m_height, RGBA);
         if (tex == nullptr)
            return nullptr;
         copy_rgb_rgba<false>((unsigned int*)tex->data(), static_cast<const uint8_t*>(datac()), (size_t)width() * height());
         break;
      default: break;
      }
      break;

   case SRGB:
      switch (format)
      {
      case SRGBA:
         tex = BaseTexture::Create(m_width, m_height, SRGBA);
         if (tex == nullptr)
            return nullptr;
         copy_rgb_rgba<false>((unsigned int*)tex->data(), static_cast<const uint8_t*>(datac()), (size_t)width() * height());
         break;
      default: break;
      }
      break;

   case SRGBA:
      switch (format)
      {
      case SRGB:
         tex = BaseTexture::Create(m_width, m_height, SRGB);
         if (tex == nullptr)
            return nullptr;
         {
            const uint32_t* const __restrict src_data = reinterpret_cast<const uint32_t*>(datac());
            uint8_t* const __restrict dest_data = static_cast<uint8_t*>(tex->data());
            copy_rgba_rgb<false>(dest_data, src_data, (size_t)width() * height());
         }
         break;
      default: break;
      }
      break;

   case SRGB565:
      switch (format)
      {
      case SRGBA:
         {
            tex = BaseTexture::Create(m_width, m_height, SRGBA);
            if (tex == nullptr)
               return nullptr;
            static constexpr uint8_t lum32[] = { 0, 8, 16, 25, 33, 41, 49, 58, 66, 74, 82, 90, 99, 107, 115, 123, 132, 140, 148, 156, 165, 173, 181, 189, 197, 206, 214, 222, 230, 239, 247, 255 };
            static constexpr uint8_t lum64[] = { 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 45, 49, 53, 57, 61, 65, 69, 73, 77, 81, 85, 89, 93, 97, 101, 105, 109, 113, 117, 121, 125, 130, 134, 138,
               142, 146, 150, 154, 158, 162, 166, 170, 174, 178, 182, 186, 190, 194, 198, 202, 206, 210, 215, 219, 223, 227, 231, 235, 239, 243, 247, 251, 255 };
            uint32_t* const __restrict data = reinterpret_cast<uint32_t*>(tex->data());
            const uint16_t* const __restrict frame = reinterpret_cast<const uint16_t*>(datac());
            const size_t size = (size_t)width() * height();
            for (size_t ofs = 0; ofs < size; ++ofs)
            {
               const uint16_t rgb565 = frame[ofs];
               data[ofs] = 0xFF000000 | (lum32[rgb565 & 0x1F] << 16) | (lum64[(rgb565 >> 5) & 0x3F] << 8) | lum32[(rgb565 >> 11) & 0x1F];
            }
            tex->SetIsOpaque(true);
         }
         break;
      default: break;
      }
      break;

   case RGB_FP16:
      switch (format)
      {
         case RGBA_FP16:
         {
            tex = BaseTexture::Create(m_width, m_height, RGBA_FP16);
            if (tex == nullptr)
               return nullptr;
            copy_rgb_rgba(reinterpret_cast<uint16_t*>(tex->data()), reinterpret_cast<const uint16_t*>(datac()), (size_t)width() * height());
         }
         break;
         default: break;
      }
      break;
   
   case RGB_FP32:
      switch (format)
      {
         case RGBA_FP32:
            {
            tex = BaseTexture::Create(m_width, m_height, RGBA_FP32);
            if (tex == nullptr)
               return nullptr;
            copy_rgb_rgba(reinterpret_cast<float*>(tex->data()), reinterpret_cast<const float*>(datac()), (size_t)width() * height());
         }
         break;
         default: break;
      }
      break;

   default: break;

   }

   // Copy without conversion
   if (m_format == format)
   {
      assert(tex == nullptr);
      tex = BaseTexture::Create(m_width, m_height, m_format);
      memcpy(tex->data(), datac(), (size_t)m_width * m_height * GetPixelSize(m_format));
   }

   if (tex)
   {
      if (!m_isOpaqueDirty)
         tex->SetIsOpaque(IsOpaque());
      if (!m_isMD5Dirty)
         tex->SetMD5Hash(GetMD5Hash());
   }

   return tex;
}

std::shared_ptr<BaseTexture> BaseTexture::ToBGRA() const
{
   std::shared_ptr<BaseTexture> tex = BaseTexture::Create(m_width, m_height, RGBA);
   if (tex == nullptr)
      return nullptr;

   tex->m_realWidth = m_realWidth;
   tex->m_realHeight = m_realHeight;
   if (IsOpaqueComputed())
      tex->SetIsOpaque(IsOpaque());
   uint8_t* const __restrict tmp = static_cast<uint8_t*>(tex->data());

   if (m_format == BaseTexture::RGB_FP32) // Tonemap for 8bpc-Display
   {
      const float* const __restrict src = (const float*)datac();
      const size_t e = (size_t)width() * height();
      for (size_t o = 0; o < e; ++o)
      {
         const float r = src[o * 3 + 0];
         const float g = src[o * 3 + 1];
         const float b = src[o * 3 + 2];
         const float l = r * 0.176204f + g * 0.812985f + b * 0.0108109f;
         const float n = (l * (float)(255. * 0.25) + 255.0f) / (l + 1.0f); // simple tonemap and scale by 255, overflow is handled by clamp below
         tmp[o * 4 + 0] = (uint8_t)clamp(b * n, 0.f, 255.f);
         tmp[o * 4 + 1] = (uint8_t)clamp(g * n, 0.f, 255.f);
         tmp[o * 4 + 2] = (uint8_t)clamp(r * n, 0.f, 255.f);
         tmp[o * 4 + 3] = 255;
      }
   }
   else if (m_format == BaseTexture::RGB_FP16) // Tonemap for 8bpc-Display
   {
      const uint16_t* const __restrict src = (const uint16_t*)datac();
      const size_t e = (size_t)width() * height();
      for (size_t o = 0; o < e; ++o)
      {
         const float r = half2float(src[o * 3 + 0]);
         const float g = half2float(src[o * 3 + 1]);
         const float b = half2float(src[o * 3 + 2]);
         const float l = r * 0.176204f + g * 0.812985f + b * 0.0108109f;
         const float n = (l * (float)(255. * 0.25) + 255.0f) / (l + 1.0f); // simple tonemap and scale by 255, overflow is handled by clamp below
         tmp[o * 4 + 0] = (uint8_t)clamp(b * n, 0.f, 255.f);
         tmp[o * 4 + 1] = (uint8_t)clamp(g * n, 0.f, 255.f);
         tmp[o * 4 + 2] = (uint8_t)clamp(r * n, 0.f, 255.f);
         tmp[o * 4 + 3] = 255;
      }
   }
   else if (m_format == BaseTexture::RGBA_FP16) // Tonemap for 8bpc-Display
   {
      const uint16_t* const __restrict src = (const uint16_t*)datac();
      size_t o = 0;
      for (unsigned int j = 0; j < height(); ++j)
         for (unsigned int i = 0; i < width(); ++i, ++o)
         {
            const float rf = half2float(src[o * 4 + 0]);
            const float gf = half2float(src[o * 4 + 1]);
            const float bf = half2float(src[o * 4 + 2]);
            const int alpha = (int)clamp(half2float(src[o * 4 + 3]) * 255.f, 0.f, 255.f);
            const float l = rf * 0.176204f + gf * 0.812985f + bf * 0.0108109f;
            const float n = (l * (float)(255. * 0.25) + 255.0f) / (l + 1.0f); // simple tonemap and scale by 255, overflow is handled by clamp below
            int r = (int)clamp(bf * n, 0.f, 255.f);
            int g = (int)clamp(gf * n, 0.f, 255.f);
            int b = (int)clamp(rf * n, 0.f, 255.f);
            // use the alpha in the bitmap, thus RGB needs to be premultiplied with alpha, due to how AlphaBlend() works
            if (alpha == 0) // adds a checkerboard where completely transparent (for the image manager display)
            {
               r = g = b = ((((i >> 4) ^ (j >> 4)) & 1) << 7) + 127;
            }
            else if (alpha != 255) // premultiply alpha for win32 AlphaBlend()
            {
               r = r * alpha >> 8;
               g = g * alpha >> 8;
               b = b * alpha >> 8;
            }
            tmp[o * 4 + 0] = r;
            tmp[o * 4 + 1] = g;
            tmp[o * 4 + 2] = b;
            tmp[o * 4 + 3] = alpha;
         }
   }
   else if (m_format == BaseTexture::BW)
   {
      const uint8_t* const __restrict src = static_cast<const uint8_t*>(datac());
      const size_t e = (size_t)width() * height();
      for (size_t o = 0; o < e; ++o)
      {
         tmp[o * 4 + 0] =
         tmp[o * 4 + 1] =
         tmp[o * 4 + 2] = src[o];
         tmp[o * 4 + 3] = 255; // A
      }
   }
   else if (m_format == BaseTexture::RGB || m_format == BaseTexture::SRGB)
   {
      copy_rgb_rgba<true>((unsigned int*)tmp, static_cast<const uint8_t*>(datac()), (size_t)width() * height());
   }
   else if (m_format == BaseTexture::RGBA || m_format == BaseTexture::SRGBA)
   {
      const uint8_t* const __restrict psrc = static_cast<const uint8_t*>(datac());
      size_t o = 0;
      for (unsigned int j = 0; j < height(); ++j)
      {
         for (unsigned int i = 0; i < width(); ++i, ++o)
         {
            int r = psrc[o * 4 + 0];
            int g = psrc[o * 4 + 1];
            int b = psrc[o * 4 + 2];
            const int alpha = psrc[o * 4 + 3];
            // use the alpha in the bitmap, thus RGB needs to be premultiplied with alpha, due to how AlphaBlend() works
            if (alpha == 0) // adds a checkerboard where completely transparent (for the image manager display)
            {
               r = g = b = ((((i >> 4) ^ (j >> 4)) & 1) << 7) + 127;
            }
            else if (alpha != 255) // premultiply alpha for win32 AlphaBlend()
            {
               r = r * alpha >> 8;
               g = g * alpha >> 8;
               b = b * alpha >> 8;
            }
            tmp[o * 4 + 0] = b;
            tmp[o * 4 + 1] = g;
            tmp[o * 4 + 2] = r;
            tmp[o * 4 + 3] = alpha;
         }
      }
   }
   else
      assert(!"unknown format");

   return tex;
}

SDL_Surface* BaseTexture::ToSDLSurface() const
{
   SDL_PixelFormat format;
   switch (m_format)
   {
   case BW: format = SDL_PIXELFORMAT_INDEX8; break;
   case RGB: format = SDL_PIXELFORMAT_RGB24; break;
   case RGBA: format = SDL_PIXELFORMAT_RGBA32; break;
   case SRGB: format = SDL_PIXELFORMAT_RGB24; break;
   case SRGBA: format = SDL_PIXELFORMAT_RGBA32; break;
   case SRGB565: format = SDL_PIXELFORMAT_RGB565; break;
   case RGB_FP16: format = SDL_PIXELFORMAT_RGB48_FLOAT; break;
   case RGBA_FP16: format = SDL_PIXELFORMAT_RGBA64_FLOAT; break;
   case RGB_FP32: format = SDL_PIXELFORMAT_RGB96_FLOAT; break;
   case RGBA_FP32: format = SDL_PIXELFORMAT_RGBA128_FLOAT; break;
   default: format = SDL_PIXELFORMAT_UNKNOWN; break;
   }
   return format == SDL_PIXELFORMAT_UNKNOWN ? nullptr : SDL_CreateSurfaceFrom(m_width, m_height, format, const_cast<uint8_t*>(static_cast<const uint8_t*>(datac())), pitch());
}

void BaseTexture::UpdateMD5() const
{
   if (!m_isMD5Dirty)
      return;
   m_isMD5Dirty = false;
   generateMD5((uint8_t*)m_data, pitch() * height(), m_md5Hash);
}

void BaseTexture::UpdateOpaque() const
{
   if (!m_isOpaqueDirty)
      return;
   m_isOpaqueDirty = false;
   m_isOpaque = true;
   if (m_format == RGBA || m_format == SRGBA)
   {
      // RGBA_FP16/RGBA_FP32 could be transparent but for the time being, the alpha channel is always opaque, only added for driver's texture format support
      uint8_t* const __restrict pdst = m_data;
      constexpr unsigned int stride = 4;
      for (unsigned int y = 0; y < m_height && m_isOpaque; ++y)
      {
         const size_t offs = (size_t)(m_height - y - 1) * (m_width * stride);
         for (size_t o = offs; o < m_width * stride + offs; o += stride)
         {
            if (pdst[o + 3] != 255)
            {
               m_isOpaque = false;
               break;
            }
         }
      }
   }
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Texture::Texture(string name, PinBinary* ppb, unsigned int width, unsigned int height)
   : m_name(std::move(name))
   , m_width(width)
   , m_height(height)
   , m_ppb(ppb)
   , m_liveHash(((size_t)this) ^ ((uint64_t)ppb) ^ usec() ^ ((uint64_t)width << 16) ^ ((uint64_t)height << 32))
{
   assert(m_ppb != nullptr);
   assert(m_width > 0);
   assert(m_height > 0);
}

Texture* Texture::CreateFromObjectReader(IObjectReader& reader, PinTable* const pt)
{
   string name;
   string path;
   unsigned int width = 0;
   unsigned int height = 0;
   float alphaTestValue = static_cast<float>(-1.0 / 255.0);
   PinBinary* ppb = nullptr;
   bool isMD5Dirty = true;
   uint8_t md5Hash[16] = {};
   bool isOpaqueDirty = true;
   bool isOpaque = true;
   reader.AsObject(
      [&](const int id, IObjectReader& reader)
      {
         switch (id)
         {
         case FID(NAME): name = reader.AsString(); break;
         case FID(PATH): path = reader.AsString(); break;
         case FID(WDTH): width = reader.AsInt(); break;
         case FID(HGHT): height = reader.AsInt(); break;
         case FID(ALTV): alphaTestValue = reader.AsFloat() * (float)(1.0 / 255.0); break;
         case FID(MD5H):
            reader.AsRaw(md5Hash, 16);
            isMD5Dirty = false;
            break;
         case FID(OPAQ):
            isOpaque = reader.AsBool();
            isOpaqueDirty = false;
            break;
         case FID(BITS):
         {
            // The 'BITS' field is deprecated and only used in pre 10.8.1 files which were all BIFF streams so we can safely cast here
            BiffReader& br = (BiffReader&)reader;

            // Old files used to store some bitmaps as a 32-bit SBGRA picture, we now (10.8.1+) always use a compressed file format. Convert here to simplify the code
            const size_t size = (size_t)height * width;
            assert(ppb == nullptr && size != 0);

            // Uncompress to RGBA image
            uint8_t* const __restrict tmp = new uint8_t[size * 4];
            const LZWReader lzwreader(br.m_pistream, tmp, width * 4);

            // Find out if all alpha values are 0x00 or 0xFF
            bool has_alpha = false;
            for (size_t o = 3; o < size * 4; o += 4)
               if (tmp[o] != 0 && tmp[o] != 255)
               {
                  has_alpha = true;
                  break;
               }

            // Create a FreeImage from LZW data, optionally dropping a constant (0 or 255) alpha channel
            FIBITMAP* dib = FreeImage_Allocate(width, height, has_alpha ? 32 : 24);
            uint8_t* const pdst = (uint8_t*)FreeImage_GetBits(dib);
            const unsigned int pitch = width * 4;
            const unsigned int pitch_dst = FreeImage_GetPitch(dib);
            const uint8_t* spch = tmp + (height * pitch);
            for (unsigned int i = 0; i < height; i++)
            {
               const uint32_t* const __restrict src = (const uint32_t*)(spch -= pitch); // start on previous previous line
               uint8_t* __restrict dst = pdst + i * pitch_dst;
               if (has_alpha)
                  memcpy(dst, src, pitch);
               else
                  copy_rgba_rgb<false>(dst, src, width); // copy without alpha channel
            }

            // Convert to a lossless webp
            auto memStream = FreeImage_OpenMemory();
            FreeImage_SaveToMemory(FREE_IMAGE_FORMAT::FIF_WEBP, dib, memStream, WEBP_LOSSLESS);
            ppb = new PinBinary();
            ppb->m_buffer.resize(FreeImage_TellMemory(memStream));
            ppb->m_name = name;
            const string ext = extension_from_path(path);
            if (!ext.empty())
            {
               path.erase(path.length() - ext.length());
               path += "webp"sv;
            }
            ppb->m_path = PathFromString(path);
            FreeImage_SeekMemory(memStream, 0, SEEK_SET);
            FreeImage_ReadMemory(ppb->m_buffer.data(), 1, static_cast<unsigned int>(ppb->m_buffer.size()), memStream);
            FreeImage_CloseMemory(memStream);
            FreeImage_Unload(dib);
            break;
         }
         case FID(JPEG): // JPEG may be misleading as this chunk contains original binary image data (in whatever format JPEG, PNG, EXR,...)
         {
            assert(ppb == nullptr);
            ppb = new PinBinary();
            ppb->Load(reader);
            if (reader.HasError())
            {
               assert(!"Invalid binary image file");
               return false;
            }
            break;
         }
         case FID(LINK):
         {
            int linkid = reader.AsInt();
            if (pt == nullptr)
            {
               assert(!"Invalid Texture load with link and no table to resolve links");
               return false;
            }
            ppb = pt->GetImageLinkBinary(linkid);
            if (!ppb)
            {
               assert(!"Invalid PinBinary");
               return false;
            }
            break;
         }
         // Legacy field, now unused
         case FID(SIGN): reader.AsBool(); break; // Signed image
         }
         return true;
      });

   if (ppb == nullptr)
      return nullptr;

   Texture* const tex = new Texture(name, ppb, width, height);
   tex->m_alphaTestValue = alphaTestValue;
   if (!isOpaqueDirty)
      tex->SetIsOpaque(isOpaque);
   if (!isMD5Dirty)
      tex->SetMD5Hash(md5Hash);
   return tex;
}

Texture* Texture::CreateFromFile(const std::filesystem::path& filename, const bool isImageData)
{
   PinBinary* const ppb = new PinBinary();
   ppb->ReadFromFile(filename);

   std::shared_ptr<BaseTexture> const imageBuffer = BaseTexture::CreateFromData(ppb->m_buffer.data(), ppb->m_buffer.size(), isImageData);
   if (imageBuffer == nullptr)
   {
      delete ppb;
      return nullptr;
   }

   Texture* tex = new Texture(TitleFromFilename(filename), ppb, imageBuffer->m_realWidth, imageBuffer->m_realHeight);
   tex->m_imageBuffer = imageBuffer;
   tex->UpdateMD5();
   tex->UpdateOpaque();
   return tex;
}

Texture::~Texture()
{
   delete m_ppb;
}

void Texture::Save(IObjectWriter& writer, PinTable* pt) const
{
   writer.WriteString(FID(NAME), m_name);
   writer.WriteString(FID(PATH), m_ppb->m_path.string());
   writer.WriteInt(FID(WDTH), m_width);
   writer.WriteInt(FID(HGHT), m_height);
   if (pt && pt->GetImageLink(this))
      writer.WriteInt(FID(LINK), 1);
   else
   {
      writer.BeginObject(FID(JPEG), false, false);
      m_ppb->Save(writer);
   }
   writer.WriteFloat(FID(ALTV), m_alphaTestValue * 255.0f);
   writer.WriteRaw(FID(MD5H), GetMD5Hash(), 16);
   writer.WriteBool(FID(OPAQ), IsOpaque());
   writer.EndObject();
}

bool Texture::IsHDR() const
{
   auto buffer = m_imageBuffer.lock();
   if (buffer)
      return buffer->m_format == BaseTexture::RGB_FP16 || buffer->m_format == BaseTexture::RGBA_FP16
          || buffer->m_format == BaseTexture::RGB_FP32 || buffer->m_format == BaseTexture::RGBA_FP32;
   const string ext = lowerCase(m_ppb->m_path.extension().string());
   return (ext == ".exr") || (ext == ".hdr");
}

size_t Texture::GetEstimatedGPUSize() const
{
   size_t estimatedSize;
   auto buffer = m_imageBuffer.lock();
   if (buffer)
      estimatedSize = static_cast<size_t>(buffer->height()) * static_cast<size_t>(buffer->pitch());
   else
      estimatedSize = static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * (IsHDR() ? 8 : 4); // 8 bytes per pixel for HDR (RGBA_FP16) and 4 bytes per pixel for non-HDR (RGBA)
   // Add mipmaps (+1/3).
   return (4 * estimatedSize) / 3;
}

std::shared_ptr<const BaseTexture> Texture::GetRawBitmap(bool resizeOnLowMem, unsigned int maxTexDimension) const
{
   auto buffer = m_imageBuffer.lock();
   if (buffer)
      return buffer;
   //PLOGD << "Decoding image " << m_name;
   buffer = std::shared_ptr<BaseTexture>(BaseTexture::CreateFromData(m_ppb->m_buffer.data(), m_ppb->m_buffer.size(), true, maxTexDimension, resizeOnLowMem));
   if (buffer && m_width != buffer->m_realWidth)
   {
      PLOGE << "Corrupted file: image '" << m_name << "' width (" << buffer->m_realWidth << ") does not match the width (" << m_width << ") of the image datablock.";
      const_cast<Texture*>(this)->m_width = buffer->m_realWidth;
   }
   if (buffer && m_height != buffer->m_realHeight)
   {
      PLOGE << "Corrupted file: image '" << m_name << "' height (" << buffer->m_realHeight << ") does not match the height (" << m_height << ") of the image datablock.";
      const_cast<Texture*>(this)->m_height = buffer->m_realHeight;
   }
   m_imageBuffer = buffer;
   UpdateOpaque();
   return buffer;
}

HBITMAP Texture::GetGDIBitmap() const
{
   return nullptr;
}

void Texture::UpdateMD5() const
{
   if (!m_isMD5Dirty)
      return;
   m_isMD5Dirty = false;
   generateMD5(m_ppb->m_buffer.data(), m_ppb->m_buffer.size(), m_md5Hash);
   SetMD5Hash(m_md5Hash);
}

void Texture::SetMD5Hash(uint8_t* md5) const
{
   memcpy(m_md5Hash, md5, sizeof(m_md5Hash));
   m_isMD5Dirty = false;
   auto buffer = m_imageBuffer.lock();
   if (buffer)
      buffer->SetMD5Hash(md5);
}

void Texture::UpdateOpaque() const
{
   if (!m_isOpaqueDirty)
      return;
   m_isOpaqueDirty = false;
   const auto bitmap = GetRawBitmap(false, 0);
   m_isOpaque = bitmap ? bitmap->IsOpaque() : false;
}

void Texture::SetIsOpaque(const bool v) const
{
   m_isOpaque = v;
   m_isOpaqueDirty = false;
   auto buffer = m_imageBuffer.lock();
   if (buffer)
      buffer->SetIsOpaque(v);
}
