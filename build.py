#!/usr/bin/env python3
import argparse
import configparser
import glob
import hashlib
import json
import os
import platform
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

if sys.version_info < (3, 10):
    sys.exit(f"error: Python 3.10+ required (Emscripten needs it), you have {sys.version.split()[0]}")

ROOT = os.path.dirname(os.path.abspath(__file__))
NG = os.path.join(ROOT, "blitz3d-ng")
SYSTEM = platform.system()
MACHINE = platform.machine().lower()
IS_ARM = MACHINE in ("arm64", "aarch64")
ARCH = "arm64" if IS_ARM else "x86_64"
EXE = ".exe" if SYSTEM == "Windows" else ""

LLVM_VERSION = "19.1.5"
LLVM_RELEASE = f"https://github.com/blitz3d-ng/build-llvm/releases/download/v{LLVM_VERSION}"
LLVM_SOURCE = f"https://github.com/llvm/llvm-project/archive/refs/tags/llvmorg-{LLVM_VERSION}.tar.gz"
CMAKE_COMPAT = "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"

STEPS = ["deps", "llvm", "blitzcc", "runtime", "pack", "game"]

if SYSTEM == "Darwin":
    BB_PLATFORM = "macos"
elif SYSTEM == "Windows":
    BB_PLATFORM = "win64"
    ARCH = "x86_64"
elif SYSTEM == "Linux":
    BB_PLATFORM = "linux"
else:
    sys.exit(f"error: unsupported platform {SYSTEM}")

os.environ["PATH"] = os.path.dirname(sys.executable) + os.pathsep + os.environ.get("PATH", "")
os.environ.setdefault("EMSDK_PYTHON", sys.executable)

NATIVE_BUILD = os.path.join(NG, "build", f"{ARCH}-{BB_PLATFORM}-release")
VARIANTS = {
    "jspi": {
        "build": "webgpu-emscripten-jspi-release",
        "out": "_release_webgpu_jspi",
        "cmake": ["-DBB_JSPI=ON"],
        "eh": "-fwasm-exceptions",
        "env": {"SCPCB_JSPI": "1"},
    },
    "compat": {
        "build": "webgpu-emscripten-release",
        "out": "_release_webgpu",
        "cmake": [],
        "eh": "-fexceptions",
        "env": {},
    },
}
SELECTED = list(VARIANTS)
BLITZCC = os.path.join(NG, "_release", "bin", "blitzcc" + EXE)


def log(msg):
    print(f"\n==> {msg}", flush=True)


def run(cmd, **kw):
    print("  $", " ".join(str(c) for c in cmd), flush=True)
    subprocess.check_call([str(c) for c in cmd], **kw)


def need(tool, hint):
    path = shutil.which(tool)
    if not path:
        sys.exit(f"error: '{tool}' not found on PATH. {hint}")
    return path


def jobs():
    return str(os.cpu_count() or 4)


def load_msvc_env():
    if SYSTEM != "Windows" or shutil.which("cl"):
        return
    pf = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = os.path.join(pf, "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not os.path.isfile(vswhere):
        sys.exit("error: Visual Studio 2022 not found. Install it with the "
                 "'Desktop development with C++' workload and the MFC component.")
    install = subprocess.check_output(
        [vswhere, "-latest", "-products", "*",
         "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
         "-property", "installationPath"], text=True).strip()
    if not install:
        sys.exit("error: Visual Studio has no C++ tools installed.")
    vcvars = os.path.join(install, "VC", "Auxiliary", "Build", "vcvars64.bat")
    log("loading Visual Studio environment")
    out = subprocess.check_output(f'cmd /c ""{vcvars}" && set"', text=True, shell=True)
    for line in out.splitlines():
        if "=" in line:
            k, _, v = line.partition("=")
            os.environ[k] = v


def download(url, dest):
    print(f"  downloading {url}", flush=True)
    last = [-1]

    def hook(blocks, size, total):
        if total > 0:
            pct = min(100, blocks * size * 100 // total)
            if pct // 10 != last[0]:
                last[0] = pct // 10
                print(f"    {pct}%", flush=True)

    urllib.request.urlretrieve(url, dest, hook)


def extract_zip_keep_modes(zip_path, dest):
    with zipfile.ZipFile(zip_path) as z:
        for info in z.infolist():
            z.extract(info, dest)
            mode = info.external_attr >> 16
            if mode:
                os.chmod(os.path.join(dest, info.filename), mode)


UPSTREAM_URL = "https://github.com/blitz3d-ng/blitz3d-ng.git"
GIT_ENV = {**os.environ, "GIT_TERMINAL_PROMPT": "0"}
DEPS_MARKERS = [
    os.path.join("zlib", "tree"),
    os.path.join("sdl", "tree"),
    os.path.join("wxwidgets", "tree"),
    os.path.join("freeimage", "src"),
]


def git(cmd, timeout, **kw):
    print("  $", " ".join(cmd), flush=True)
    try:
        subprocess.run(cmd, check=True, timeout=timeout, env=GIT_ENV, **kw)
    except subprocess.TimeoutExpired:
        sys.exit(f"error: timed out after {timeout}s running: {' '.join(cmd)}")


def step_deps():
    log("dependency sources")
    deps_dir = os.path.join(NG, "deps")
    if all(os.path.isdir(os.path.join(deps_dir, p)) and os.listdir(os.path.join(deps_dir, p))
           for p in DEPS_MARKERS):
        print("  already present, skipping")
        return

    with tempfile.TemporaryDirectory() as tmp:
        clone = os.path.join(tmp, "blitz3d-ng")
        git(["git", "clone", "--depth", "1", UPSTREAM_URL, clone], 120)

        cfg = configparser.ConfigParser()
        with open(os.path.join(clone, ".gitmodules"), encoding="utf-8") as f:
            cfg.read_string(f.read().replace('[submodule "', "[").replace('"]', "]"))
        paths = [cfg[sec]["path"] for sec in cfg.sections() if cfg[sec]["path"] != "deps/llvm"]

        for path in paths:
            timeout = 600 if path == "deps/wxwidgets/tree" else 180
            git(["git", "submodule", "update", "--init", "--recursive", "--depth", "1",
                 "--jobs", "4", "--", path], timeout, cwd=clone)

        for name in os.listdir(os.path.join(clone, "deps")):
            if name == "llvm":
                continue
            for sub in ("tree", "src"):
                src = os.path.join(clone, "deps", name, sub)
                if os.path.isdir(src):
                    dst = os.path.join(deps_dir, name, sub)
                    if os.path.isdir(dst):
                        shutil.rmtree(dst)
                    print(f"  {name}/{sub}")
                    shutil.copytree(src, dst)


def llvm_present():
    return os.path.isdir(os.path.join(NG, "llvm", "lib")) and \
        os.path.isdir(os.path.join(NG, "llvm", "include"))


def step_llvm():
    log("LLVM")
    if llvm_present():
        print("  already present, skipping")
        return

    prebuilt = None
    if SYSTEM == "Darwin" and IS_ARM:
        prebuilt = f"llvm-{LLVM_VERSION}-macos-14.zip"
    elif SYSTEM == "Windows":
        prebuilt = f"llvm-{LLVM_VERSION}-win64-msvc17.0.zip"

    if prebuilt:
        with tempfile.TemporaryDirectory() as tmp:
            zp = os.path.join(tmp, prebuilt)
            download(f"{LLVM_RELEASE}/{prebuilt}", zp)
            print("  extracting...", flush=True)
            extract_zip_keep_modes(zp, NG)
        return

    print("  no prebuilt LLVM for this platform, building from source (slow, one time)")
    need("cmake", "Install CMake.")
    need("ninja", "Install Ninja.")
    toolchain = os.path.join(NG, "deps", "llvm", "llvm.cmake")
    with tempfile.TemporaryDirectory() as tmp:
        tarball = os.path.join(tmp, "llvm.tar.gz")
        download(LLVM_SOURCE, tarball)
        print("  extracting source...", flush=True)
        with tarfile.open(tarball) as t:
            t.extractall(tmp)
        src = os.path.join(tmp, f"llvm-project-llvmorg-{LLVM_VERSION}", "llvm")
        build = os.path.join(tmp, "build")
        run(["cmake", "-S", src, "-B", build, "-GNinja",
             f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
             f"-DCMAKE_INSTALL_PREFIX={os.path.join(NG, 'llvm')}"])
        run(["cmake", "--build", build, "-j", jobs()])
        run(["cmake", "--install", build])


def step_blitzcc():
    log("blitzcc (native compiler)")
    if not llvm_present():
        sys.exit("error: LLVM missing, run: python3 build.py llvm")
    load_msvc_env()
    need("cmake", "Install CMake.")
    need("ninja", "Install Ninja.")
    cfg = ["cmake", "-G", "Ninja", "-S", NG, "-B", NATIVE_BUILD, CMAKE_COMPAT,
           f"-DBB_PLATFORM={BB_PLATFORM}", "-DBB_ENV=release", f"-DARCH={ARCH}"]
    if SYSTEM != "Windows":
        cfg.append("-DOUTPUT_PATH=")
    if SYSTEM == "Darwin":
        cfg.append("-DCMAKE_C_FLAGS=-Dfdopen=fdopen")
    run(cfg)
    run(["cmake", "--build", NATIVE_BUILD, "--target", "blitzcc", "-j", jobs()])
    if not os.path.isfile(BLITZCC):
        sys.exit(f"error: build finished but {BLITZCC} was not produced")


def emscripten_tools():
    emcmake = shutil.which("emcmake")
    emcc = shutil.which("emcc")
    if not emcmake or not emcc:
        sys.exit("error: Emscripten not found. Install the emsdk "
                 "(https://emscripten.org/docs/getting_started/downloads.html) "
                 "and activate it in this terminal (emsdk_env), then re-run.")
    return emcmake, emcc


def step_runtime():
    load_msvc_env()
    emcmake = emscripten_tools()[0]
    need("cmake", "Install CMake.")
    need("ninja", "Install Ninja.")
    for name in SELECTED:
        variant = VARIANTS[name]
        log(f"WebGPU runtime libraries (wasm, {name})")
        build_dir = os.path.join(NG, "build", variant["build"])
        os.makedirs(build_dir, exist_ok=True)
        run([emcmake, "cmake", "-G", "Ninja", CMAKE_COMPAT,
             f"-DOUTPUT_PATH={variant['out']}", "-DBB_PLATFORM=emscripten",
             "-DBB_ENV=release", "-DBB_WEBGPU=ON", "-DARCH=webgpu",
             *variant["cmake"], NG],
            cwd=build_dir)
        run(["cmake", "--build", build_dir, "-j", jobs()])


GAME_DIR = os.path.join(ROOT, "upstream-scpcb")
STAGE_DIR = os.path.join(tempfile.gettempdir(), "scpcb-web")
WORK_DIR = os.path.join(tempfile.gettempdir(), "scpcb-web-webgpu")
PACKAGED_DIRS = ["Data", "GFX", "SFX", "Loadingscreens"]
PACKAGED_FILES = ["defaults.ini"]
WEBGPU_SRC = os.path.join(NG, "src", "modules", "bb", "graphics.webgpu")


def find_file_packager():
    emcc = shutil.which("emcc")
    if emcc:
        candidate = os.path.join(os.path.dirname(os.path.realpath(emcc)), "tools", "file_packager.py")
        if os.path.isfile(candidate):
            return candidate
    emsdk = os.environ.get("EMSDK")
    if emsdk:
        hits = glob.glob(os.path.join(emsdk, "upstream", "emscripten", "tools", "file_packager.py"))
        if hits:
            return hits[0]
    for pattern in (
        "/opt/homebrew/Cellar/emscripten/*/libexec/tools/file_packager.py",
        "/usr/local/Cellar/emscripten/*/libexec/tools/file_packager.py",
        "/usr/lib/emscripten/tools/file_packager.py",
        "/usr/share/emscripten/tools/file_packager.py",
    ):
        hits = glob.glob(pattern)
        if hits:
            return hits[0]
    sys.exit("error: file_packager.py not found. Activate the Emscripten SDK.")


def step_pack():
    log("packaging game assets")
    emscripten_tools()
    entries = []
    for d in PACKAGED_DIRS:
        base = os.path.join(GAME_DIR, d)
        if not os.path.isdir(base):
            print(f"  warning: missing directory {base}")
            continue
        for dirpath, _dirs, files in os.walk(base):
            for fn in files:
                ap = os.path.join(dirpath, fn)
                entries.append((os.path.relpath(ap, GAME_DIR).replace(os.sep, "/"), ap))
    for f in PACKAGED_FILES:
        ap = os.path.join(GAME_DIR, f)
        if os.path.isfile(ap):
            entries.append((f, ap))
        else:
            print(f"  warning: missing file {ap}")
    entries.sort()

    stage = os.path.join(STAGE_DIR, "stage-monolith")
    if os.path.isdir(stage):
        shutil.rmtree(stage)
    for vp, ap in entries:
        dst = os.path.join(stage, vp)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if SYSTEM == "Windows":
            try:
                os.link(ap, dst)
            except OSError:
                shutil.copy2(ap, dst)
        else:
            os.symlink(ap, dst)

    run([sys.executable, find_file_packager(), os.path.join(STAGE_DIR, "assets.data"),
         "--preload", f"{stage}@/", f"--js-output={os.path.join(STAGE_DIR, 'assets.js')}",
         "--no-node"], cwd=GAME_DIR)
    shutil.rmtree(stage)


def file_digest(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def link_variant(name, emcc, assets_js):
    variant = VARIANTS[name]
    work = os.path.join(WORK_DIR, name)
    os.makedirs(work, exist_ok=True)

    compat_obj = os.path.join(work, "web_compat.o")
    run([emcc, os.path.join(WEBGPU_SRC, "web_compat.cpp"),
         "-c", "-O2", "-std=c++17", variant["eh"],
         "-I", os.path.join(NG, "src", "modules"),
         "-I", os.path.join(NG, "src", "modules", "bb", "pixmap"),
         "-I", os.path.join(NG, "src"),
         "-I", WEBGPU_SRC,
         "-o", compat_obj])

    debug = os.environ.get("SCPCB_DEBUG", "0") == "1"
    perf_flags = "-O2 -sASSERTIONS=1 --profiling-funcs" if debug else \
        "-O3 -sASSERTIONS=0 -sGL_TRACK_ERRORS=0 -sINITIAL_MEMORY=536870912"

    out_dir = os.path.join(NG, variant["out"])
    env = os.environ.copy()
    env.pop("SCPCB_JSPI", None)
    env.update(variant["env"])
    env["LLVM_ROOT"] = os.path.join(NG, "llvm")
    env["blitzpath"] = out_dir
    env["SCPCB_WEBGPU"] = "1"
    env["SCPCB_LIB_DIR"] = os.path.join(out_dir, "bin", "wasm32-unknown-emscripten", "lib")
    env["SCPCB_COMPAT_OBJ"] = compat_obj
    env["SCPCB_EMCC_EXTRA"] = (
        f"--pre-js {assets_js} -sSTACK_SIZE=16777216 "
        f"-sDEFAULT_TO_CXX -sGROWABLE_ARRAYBUFFERS=0 {perf_flags}")

    out = os.path.join(work, "scpcb")
    run([BLITZCC, "-target", "emscripten", "-o", out, "Main.bb"], cwd=GAME_DIR, env=env)
    return out


def step_game():
    log("compiling and linking the game")
    emcc = emscripten_tools()[1]
    if not os.path.isfile(BLITZCC):
        sys.exit("error: blitzcc missing, run: python3 build.py blitzcc")
    assets_js = os.path.join(STAGE_DIR, "assets.js")
    assets_data = os.path.join(STAGE_DIR, "assets.data")
    if not os.path.isfile(assets_js) or not os.path.isfile(assets_data):
        sys.exit("error: packaged assets missing, run: python3 build.py pack")

    main_bb = os.path.join(GAME_DIR, "Main.bb")
    with open(main_bb, "r", encoding="utf-8", errors="surrogateescape") as f:
        body = f.read()
    if not body.startswith('Include "WebShims.bb"'):
        with open(main_bb, "w", encoding="utf-8", errors="surrogateescape") as f:
            f.write('Include "WebShims.bb"\n' + body)

    deploy = os.path.join(ROOT, "webgame")
    os.makedirs(deploy, exist_ok=True)
    for old in os.listdir(deploy):
        if old in ("scpcb.js", "scpcb.wasm", "assets.manifest.json") or old.startswith("assets.data"):
            os.remove(os.path.join(deploy, old))

    for name in SELECTED:
        log(f"linking the {name} build")
        out = link_variant(name, emcc, assets_js)
        variant_dir = os.path.join(deploy, name)
        os.makedirs(variant_dir, exist_ok=True)
        shutil.copy(out + ".js", variant_dir)
        shutil.copy(out + ".wasm", variant_dir)

    shutil.copy(os.path.join(ROOT, "web-shell", "index.html"), os.path.join(deploy, "index.html"))

    chunk_size = 24 * 1024 * 1024
    parts = []
    with open(assets_data, "rb") as src:
        while True:
            chunk = src.read(chunk_size)
            if not chunk:
                break
            name = f"assets.data.{len(parts):03d}"
            with open(os.path.join(deploy, name), "wb") as f:
                f.write(chunk)
            parts.append({"name": name, "size": len(chunk),
                          "hash": hashlib.sha256(chunk).hexdigest()[:16]})

    variants = [n for n in VARIANTS if os.path.isfile(os.path.join(deploy, n, "scpcb.wasm"))]
    version = hashlib.sha256()
    for part in parts:
        version.update(part["hash"].encode())
    for name in variants:
        for ext in ("js", "wasm"):
            version.update(file_digest(os.path.join(deploy, name, f"scpcb.{ext}")).encode())

    with open(os.path.join(deploy, "assets.manifest.json"), "w") as f:
        json.dump({"version": version.hexdigest()[:16], "variants": variants,
                   "size": os.path.getsize(assets_data), "chunkSize": chunk_size,
                   "parts": parts}, f)


def main():
    ap = argparse.ArgumentParser(description="Build SCP: Containment Breach (Web).")
    ap.add_argument("steps", nargs="*", choices=STEPS,
                    metavar="step", help=f"any of: {', '.join(STEPS)} (default: all)")
    ap.add_argument("--variant", choices=["all", *VARIANTS], default="all",
                    help="build only one of the wasm builds (default: all)")
    ap.add_argument("--serve", action="store_true",
                    help="serve webgame/ on http://127.0.0.1:8090 when done")
    args = ap.parse_args()

    if args.variant != "all":
        SELECTED[:] = [args.variant]
    steps = args.steps or STEPS
    print(f"platform: {SYSTEM} {ARCH}")
    for name in STEPS:
        if name in steps:
            globals()[f"step_{name}"]()

    if "game" in steps:
        print(f"\ndone. output is in {os.path.join(ROOT, 'webgame')}")
    if args.serve:
        os.chdir(os.path.join(ROOT, "webgame"))
        run([sys.executable, "-m", "http.server", "8090", "--bind", "127.0.0.1"])


if __name__ == "__main__":
    main()
