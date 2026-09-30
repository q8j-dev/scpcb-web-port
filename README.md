# SCP: Containment Breach (Web)

SCP: Containment Breach running in your browser. No install, just WebAssembly and WebGPU.

**Play it:** https://q8j-dev.github.io/scpcb-web-port/

The game itself is untouched. It's built with [blitz3d-ng](https://github.com/blitz3d-ng/blitz3d-ng), and this repo adds a WebGPU renderer to it (`blitz3d-ng/src/modules/bb/graphics.webgpu`) plus a few fixes.

## What's here

- `upstream-scpcb/` the game source
- `blitz3d-ng/` the compiler and runtime, with the WebGPU backend added
- `web-shell/` the page that boots the game
- `tools/` asset packaging and dependency scripts
- `webgame/` build output (not checked in)

## Building

Works on macOS, Windows and Linux. Install these first:

- CMake and Ninja
- Python 3
- the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html), activated in your terminal
- **Windows:** Visual Studio 2022 with "Desktop development with C++" and MFC (no special prompt needed, the script finds it)
- **Linux:** a C++ toolchain plus, on Ubuntu/Debian: `git autoconf libtool gettext autopoint gperf clang libxml2-dev zlib1g-dev libwxgtk3.0-gtk3-dev libxrandr-dev libxinerama-dev libxcursor-dev uuid-dev libfontconfig1-dev`

Then:

```
python3 build.py
```

That does everything: fetches dependencies, gets LLVM, builds the compiler and the wasm runtime, packs the assets and links the game into `webgame/`. Finished steps are skipped on re-runs. You can also run one at a time (`python3 build.py --help` lists them).

LLVM comes prebuilt on macOS (Apple Silicon) and Windows. On Linux and Intel Macs there's no prebuilt archive, so the first build compiles LLVM from source, which takes a while but only happens once.

## Running it

```
python3 build.py --serve
```

or, if it's already built, `cd webgame && python3 -m http.server 8090`.

Open `http://127.0.0.1:8090/` in a browser with WebGPU (Chrome or Edge works best; Firefox and Safari vary).
