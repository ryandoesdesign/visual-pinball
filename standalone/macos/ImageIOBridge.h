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

// PNG encode. channels must be 1 (grayscale), 3 (RGB), or 4 (RGBA).
// Source bytes are top-down, tightly packed, sRGB (linear interpretation
// for 1-channel). Always lossless.
//
// _to_memory: out_bytes is malloc'd, caller frees via vpx_imageio_free_buffer.
// _to_file:   writes PNG file to out_path; returns 0 on any failure.
int  vpx_imageio_encode_png_to_memory(uint32_t width, uint32_t height,
                                      uint32_t channels,
                                      const uint8_t* src_bytes,
                                      uint8_t** out_bytes, size_t* out_size);
int  vpx_imageio_save_png_to_file(uint32_t width, uint32_t height,
                                  uint32_t channels,
                                  const uint8_t* src_bytes,
                                  const char* out_path);
void vpx_imageio_free_buffer(void* p);

#ifdef __cplusplus
} // extern "C"
#endif
