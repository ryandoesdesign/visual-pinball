// Bridge between the two decoder TUs. Pure C ABI so the FreeImage side
// (which pulls in <windows.h>'s BOOL=int via the libwinevbs-flavored
// FreeImage.h) and the ImageIO side (which pulls in objc.h's BOOL=bool
// via Foundation) never see each other's headers.
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Canonical pixel buffer description shared by both decoders:
//   width*height*4 bytes, RGBA byte order in memory, top-down rows,
//   tightly packed, STRAIGHT (un-premultiplied) alpha, sRGB.
typedef struct {
   uint32_t width;
   uint32_t height;
   uint8_t* rgba;     // malloc'd; caller frees via canonical_free
   int      ok;       // 1 on success, 0 on failure
   char     note[256];
} CanonicalRaw;

void canonical_free(CanonicalRaw* c);

CanonicalRaw decode_freeimage(const uint8_t* data, size_t size);
CanonicalRaw decode_imageio  (const uint8_t* data, size_t size);

// Float canonical buffer for HDR sources (EXR, Radiance .hdr, etc.):
//   width*height*3 floats, R,G,B order, top-down rows, tightly packed,
//   linear (un-tonemapped) values. No alpha.
typedef struct {
   uint32_t width;
   uint32_t height;
   float*   rgb;      // malloc'd; caller frees via canonical_rgb32f_free
   int      ok;
   char     note[256];
} CanonicalRGB32F;

void canonical_rgb32f_free(CanonicalRGB32F* c);

CanonicalRGB32F decode_freeimage_rgb32f(const uint8_t* data, size_t size);
CanonicalRGB32F decode_imageio_rgb32f  (const uint8_t* data, size_t size);

#ifdef __cplusplus
} // extern "C"
#endif
