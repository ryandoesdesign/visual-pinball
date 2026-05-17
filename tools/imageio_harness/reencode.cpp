// One-shot utility: load an image via FreeImage, save it in a chosen
// output format. Used during Phase 0 to probe which HDR-capable output
// formats ImageIO can read back.
//
//   reencode <input> <output> <format>
//   format ∈ { exr-piz, exr-none, exr-zip, tif-float, hdr, png16 }
//
// Pure C++ — no Apple frameworks (BOOL collision avoidance).

#include "FreeImage.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
   if (argc < 4) {
      std::fprintf(stderr, "usage: %s <input> <output> <format>\n", argv[0]);
      std::fprintf(stderr, "  format: exr-piz exr-none exr-zip tif-float hdr png16\n");
      return 2;
   }
   const char* in = argv[1];
   const char* out = argv[2];
   const char* fmt = argv[3];

   FreeImage_Initialise();

   FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(in, 0);
   if (fif == FIF_UNKNOWN) { std::fprintf(stderr, "unknown input format\n"); return 1; }

   FIBITMAP* dib = FreeImage_Load(fif, in, 0);
   if (!dib) { std::fprintf(stderr, "load failed\n"); return 1; }

   const FREE_IMAGE_TYPE t = FreeImage_GetImageType(dib);
   std::printf("loaded %s: %ux%u type=%d bpp=%u\n", in,
      FreeImage_GetWidth(dib), FreeImage_GetHeight(dib),
      static_cast<int>(t), FreeImage_GetBPP(dib));

   FREE_IMAGE_FORMAT outFif;
   int flags = 0;
   FIBITMAP* save = dib;
   FIBITMAP* converted = nullptr;

   if (std::strcmp(fmt, "exr-piz") == 0)      { outFif = FIF_EXR;  flags = EXR_DEFAULT; }
   else if (std::strcmp(fmt, "exr-none") == 0){ outFif = FIF_EXR;  flags = EXR_NONE; }
   else if (std::strcmp(fmt, "exr-zip") == 0) { outFif = FIF_EXR;  flags = EXR_ZIP; }
   else if (std::strcmp(fmt, "tif-float") == 0){
      outFif = FIF_TIFF;
      flags = TIFF_NONE;
      if (t != FIT_RGBF) {
         converted = FreeImage_ConvertToRGBF(dib);
         if (!converted) { std::fprintf(stderr, "ConvertToRGBF failed\n"); return 1; }
         save = converted;
      }
   }
   else if (std::strcmp(fmt, "hdr") == 0)     { outFif = FIF_HDR;  flags = 0;
      if (t != FIT_RGBF) {
         converted = FreeImage_ConvertToRGBF(dib);
         if (!converted) { std::fprintf(stderr, "ConvertToRGBF failed\n"); return 1; }
         save = converted;
      }
   }
   else if (std::strcmp(fmt, "png16") == 0)   { outFif = FIF_PNG; flags = 0;
      // PNG can store RGB16 but not float; only useful for [0,1] HDR.
      converted = FreeImage_ConvertToRGB16(dib);
      if (!converted) { std::fprintf(stderr, "ConvertToRGB16 failed\n"); return 1; }
      save = converted;
   }
   else { std::fprintf(stderr, "unknown format %s\n", fmt); return 1; }

   const BOOL ok = FreeImage_Save(outFif, save, out, flags);
   if (!ok) { std::fprintf(stderr, "save failed\n"); return 1; }
   std::printf("wrote %s (fif=%d flags=%d)\n", out, static_cast<int>(outFif), flags);

   if (converted) FreeImage_Unload(converted);
   FreeImage_Unload(dib);
   FreeImage_DeInitialise();
   return 0;
}
