# neomod
[![win-multiarch](https://github.com/neomodnet/neomod/actions/workflows/win-multiarch.yml/badge.svg)](https://github.com/neomodnet/neomod/actions/workflows/win-multiarch.yml)
[![linux-multiarch](https://github.com/neomodnet/neomod/actions/workflows/linux-multiarch.yml/badge.svg)](https://github.com/neomodnet/neomod/actions/workflows/linux-multiarch.yml)
[![linux-aarch64](https://github.com/neomodnet/neomod/actions/workflows/linux-aarch64.yaml/badge.svg)](https://github.com/neomodnet/neomod/actions/workflows/linux-aarch64.yaml)
[![web-wasm32](https://github.com/neomodnet/neomod/actions/workflows/web-wasm32.yml/badge.svg)](https://github.com/neomodnet/neomod/actions/workflows/web-wasm32.yml)
[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/neomodnet/neomod)

This is a third-party fork of McKay's [McOsu](https://store.steampowered.com/app/607260/McOsu/).

If you need help, contact `kiwec` or `spec.ta.tor` on Discord, either by direct message or on [the neomod server](https://discord.com/invite/YWPBFSpH8v).

### Building

The recommended way to build (and the way releases are made) is using gcc/gcc-mingw.

- For all *nix systems, run `./autogen.sh` in the top-level folder (once) to generate the build files.
- Create and enter a build subdirectory; e.g. `mkdir build && cd build`
- On Linux, for Linux -> run `../configure`, then `make install`
  - This will build and install everything under `./dist/bin-$arch`, configurable with the `--prefix` option to `configure`
- On Linux/WSL, for Windows -> run ` ../configure --host=x86_64-w64-mingw32`, then `make install`

For an example of a GCC (Linux) build on Debian, see the [Linux](https://github.com/neomodnet/neomod/blob/master/.github/workflows/linux-multiarch.yml) Actions workflow (and [associated](https://github.com/neomodnet/neomod/blob/9c49f3f0d8924989092252ece4aeb9dec3f0c8bd/.github/workflows/docker/Dockerfile) [scripts](https://github.com/neomodnet/neomod/blob/9c49f3f0d8924989092252ece4aeb9dec3f0c8bd/.github/workflows/docker/build.sh)).

For an example of a MinGW-GCC build on Arch Linux, see the [Windows](https://github.com/neomodnet/neomod/blob/master/.github/workflows/win-multiarch.yml) Actions workflow.

These should help with finding a few obscure autotools-related packages that you might not have installed.

---

For debugging convenience, you can also do an **MSVC** build with **CMake** on **Windows**, by running `buildwin64.bat` in `cmake-win`. For this to work properly, a couple prerequisites you'll need:

- The [NASM](https://nasm.us/) assembler installed/in your PATH (it's searched for under `C:/Program Files/NASM` by default otherwise); used for bundling binary resources into the executable
- You'll probably also need `python3` + `pip` set up on your system, so that a newer version of `pkg-config` can be installed (automatic if you have `pip`); used for building/finding dependencies before the main project is built
