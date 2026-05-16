# How to build

This fork targets **macOS arm64 only** (BGFX backend). See `~/.claude/plans/now-that-the-project-async-bear.md` for the modernisation roadmap.

## Prerequisites

```bash
sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer
brew install autoconf automake libtool cmake bison curl
export PATH="$(brew --prefix bison)/bin:$PATH"
```

Xcode 26 (macOS 26 SDK) is the supported toolchain.

## External dependencies

Builds SDL3, BGFX, FFmpeg, FreeImage, libpinmame, libdmdutil, libaltsound, libdof, libwinevbs, libzip. SHAs are pinned in `platforms/config.sh`. Run from the project root:

```bash
platforms/macos-arm64/external.sh
```

This populates `third-party/runtime-libs/macos-arm64/` and refreshes headers under `third-party/include/`. Subsequent runs are no-ops unless a SHA changes.

## Compiling

Top-level `CMakeLists.txt` is the build entry. From the project root:

```bash
cmake -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build -- -j$(sysctl -n hw.ncpu)
```

The output bundle is `build/VPinballX_BGFX.app`. Run it directly or via the binary inside:

```bash
build/VPinballX_BGFX.app/Contents/MacOS/VPinballX_BGFX -play src/assets/exampleTable.vpx
```

## Continuous Integration

See [CI workflows](../.github/workflows).
