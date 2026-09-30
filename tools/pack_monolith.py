#!/usr/bin/env python3
import glob, os, platform, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME_DIR = os.path.join(ROOT, "upstream-scpcb")
OUT_DIR = os.path.join(tempfile.gettempdir(), "scpcb-web")

PACKAGED_ROOTS = ["Data", "GFX", "SFX", "Loadingscreens"]
PACKAGED_LOOSE = ["defaults.ini"]

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
    return None

def collect():
    entries = []
    for root_name in PACKAGED_ROOTS:
        base = os.path.join(GAME_DIR, root_name)
        if not os.path.isdir(base):
            print(f"warning: missing directory {base}", file=sys.stderr)
            continue
        for dirpath, _dirnames, filenames in os.walk(base):
            for fn in filenames:
                ap = os.path.join(dirpath, fn)
                vp = os.path.relpath(ap, GAME_DIR).replace(os.sep, "/")
                entries.append((vp, ap))
    for loose in PACKAGED_LOOSE:
        ap = os.path.join(GAME_DIR, loose)
        if os.path.isfile(ap):
            entries.append((loose, ap))
        else:
            print(f"warning: missing file {ap}", file=sys.stderr)
    entries.sort()
    return entries

def main():
    entries = collect()
    stage = os.path.join(OUT_DIR, "stage-monolith")
    if os.path.isdir(stage):
        shutil.rmtree(stage)
    total = 0
    for vp, ap in entries:
        dst = os.path.join(stage, vp)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if platform.system() == "Windows":
            try:
                os.link(ap, dst)
            except OSError:
                shutil.copy2(ap, dst)
        else:
            os.symlink(ap, dst)
        total += os.path.getsize(ap)
    print(f"[monolith] {len(entries)} files  {total/1e6:.1f} MB staged at {stage}")

    data = os.path.join(OUT_DIR, "assets.data")
    js = os.path.join(OUT_DIR, "assets.js")
    fp = find_file_packager()
    if not fp:
        sys.exit("file_packager.py not found; make sure emscripten is installed and on PATH (or set EMSDK)")
    cmd = [
        sys.executable, fp, data,
        "--preload", f"{stage}@/",
        f"--js-output={js}",
        "--no-node",
    ]
    print("  $", " ".join(cmd))
    subprocess.check_call(cmd, cwd=GAME_DIR)
    shutil.rmtree(stage)
    print(f"[monolith] wrote {data} ({os.path.getsize(data)/1e6:.1f} MB) and {js}")

if __name__ == "__main__":
    main()
