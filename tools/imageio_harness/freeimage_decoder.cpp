// FreeImage decode path. Pure C++, NO Foundation / ObjC headers — pulling
// either in would collide with FreeImage's BOOL=int32_t typedef.
//
// Output layout mirrors the non-float SRGBA branch of
// BaseTexture::CreateFromFreeImage (src/renderer/Texture.cpp:285,367-401):
// FreeImage_ConvertTo32Bits → walk bottom-up rows and emit top-down RGBA,
// swapping B/R because FreeImage stores 32-bit DIBs as B,G,R,A in memory
// (FI_RGBA_RED == 2).

#include "bridge.h"

#include "FreeImage.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>

static void set_note(CanonicalRaw* c, const char* s) {
   std::snprintf(c->note, sizeof(c->note), "%s", s);
}

extern "C" void canonical_free(CanonicalRaw* c) {
   if (c && c->rgba) {
      std::free(c->rgba);
      c->rgba = nullptr;
   }
}

extern "C" void canonical_rgb32f_free(CanonicalRGB32F* c) {
   if (c && c->rgb) {
      std::free(c->rgb);
      c->rgb = nullptr;
   }
}

static void set_note_f(CanonicalRGB32F* c, const char* s) {
   std::snprintf(c->note, sizeof(c->note), "%s", s);
}

extern "C" CanonicalRaw decode_freeimage(const uint8_t* data, size_t size) {
   CanonicalRaw out = {};

   FreeImage_Initialise();

   FIMEMORY* mem = FreeImage_OpenMemory(const_cast<BYTE*>(data),
                                        static_cast<DWORD>(size));
   if (!mem) { set_note(&out, "FreeImage_OpenMemory failed"); FreeImage_DeInitialise(); return out; }

   const FREE_IMAGE_FORMAT fif = FreeImage_GetFileTypeFromMemory(
      mem, static_cast<int>(size));
   if (fif == FIF_UNKNOWN || !FreeImage_FIFSupportsReading(fif)) {
      FreeImage_CloseMemory(mem);
      FreeImage_DeInitialise();
      set_note(&out, "FreeImage: unknown / unsupported format");
      return out;
   }

   FIBITMAP* dib = FreeImage_LoadFromMemory(fif, mem, 0);
   FreeImage_CloseMemory(mem);
   if (!dib) {
      FreeImage_DeInitialise();
      set_note(&out, "FreeImage_LoadFromMemory returned null");
      return out;
   }

   FIBITMAP* rgba = FreeImage_ConvertTo32Bits(dib);
   FreeImage_Unload(dib);
   if (!rgba) {
      FreeImage_DeInitialise();
      set_note(&out, "FreeImage_ConvertTo32Bits returned null");
      return out;
   }

   const uint32_t w = FreeImage_GetWidth(rgba);
   const uint32_t h = FreeImage_GetHeight(rgba);
   const uint32_t pitch = FreeImage_GetPitch(rgba);
   const uint8_t* bits = static_cast<uint8_t*>(FreeImage_GetBits(rgba));

   const size_t bytes = static_cast<size_t>(w) * h * 4;
   uint8_t* dst = static_cast<uint8_t*>(std::malloc(bytes));
   if (!dst) {
      FreeImage_Unload(rgba);
      FreeImage_DeInitialise();
      set_note(&out, "malloc failed");
      return out;
   }

   for (uint32_t y = 0; y < h; ++y) {
      // FreeImage DIB is bottom-up; canonical wants top-down.
      const uint8_t* srow = bits + static_cast<size_t>(y) * pitch;
      uint8_t* drow = dst + static_cast<size_t>(h - 1 - y) * w * 4;
      for (uint32_t x = 0; x < w; ++x) {
         drow[x*4 + 0] = srow[x*4 + FI_RGBA_RED];
         drow[x*4 + 1] = srow[x*4 + FI_RGBA_GREEN];
         drow[x*4 + 2] = srow[x*4 + FI_RGBA_BLUE];
         drow[x*4 + 3] = srow[x*4 + FI_RGBA_ALPHA];
      }
   }

   FreeImage_Unload(rgba);
   FreeImage_DeInitialise();

   out.width = w;
   out.height = h;
   out.rgba = dst;
   out.ok = 1;
   set_note(&out, "FreeImage_ConvertTo32Bits + BGRA->RGBA + Y-flip");
   return out;
}

extern "C" CanonicalRGB32F decode_freeimage_rgb32f(const uint8_t* data, size_t size) {
   CanonicalRGB32F out = {};

   FreeImage_Initialise();

   FIMEMORY* mem = FreeImage_OpenMemory(const_cast<BYTE*>(data),
                                        static_cast<DWORD>(size));
   if (!mem) { set_note_f(&out, "FreeImage_OpenMemory failed"); FreeImage_DeInitialise(); return out; }

   const FREE_IMAGE_FORMAT fif = FreeImage_GetFileTypeFromMemory(
      mem, static_cast<int>(size));
   if (fif == FIF_UNKNOWN || !FreeImage_FIFSupportsReading(fif)) {
      FreeImage_CloseMemory(mem);
      FreeImage_DeInitialise();
      set_note_f(&out, "FreeImage: unknown / unsupported format");
      return out;
   }

   FIBITMAP* dib = FreeImage_LoadFromMemory(fif, mem, 0);
   FreeImage_CloseMemory(mem);
   if (!dib) {
      FreeImage_DeInitialise();
      set_note_f(&out, "FreeImage_LoadFromMemory returned null");
      return out;
   }

   const FREE_IMAGE_TYPE t = FreeImage_GetImageType(dib);
   char nbuf[256];
   std::snprintf(nbuf, sizeof(nbuf), "FreeImage native type=%d", static_cast<int>(t));
   set_note_f(&out, nbuf);

   const bool is_float = (t == FIT_FLOAT) || (t == FIT_RGBF) || (t == FIT_RGBAF);
   if (!is_float) {
      FreeImage_Unload(dib);
      FreeImage_DeInitialise();
      std::snprintf(out.note + std::strlen(out.note),
         sizeof(out.note) - std::strlen(out.note),
         "; not a float source");
      return out;
   }

   FIBITMAP* rgbf = (t == FIT_RGBF) ? dib : FreeImage_ConvertToRGBF(dib);
   if (rgbf == nullptr) {
      FreeImage_Unload(dib);
      FreeImage_DeInitialise();
      std::snprintf(out.note + std::strlen(out.note),
         sizeof(out.note) - std::strlen(out.note),
         "; FreeImage_ConvertToRGBF returned null");
      return out;
   }
   if (rgbf != dib) FreeImage_Unload(dib);

   const uint32_t w = FreeImage_GetWidth(rgbf);
   const uint32_t h = FreeImage_GetHeight(rgbf);
   const uint32_t pitch = FreeImage_GetPitch(rgbf);
   const uint8_t* bits = static_cast<uint8_t*>(FreeImage_GetBits(rgbf));

   float* dst = static_cast<float*>(std::malloc(static_cast<size_t>(w) * h * 3 * sizeof(float)));
   if (!dst) {
      FreeImage_Unload(rgbf);
      FreeImage_DeInitialise();
      return out;
   }

   for (uint32_t y = 0; y < h; ++y) {
      // FreeImage float DIBs are also bottom-up; Texture.cpp flips them
      // on copy (line 346) so the canonical wants top-down too.
      const float* srow = reinterpret_cast<const float*>(bits + static_cast<size_t>(y) * pitch);
      float* drow = dst + static_cast<size_t>(h - 1 - y) * w * 3;
      std::memcpy(drow, srow, static_cast<size_t>(w) * 3 * sizeof(float));
   }

   FreeImage_Unload(rgbf);
   FreeImage_DeInitialise();

   out.width = w;
   out.height = h;
   out.rgb = dst;
   out.ok = 1;
   std::snprintf(out.note + std::strlen(out.note),
      sizeof(out.note) - std::strlen(out.note),
      "; ConvertToRGBF + Y-flip");
   return out;
}
