// Phase 0 instrumentation: compare FreeImage decode vs ImageIO decode
// for the same input image. Hashes each canonical pixel buffer with
// SHA-256 and reports per-byte diff statistics. Both decoders live in
// separate TUs because their headers' BOOL typedefs are incompatible.

#include "bridge.h"

#include <CommonCrypto/CommonDigest.h>

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>

static std::vector<uint8_t> read_file(const char* path) {
   std::ifstream f(path, std::ios::binary);
   if (!f) return {};
   f.seekg(0, std::ios::end);
   const auto n = static_cast<size_t>(f.tellg());
   f.seekg(0, std::ios::beg);
   std::vector<uint8_t> buf(n);
   f.read(reinterpret_cast<char*>(buf.data()), n);
   return buf;
}

static std::string sha256_hex(const uint8_t* data, size_t len) {
   uint8_t digest[CC_SHA256_DIGEST_LENGTH];
   CC_SHA256(data, static_cast<CC_LONG>(len), digest);
   char hex[CC_SHA256_DIGEST_LENGTH * 2 + 1];
   for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; ++i)
      std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
   return std::string(hex);
}

static void report(const char* name, const CanonicalRaw& c) {
   std::printf("[%s] ok=%d w=%u h=%u  note=%s\n",
      name, c.ok, c.width, c.height, c.note);
   if (c.ok)
      std::printf("       sha256=%s\n",
         sha256_hex(c.rgba, static_cast<size_t>(c.width) * c.height * 4).c_str());
}

static void diff(const CanonicalRaw& a, const CanonicalRaw& b) {
   if (!a.ok || !b.ok) {
      std::printf("[diff] one side failed; cannot compare\n");
      return;
   }
   if (a.width != b.width || a.height != b.height) {
      std::printf("[diff] dimension mismatch (%ux%u vs %ux%u)\n",
         a.width, a.height, b.width, b.height);
      return;
   }
   const size_t n = static_cast<size_t>(a.width) * a.height * 4;
   size_t diff_pixels = 0;
   long long sum_abs_r = 0, sum_abs_g = 0, sum_abs_b = 0, sum_abs_a = 0;
   int max_r = 0, max_g = 0, max_b = 0, max_a = 0;
   for (size_t i = 0; i < n; i += 4) {
      const int dr = std::abs(static_cast<int>(a.rgba[i+0]) - b.rgba[i+0]);
      const int dg = std::abs(static_cast<int>(a.rgba[i+1]) - b.rgba[i+1]);
      const int db = std::abs(static_cast<int>(a.rgba[i+2]) - b.rgba[i+2]);
      const int da = std::abs(static_cast<int>(a.rgba[i+3]) - b.rgba[i+3]);
      if (dr || dg || db || da) ++diff_pixels;
      sum_abs_r += dr; sum_abs_g += dg; sum_abs_b += db; sum_abs_a += da;
      if (dr > max_r) max_r = dr;
      if (dg > max_g) max_g = dg;
      if (db > max_b) max_b = db;
      if (da > max_a) max_a = da;
   }
   const size_t total_pixels = n / 4;
   std::printf("[diff] differing pixels = %zu / %zu (%.2f%%)\n",
      diff_pixels, total_pixels,
      100.0 * static_cast<double>(diff_pixels) / total_pixels);
   std::printf("[diff] mean |delta|  R=%.3f G=%.3f B=%.3f A=%.3f\n",
      static_cast<double>(sum_abs_r) / total_pixels,
      static_cast<double>(sum_abs_g) / total_pixels,
      static_cast<double>(sum_abs_b) / total_pixels,
      static_cast<double>(sum_abs_a) / total_pixels);
   std::printf("[diff] max  |delta|  R=%d G=%d B=%d A=%d\n",
      max_r, max_g, max_b, max_a);
}

static void report_f(const char* name, const CanonicalRGB32F& c) {
   std::printf("[%s] ok=%d w=%u h=%u  note=%s\n",
      name, c.ok, c.width, c.height, c.note);
   if (c.ok) {
      const size_t n = static_cast<size_t>(c.width) * c.height * 3;
      float mn =  std::numeric_limits<float>::infinity();
      float mx = -std::numeric_limits<float>::infinity();
      size_t nans = 0, infs = 0;
      for (size_t i = 0; i < n; ++i) {
         const float v = c.rgb[i];
         if (std::isnan(v)) { ++nans; continue; }
         if (std::isinf(v)) { ++infs; continue; }
         if (v < mn) mn = v;
         if (v > mx) mx = v;
      }
      std::printf("       range=[%g .. %g]  nans=%zu  infs=%zu\n",
         mn, mx, nans, infs);
      std::printf("       sha256=%s\n",
         sha256_hex(reinterpret_cast<const uint8_t*>(c.rgb), n * sizeof(float)).c_str());
   }
}

static void diff_f(const CanonicalRGB32F& a, const CanonicalRGB32F& b) {
   if (!a.ok || !b.ok) {
      std::printf("[diff] one side failed; cannot compare\n");
      return;
   }
   if (a.width != b.width || a.height != b.height) {
      std::printf("[diff] dimension mismatch (%ux%u vs %ux%u)\n",
         a.width, a.height, b.width, b.height);
      return;
   }
   const size_t n = static_cast<size_t>(a.width) * a.height * 3;
   double sum_abs_r = 0, sum_abs_g = 0, sum_abs_b = 0;
   float max_r = 0, max_g = 0, max_b = 0;
   double sum_rel = 0;
   float max_rel = 0;
   size_t diff_pixels = 0;
   size_t finite_pixels = 0;
   for (size_t i = 0; i < n; i += 3) {
      const float ar = a.rgb[i+0], ag = a.rgb[i+1], ab = a.rgb[i+2];
      const float br = b.rgb[i+0], bg = b.rgb[i+1], bb = b.rgb[i+2];
      if (!std::isfinite(ar) || !std::isfinite(ag) || !std::isfinite(ab)
       || !std::isfinite(br) || !std::isfinite(bg) || !std::isfinite(bb)) continue;
      const float dr = std::fabs(ar - br), dg = std::fabs(ag - bg), db = std::fabs(ab - bb);
      if (dr > 0 || dg > 0 || db > 0) ++diff_pixels;
      sum_abs_r += dr; sum_abs_g += dg; sum_abs_b += db;
      if (dr > max_r) max_r = dr;
      if (dg > max_g) max_g = dg;
      if (db > max_b) max_b = db;
      const float scale_r = std::fmax(std::fabs(ar), std::fabs(br));
      const float scale_g = std::fmax(std::fabs(ag), std::fabs(bg));
      const float scale_b = std::fmax(std::fabs(ab), std::fabs(bb));
      const float rel_r = scale_r > 0 ? dr / scale_r : 0;
      const float rel_g = scale_g > 0 ? dg / scale_g : 0;
      const float rel_b = scale_b > 0 ? db / scale_b : 0;
      const float rel_max = std::fmax(rel_r, std::fmax(rel_g, rel_b));
      sum_rel += rel_max;
      if (rel_max > max_rel) max_rel = rel_max;
      ++finite_pixels;
   }
   if (finite_pixels == 0) {
      std::printf("[diff] no finite pixels to compare\n");
      return;
   }
   std::printf("[diff] differing pixels = %zu / %zu  (finite=%zu)\n",
      diff_pixels, n / 3, finite_pixels);
   std::printf("[diff] mean |delta|  R=%g G=%g B=%g\n",
      sum_abs_r / finite_pixels, sum_abs_g / finite_pixels, sum_abs_b / finite_pixels);
   std::printf("[diff] max  |delta|  R=%g G=%g B=%g\n", max_r, max_g, max_b);
   std::printf("[diff] mean rel err   = %g   max rel err = %g\n",
      sum_rel / finite_pixels, max_rel);
}

int main(int argc, char** argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s <image>\n", argv[0]);
      return 2;
   }
   const char* path = argv[1];
   const auto src = read_file(path);
   if (src.empty()) {
      std::fprintf(stderr, "could not read %s\n", path);
      return 1;
   }
   std::printf("=== %s (%zu bytes) ===\n", path, src.size());

   // 8-bit pass
   CanonicalRaw fi = decode_freeimage(src.data(), src.size());
   CanonicalRaw io = decode_imageio  (src.data(), src.size());
   report("FreeImage", fi);
   report("ImageIO  ", io);
   diff(fi, io);
   canonical_free(&fi);
   canonical_free(&io);

   // Float pass — run unconditionally so we can see what each decoder
   // says about the input. FreeImage's note will state whether it's a
   // float source; ImageIO's note will too.
   std::printf("\n-- float pass --\n");
   CanonicalRGB32F ffi = decode_freeimage_rgb32f(src.data(), src.size());
   CanonicalRGB32F fio = decode_imageio_rgb32f  (src.data(), src.size());
   report_f("FreeImage", ffi);
   report_f("ImageIO  ", fio);
   diff_f(ffi, fio);
   canonical_rgb32f_free(&ffi);
   canonical_rgb32f_free(&fio);

   return 0;
}
