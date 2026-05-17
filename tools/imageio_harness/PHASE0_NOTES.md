# Phase 0 instrumentation notes

Goal: verify that FreeImage and ImageIO can produce byte-identical
canonical pixel buffers from the same input image, before any
production code is touched. Canonical layout is
`width*height*4` bytes, RGBA byte order in memory, top-down rows,
tightly packed, straight (un-premultiplied) alpha, sRGB.

## Build / run

```
tools/imageio_harness/build.sh
tools/imageio_harness/imageio_harness <image>
```

## Format coverage

| Format | Status | Notes |
|---|---|---|
| PNG 8-bit RGBA | ✅ byte-identical | 14seg.png, 7seg-williams.png, vpinball.png, 16seg.png |
| PNG 8-bit re-encoded | ✅ byte-identical | sips re-encode of vpinball.png |
| JPEG | ⚠ near-identical | vpinball.jpg: 57% pixels differ, max ±9 per channel — JPEG-decoder variance |
| WebP (no alpha) | ✅ byte-identical | BumperBase, BumperRing, BumperCap, AODither, EnvMap, KickerCup |
| EXR (dwab compression) | ❌ ImageIO can't open | `BallEnv.exr` uses DWA-B compression (OpenEXR 2.2+) — not supported by Apple's decoder |
| EXR (PIZ/ZIP/NONE compression) | ⚠ FP16 — needs harness extension | After re-encoding via FreeImage, ImageIO opens it at half-float (bpc=16) |
| TIFF float (32-bit) | ✅ byte-identical | Re-encoded BallEnv via FreeImage: SHA-256 identical between paths |
| Radiance .hdr | ✅ byte-identical | Re-encoded BallEnv via FreeImage: SHA-256 identical between paths (relative to .hdr's lossy RGBE) |
| 32-bit SBGRA from .vpx | not yet tested | Phase 3 concern (encode side), not decode |

## Findings so far

**ImageIO returns straight-alpha RGBA top-down for the LDR formats we use.**
Calling `CGDataProviderCopyData(CGImageGetDataProvider(image))` on a
CGImage made via `CGImageSourceCreateImageAtIndex` returns:

| Source | Native layout from ImageIO |
|---|---|
| PNG with alpha   | `bpc=8 bpp=32` alpha=`kCGImageAlphaLast` (3) big-endian |
| PNG without alpha (still RGBA tagged) | same as above |
| JPEG             | `bpc=8 bpp=32` alpha=`kCGImageAlphaNoneSkipLast` (5) |
| WebP             | `bpc=8 bpp=32` alpha=`kCGImageAlphaNoneSkipLast` (5) |

No premultiplication, no Y-flip, no BGR/RGB swizzle needed at this layer.
All bundled PNG and WebP assets match FreeImage byte-for-byte. JPEGs
diverge slightly (±9 units max) due to decoder-implementation variance
between libjpeg(-turbo) and Apple's internal JPEG decoder — visually
imperceptible; accepted.

This rules out the prime suspect from the rolled-back attempt: the
decoded bytes themselves match. The green/purple rendering bug lives
**downstream** of `CreateFromData` — most likely BGFX texture upload
sRGB / colorspace handling, or the premultiplied-vs-straight contract
of the shader sampling path.

**EXR specifics: `BallEnv.exr` uses dwab compression.** Apple's
ImageIO claims `com.ilm.openexr-image` support (visible via
`CGImageSourceCopyTypeIdentifiers`), and after re-encoding the bundled
`BallEnv.exr` to PIZ/ZIP/NONE compression via FreeImage, ImageIO opens
it cleanly — at half-float precision (`bpc=16 bpp=64
alpha=kCGImageAlphaNoneSkipLast`). The dynamic range of this cube map
fits FP16 (max value ~5.79), so half-float is sufficient.

**Phase 1 conversion target (informed by these results):**

| Target format | ImageIO opens? | Precision | Bytes | Verdict |
|---|---|---|---|---|
| EXR / dwab (current) | NO | FP32 source | 343 KB | ❌ unusable |
| EXR / PIZ            | YES (FP16) | FP16 | 1.5 MB | best of EXR variants |
| EXR / ZIP            | YES (FP16) | FP16 | 1.8 MB | |
| EXR / NONE           | YES (FP16) | FP16 | 3.0 MB | |
| TIFF float           | YES (FP32) | FP32 | 6.0 MB | byte-identical round-trip |
| Radiance .hdr        | YES (FP32) | RGBE (lossy)| 1.6 MB | byte-identical round-trip |
| HEIC (float)         | not yet tested | likely FP16 | small | needs probe |

Recommendation: re-encode `BallEnv.exr` with PIZ compression for Phase 1
(smallest + lossless within FP16 range). Use the same conversion script
to produce a `.tif-float` companion for the harness's byte-identical
regression test.

**Phase 1 completed 2026-05-17**: `src/assets/BallEnv.exr` replaced
in place — DWA-B → PIZ. Verified via harness: FreeImage re-decode
produces SHA-256 `a63e39e1c2a7b2c09194339c2bdb88e81543f19cffd95bf4b57a4a8cd15dff9b`
(matches the pre-swap DWA-B decode bit-for-bit). ImageIO now opens
the file (at FP16). File grew 344 KB → 1.5 MB; acceptable for one
asset to gain Apple-decoder compatibility. No other bundled assets
needed conversion (see [[feedback-modernise-decoder-not-format]]).

## Pre-existing test asset to keep

Conversion is reproducible via:
```
tools/imageio_harness/reencode <input> <output> <format>
   formats: exr-piz exr-none exr-zip tif-float hdr png16
```

## Implementation gotcha

FreeImage.h (Wine-flavored) typedefs `BOOL` as `int32_t`. Apple's
`objc.h` typedefs `BOOL` as `bool`. They cannot coexist in one TU.
Split the harness into three: `freeimage_decoder.cpp` (FreeImage only),
`imageio_decoder.mm` (Apple frameworks only), `main.cpp` (driver),
bridged by a pure-C ABI in `bridge.h`. Same pattern documented in
the SwiftUI shell gotchas memory.
