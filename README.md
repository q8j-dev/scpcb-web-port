# SCP: Containment Breach - Web Port

[![build](https://github.com/q8j-dev/scpcb-web-port/actions/workflows/build.yml/badge.svg)](https://github.com/q8j-dev/scpcb-web-port/actions/workflows/build.yml)

Running SCP: Containment Breach in the browser. No native install, WebAssembly + WebGPU.

Play it live: **https://q8j-dev.github.io/scpcb-web-port/** (rebuilt and redeployed automatically on every push to `main`, see [`.github/workflows/build.yml`](.github/workflows/build.yml)).

The game is written in Blitz3D BASIC, an old Windows-only game-dev language from the early 2000s. Instead of reimplementing the game logic, this compiles the actual unmodified `.bb` source through [blitz3d-ng](https://github.com/blitz3d-ng/blitz3d-ng), a cross-platform LLVM-backed reimplementation of the Blitz3D compiler and runtime that targets emscripten. That project is what makes this possible at all. Without it there's no way to run Blitz3D code outside a 32-bit Windows binary.

This repo adds a WebGPU rendering backend to blitz3d-ng (`blitz3d-ng/src/modules/bb/graphics.webgpu`) plus a handful of fixes. Everything else, meaning the actual game, is the community's and untouched.

## Layout

- `upstream-scpcb/` - the game source (Blitz3D BASIC), checked out from the community-maintained fork.
- `blitz3d-ng/` - the compiler and runtime. Mostly vendored as-is, with the WebGPU backend and a few fixes added here.
- `web-shell/`, `engine/`, `tools/` - the glue. HTML/JS shell that boots the compiled game, a small native helper linked into the build, and asset packaging scripts.
- `webgame/` - build output, not checked in.

## Building

blitz3d-ng's own dependencies (LLVM, zlib, libpng, SDL, etc) aren't checked into this repo, they're pulled in the first time you build. You'll need CMake, Ninja, Python 3, and the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) installed and activated first.

```
python3 tools/fetch_blitz3d_ng_deps.py
```

Two things get built after that: the native `blitzcc` compiler that turns the `.bb` source into the game, and the WebGPU runtime libraries (cross-compiled to wasm32 via emscripten, linked into the final game).

**macOS:**
```
curl -fL -o llvm.zip https://github.com/blitz3d-ng/build-llvm/releases/download/v19.1.5/llvm-19.1.5-macos-14.zip
unzip -q llvm.zip -d blitz3d-ng && rm llvm.zip
cd blitz3d-ng && make host PROJECT_TO_BUILD=blitzcc ENV=release CMAKE_OPTIONS=-DCMAKE_POLICY_VERSION_MINIMUM=3.5 && cd ..

mkdir -p blitz3d-ng/build/webgpu-emscripten-release
cd blitz3d-ng/build/webgpu-emscripten-release
emcmake cmake -G Ninja -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DOUTPUT_PATH=_release_webgpu -DBB_PLATFORM=emscripten -DBB_ENV=release -DBB_WEBGPU=ON -DARCH=webgpu ../..
ninja
cd ../../..

python3 tools/pack_monolith.py
python3 build_game_webgpu.py
```

**Windows** (from an `x64 Native Tools Command Prompt for VS 2022`, with Visual Studio 2022 + MFC installed): grab the `llvm-19.1.5-win64-msvc17.0.zip` build from the [same releases page](https://github.com/blitz3d-ng/build-llvm/releases). It contains an `llvm\` folder at the top level, so extract it into `blitz3d-ng\` directly (you should end up with `blitz3d-ng\llvm\bin`, `blitz3d-ng\llvm\lib`, etc, not `blitz3d-ng\llvm\llvm\...`). Then:
```
cmake -G Ninja -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -Hblitz3d-ng -Bblitz3d-ng\build\win64-release -DARCH=x86_64 -DBB_PLATFORM=win64 -DBB_ENV=release
cmake --build blitz3d-ng\build\win64-release --target blitzcc

mkdir blitz3d-ng\build\webgpu-emscripten-release
cd blitz3d-ng\build\webgpu-emscripten-release
emcmake cmake -G Ninja -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DOUTPUT_PATH=_release_webgpu -DBB_PLATFORM=emscripten -DBB_ENV=release -DBB_WEBGPU=ON -DARCH=webgpu ..\..
ninja
cd ..\..\..

python tools\pack_monolith.py
python build_game_webgpu.py
```

**Linux:** there's no prebuilt LLVM archive for Linux, so `blitzcc` has to be built from source there (`cd blitz3d-ng && make llvm` before `make host PROJECT_TO_BUILD=blitzcc ENV=release CMAKE_OPTIONS=-DCMAKE_POLICY_VERSION_MINIMUM=3.5`, which needs a full C++ toolchain plus, on Ubuntu: `git autoconf libtool gettext autopoint gperf cmake clang libxml2-dev zlib1g-dev libwxgtk3.0-gtk3-dev libxrandr-dev libxinerama-dev libxcursor-dev uuid-dev libfontconfig1-dev`). Slow the first time, same steps otherwise as macOS from `emcmake cmake` onward.

`pack_monolith.py`, `build_game_webgpu.py` and `fetch_blitz3d_ng_deps.py` are plain Python, same commands on every platform. No bash, no WSL, no Git Bash required.

There's also a GitHub Actions workflow (`.github/workflows/build.yml`) that does all of this from a clean checkout and uploads `webgame/` as a build artifact. It runs on macOS runners since that's the platform with a prebuilt LLVM available.

### Running it

```
cd webgame
python3 -m http.server 8090
```

Open `http://127.0.0.1:8090/` in a WebGPU-capable browser (Chrome/Edge stable, Firefox/Safari support varies). `localhost` counts as a secure context so there's no HTTPS setup needed locally.

## Status

Playable end to end. Menus, saving, the full facility, audio, subtitles all work. Still occasionally finding a rendering edge case where the original native engine did something this reimplementation doesn't quite match.

## Why

Because "can this run in a browser" turned out to be a more interesting question than it looked, and nobody else had answered it for this particular game.
