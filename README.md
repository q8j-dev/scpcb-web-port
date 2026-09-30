# SCP: Containment Breach (Web)

SCP: Containment Breach running in your browser. No install, just WebAssembly and WebGPU.

**Play it:** https://q8j-dev.github.io/scpcb-web-port/

The game itself is untouched. It's built with [blitz3d-ng](https://github.com/blitz3d-ng/blitz3d-ng), and this repo adds a WebGPU renderer to it (`blitz3d-ng/src/modules/bb/graphics.webgpu`) plus a few fixes.

## What's here

- `upstream-scpcb/` the game source
- `blitz3d-ng/` the compiler and runtime, with the WebGPU backend added
- `web-shell/`, `engine/`, `tools/` the page that boots the game, a small native helper, and the packaging scripts
- `webgame/` build output (not checked in)

## Building

You need CMake, Ninja, Python 3 and the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) (activated). Then fetch the dependencies:

```
python3 tools/fetch_blitz3d_ng_deps.py
```

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

The Python scripts work the same on every platform.

## Running it

```
cd webgame
python3 -m http.server 8090
```

Open `http://127.0.0.1:8090/` in a browser with WebGPU (Chrome or Edge works best; Firefox and Safari vary).

## Status

Playable start to finish: menus, saving, the whole facility, audio and subtitles. Some rendering edge cases still don't match the original engine.
