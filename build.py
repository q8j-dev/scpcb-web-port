#!/usr/bin/env python3
"""One-command build for macOS, Windows and Linux.

    python3 build.py            # everything
    python3 build.py --help     # individual steps

Steps: deps -> llvm -> blitzcc -> runtime -> pack -> game
Each step skips itself if its output is already there, so re-running is cheap.
You need CMake, Ninja, Python 3 and an activated Emscripten SDK. On Windows
also Visual Studio 2022 with the C++ and MFC components (the script finds it
for you, no special prompt needed).
"""
import argparse
import glob
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

# Emscripten's wrappers run "python3" from PATH; make sure that's the interpreter
# running this script (the system one is often too old).
os.environ["PATH"] = os.path.dirname(sys.executable) + os.pathsep + os.environ.get("PATH", "")
os.environ.setdefault("EMSDK_PYTHON", sys.executable)

NATIVE_BUILD = os.path.join(NG, "build", f"{ARCH}-{BB_PLATFORM}-release")
WASM_BUILD = os.path.join(NG, "build", "webgpu-emscripten-release")
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
    """Windows: pull in the Visual Studio compiler environment if we aren't
    already inside a developer prompt."""
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


# ---------------------------------------------------------------- steps

def step_deps():
    log("dependency sources")
    run([sys.executable, os.path.join(ROOT, "tools", "fetch_blitz3d_ng_deps.py")])


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

    # No prebuilt archive for Linux (or Intel Macs): build LLVM from source.
    # Slow (30-90 minutes) but only ever happens once.
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
    if os.path.isfile(BLITZCC):
        print("  already built, skipping")
        return
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
        # the pinned zlib stubs out fdopen(), which clashes with current macOS SDK headers
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
    return emcmake


def step_runtime():
    log("WebGPU runtime libraries (wasm)")
    lib = os.path.join(NG, "_release_webgpu", "bin", "wasm32-unknown-emscripten", "lib")
    if os.path.isdir(lib) and os.listdir(lib):
        print("  already built, skipping")
        return
    load_msvc_env()
    emcmake = emscripten_tools()
    need("cmake", "Install CMake.")
    need("ninja", "Install Ninja.")
    os.makedirs(WASM_BUILD, exist_ok=True)
    run([emcmake, "cmake", "-G", "Ninja", CMAKE_COMPAT,
         "-DOUTPUT_PATH=_release_webgpu", "-DBB_PLATFORM=emscripten",
         "-DBB_ENV=release", "-DBB_WEBGPU=ON", "-DARCH=webgpu", NG],
        cwd=WASM_BUILD)
    run(["cmake", "--build", WASM_BUILD, "-j", jobs()])


def step_pack():
    log("packaging game assets")
    emscripten_tools()
    run([sys.executable, os.path.join(ROOT, "tools", "pack_monolith.py")])


def step_game():
    log("compiling and linking the game")
    emscripten_tools()
    if not os.path.isfile(BLITZCC):
        sys.exit("error: blitzcc missing, run: python3 build.py blitzcc")
    run([sys.executable, os.path.join(ROOT, "build_game_webgpu.py")])


def main():
    ap = argparse.ArgumentParser(description="Build SCP: Containment Breach (Web).")
    ap.add_argument("steps", nargs="*", choices=STEPS,
                    metavar="step", help=f"any of: {', '.join(STEPS)} (default: all)")
    ap.add_argument("--serve", action="store_true",
                    help="serve webgame/ on http://127.0.0.1:8090 when done")
    args = ap.parse_args()

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
