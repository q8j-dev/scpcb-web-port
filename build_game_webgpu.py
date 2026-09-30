#!/usr/bin/env python3
"""Link the compiled game and deploy it to webgame/. Cross-platform
replacement for build-game-webgpu.sh - works the same on macOS, Linux
and Windows. Requires the native blitzcc build and the
webgpu-emscripten-release libraries to already exist (see README.md),
and the packaged assets from tools/pack_monolith.py."""
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.abspath(__file__))
STAGE_DIR = os.path.join(tempfile.gettempdir(), "scpcb-web")
WORK_DIR = os.path.join(tempfile.gettempdir(), "scpcb-web-webgpu")

EXE_SUFFIX = ".exe" if platform.system() == "Windows" else ""


def main():
    assets_js = os.path.join(STAGE_DIR, "assets.js")
    assets_data = os.path.join(STAGE_DIR, "assets.data")
    if not os.path.isfile(assets_js) or not os.path.isfile(assets_data):
        sys.exit(f"error: {assets_js} / {assets_data} missing - run "
                 f"tools/pack_monolith.py first")

    os.makedirs(WORK_DIR, exist_ok=True)

    blitzcc = os.path.join(ROOT, "blitz3d-ng", "_release", "bin", "blitzcc" + EXE_SUFFIX)
    if not os.path.isfile(blitzcc):
        sys.exit(f"error: {blitzcc} not found - build blitzcc first (see README.md)")

    compat_obj = os.path.join(WORK_DIR, "web_compat.o")
    subprocess.check_call([
        "emcc", os.path.join(ROOT, "engine", "web_compat.cpp"),
        "-c", "-O2", "-std=c++17", "-fexceptions",
        "-I", os.path.join(ROOT, "blitz3d-ng", "src", "modules"),
        "-I", os.path.join(ROOT, "blitz3d-ng", "src", "modules", "bb", "pixmap"),
        "-I", os.path.join(ROOT, "blitz3d-ng", "src"),
        "-I", os.path.join(ROOT, "engine"),
        "-o", compat_obj,
    ])

    main_bb = os.path.join(ROOT, "upstream-scpcb", "Main.bb")
    with open(main_bb, "r", encoding="utf-8", errors="surrogateescape") as f:
        first_line = f.readline()
    if 'Include "WebShims.bb"' not in first_line:
        with open(main_bb, "r", encoding="utf-8", errors="surrogateescape") as f:
            body = f.read()
        with open(main_bb, "w", encoding="utf-8", errors="surrogateescape") as f:
            f.write('Include "WebShims.bb"\n' + body)

    debug = os.environ.get("SCPCB_DEBUG", "0") == "1"
    perf_flags = "-O2 -sASSERTIONS=1" if debug else \
        "-O3 -sASSERTIONS=0 -sGL_TRACK_ERRORS=0 -sINITIAL_MEMORY=536870912"

    env = os.environ.copy()
    env["LLVM_ROOT"] = os.path.join(ROOT, "blitz3d-ng", "llvm")
    env["blitzpath"] = os.path.join(ROOT, "blitz3d-ng", "_release_webgpu")
    env["SCPCB_WEBGPU"] = "1"
    env["SCPCB_LIB_DIR"] = os.path.join(
        ROOT, "blitz3d-ng", "_release_webgpu", "bin", "wasm32-unknown-emscripten", "lib")
    env["SCPCB_COMPAT_OBJ"] = compat_obj
    env["SCPCB_EMCC_EXTRA"] = (
        f"--pre-js {assets_js} -sSTACK_SIZE=16777216 -sASYNCIFY_STACK_SIZE=1048576 "
        f"-sGROWABLE_ARRAYBUFFERS=0 --profiling-funcs {perf_flags}"
    )

    out = os.path.join(WORK_DIR, "scpcb")
    print("[webgpu] linking (reusing packaged assets)...")
    subprocess.check_call(
        [blitzcc, "-target", "emscripten", "-o", out, "Main.bb"],
        cwd=os.path.join(ROOT, "upstream-scpcb"), env=env)

    deploy = os.path.join(ROOT, "webgame")
    os.makedirs(deploy, exist_ok=True)
    for stale in ("scpcb.js", "scpcb.wasm"):
        p = os.path.join(deploy, stale)
        if os.path.exists(p):
            os.remove(p)
    shutil.copy(out + ".js", deploy)
    shutil.copy(out + ".wasm", deploy)

    shell_html = os.path.join(ROOT, "web-shell", "index.html")
    dst_html = os.path.join(deploy, "index.html")
    shutil.copy(shell_html if os.path.isfile(shell_html) else out + ".html", dst_html)

    for old in os.listdir(deploy):
        if old.startswith("assets.data") or old == "assets.manifest.json":
            os.remove(os.path.join(deploy, old))

    chunk_size = 24 * 1024 * 1024
    total = os.path.getsize(assets_data)
    parts = []
    with open(assets_data, "rb") as src:
        i = 0
        while True:
            chunk = src.read(chunk_size)
            if not chunk:
                break
            name = f"assets.data.{i:03d}"
            with open(os.path.join(deploy, name), "wb") as out_chunk:
                out_chunk.write(chunk)
            parts.append(name)
            i += 1

    with open(os.path.join(deploy, "assets.manifest.json"), "w") as f:
        json.dump({"size": total, "chunkSize": chunk_size, "parts": parts}, f)

    print(f"[webgpu] deployed to {deploy} ({len(parts)} asset chunks, {total} bytes)")


if __name__ == "__main__":
    main()
