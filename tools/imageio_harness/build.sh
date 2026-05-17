#!/usr/bin/env bash
# Phase 0 instrumentation build. Standalone — does not depend on the
# main project's CMake. The FreeImage and ImageIO decoders live in
# separate TUs because their headers' BOOL typedefs are incompatible.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"

FREEIMAGE_INC="$REPO/third-party/include"
FREEIMAGE_LIB="$REPO/third-party/runtime-libs/macos-arm64"

CXXFLAGS=(
   -std=c++20
   -arch arm64
   -mmacosx-version-min=14.0
   -O2 -g
   -Wall -Wextra -Wno-unused-parameter
   -I"$HERE"
)

# FreeImage TU — pure C++, FreeImage headers only.
clang++ "${CXXFLAGS[@]}" \
   -I"$FREEIMAGE_INC" \
   -c "$HERE/freeimage_decoder.cpp" \
   -o "$HERE/freeimage_decoder.o"

# ImageIO TU — Obj-C++, Apple frameworks only.
clang++ "${CXXFLAGS[@]}" \
   -fobjc-arc \
   -c "$HERE/imageio_decoder.mm" \
   -o "$HERE/imageio_decoder.o"

# Main driver — pure C++.
clang++ "${CXXFLAGS[@]}" \
   -c "$HERE/main.cpp" \
   -o "$HERE/main.o"

clang++ \
   -arch arm64 \
   -mmacosx-version-min=14.0 \
   -L"$FREEIMAGE_LIB" \
   -Wl,-rpath,"$FREEIMAGE_LIB" \
   -framework Foundation \
   -framework ImageIO \
   -framework CoreGraphics \
   -framework CoreFoundation \
   -lfreeimage \
   "$HERE/freeimage_decoder.o" \
   "$HERE/imageio_decoder.o" \
   "$HERE/main.o" \
   -o "$HERE/imageio_harness"

clang++ "${CXXFLAGS[@]}" \
   -I"$FREEIMAGE_INC" \
   -L"$FREEIMAGE_LIB" \
   -Wl,-rpath,"$FREEIMAGE_LIB" \
   -lfreeimage \
   "$HERE/reencode.cpp" \
   -o "$HERE/reencode"

rm -f "$HERE"/*.o
echo "built: $HERE/imageio_harness"
echo "built: $HERE/reencode"
