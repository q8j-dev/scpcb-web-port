# SCP: Containment Breach (Web)

SCP: Containment Breach in the browser, using WebAssembly and WebGPU. Play it at https://q8j-dev.github.io/scpcb-web-port/

The game source is unmodified except for an `Include "WebShims.bb"` line at the top of `Main.bb` and the added `WebShims.bb`, which stubs Steam and Discord calls. It is compiled with [blitz3d-ng](https://github.com/blitz3d-ng/blitz3d-ng), which this repo extends with a WebGPU renderer in `blitz3d-ng/src/modules/bb/graphics.webgpu`.

## Layout

- `upstream-scpcb/` is the game source.
- `blitz3d-ng/` is the compiler and runtime.
- `web-shell/` is the HTML page that starts the game.
- `build.py` builds everything into `webgame/`.

## Building

Install CMake, Ninja, Python 3.10 or newer, and the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html), then activate Emscripten in your terminal. Windows also needs Visual Studio 2022 with the C++ and MFC components. On Ubuntu or Debian, Linux needs `git autoconf libtool gettext autopoint gperf clang libxml2-dev zlib1g-dev libwxgtk3.0-gtk3-dev libxrandr-dev libxinerama-dev libxcursor-dev uuid-dev libfontconfig1-dev`.

```
python3 build.py
```

The first Linux build compiles LLVM from source, which takes an hour or more. macOS on Apple Silicon and Windows download a prebuilt LLVM.

The game uses JavaScript Promise Integration and native WebAssembly exceptions, so it needs Chrome or Edge 137 or later with WebGPU.

Rebuilds only do the work that changed. Changing game data only repackages it, which takes a couple of seconds. Changing game or runtime code relinks the game, which takes about 30 seconds.

## Running

```
python3 build.py --serve
```

Then open http://127.0.0.1:8090/ in a browser that supports WebGPU. Chrome and Edge work best. The page asks for a resolution before it loads the game and remembers your last choice. Alt+Enter toggles fullscreen. The 300 MB of game data is stored in the browser after the first load, so later visits skip the download.
