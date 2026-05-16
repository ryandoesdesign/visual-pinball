# Visual Pinball (macOS arm64)

*An open source pinball table editor and simulator.*

This is a personal fork of [Visual Pinball](https://github.com/vpinball/vpinball) narrowed to **macOS arm64 only**. Upstream supports Windows, Linux, iOS/tvOS, and Android in addition to macOS; this fork drops every non-Apple-Silicon target along with the related build scripts, third-party SDKs, plugins, and source branches.

## Features

- Renders the table with OpenGL or [bgfx](https://bkaradzic.github.io/bgfx/overview.html) (Metal backend via bgfx)
- Pinball table physics simulation
- Live editing of most content within the rendered viewport
- Table logic (and game rules) controlled via Visual Basic Script through the [libwinevbs](https://github.com/vpinball/libwinevbs) Wine compatibility layer
- Plays the full Visual Pinball X table library (1000+ real machines, 500+ originals, 3000+ counting MODs and variants)
- Real-machine emulation via [PinMAME](https://github.com/vpinball/pinmame) (Visual PinMAME) or the libPinMAME plugin
- Configurable camera views (e.g. correct display in virtual pinball cabinets)
- Tablet/Touch input, Joypads, or specialized pinball controllers
- Stereo3D output and VR/XR HMD rendering (where supported on macOS)
- Plugin system for displays (DMD, backglass), dynamic content (PUP, Serum, etc.), and DOF

## Download

All releases are available on the [releases page](https://github.com/vpinball/vpinball/releases).

## Documentation

Documentation is currently sparse. Check the [docs](docs) directory for various guides and references.

An [unofficial wiki](https://github.com/dekay/vpinball-wiki) is currently being developed. Community contributions, suggestions, and help are welcome to improve the resource for all users.

## How to build

Build instructions are available in the [make directory README](make/README.md).
