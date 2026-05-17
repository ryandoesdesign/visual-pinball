// Apple-side image decoding for vpinball. Pure C ABI so the call sites
// in src/renderer/Texture.cpp (cross-platform C++ that pulls libwinevbs
// Wine headers) never see Foundation / ObjC headers — the two
// disagree on the BOOL typedef.

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Canonical pixel layouts the bridge can emit. All buffers are
// top-down, tightly packed (no row padding). 8-bit values are sRGB
// encoded with straight (un-premultiplied) alpha. Float values are
// linear.
typedef enum {
   VPX_IMAGEIO_FMT_NONE      = 0,
   VPX_IMAGEIO_FMT_RGB8      = 1,  // 24 bpp, R,G,B
   VPX_IMAGEIO_FMT_RGBA8     = 2,  // 32 bpp, R,G,B,A
   VPX_IMAGEIO_FMT_RGB_FP16  = 3,  // 48 bpp, half-float per channel
   VPX_IMAGEIO_FMT_RGB_FP32  = 4,  // 96 bpp, float per channel
} vpx_imageio_format_t;

typedef struct {
   uint32_t              width;
   uint32_t              height;
   vpx_imageio_format_t  format;
   uint8_t*              pixels;       // malloc'd; free via vpx_imageio_free
   size_t                pixels_size;  // bytes
} vpx_imageio_image_t;

// Decode encoded image bytes via ImageIO. Returns 1 on success and
// populates *out (caller frees out->pixels via vpx_imageio_free).
// Returns 0 on any failure (unknown format, unsupported layout,
// allocation failure). On failure *out is zeroed.
int  vpx_imageio_decode(const void* data, size_t size, vpx_imageio_image_t* out);
void vpx_imageio_free  (vpx_imageio_image_t* img);

#ifdef __cplusplus
} // extern "C"
#endif
