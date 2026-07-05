#!/usr/bin/env python3
"""Fetch blitz3d-ng's vendored third-party dependencies.

blitz3d-ng/deps is excluded from this repo (huge, and each library has its
own upstream repo). Rather than cloning each dependency's latest branch
ourselves (which can drift out of sync with what blitz3d-ng's own build
expects - two libraries that each work fine alone don't always still agree
with each other), this clones the actual blitz3d-ng repo fresh, with its
real pinned submodule commits, and copies just its deps/ tree into place.
Our own changes live under blitz3d-ng/src and aren't touched.
"""
import configparser
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BLITZ3D_NG = os.path.join(ROOT, "blitz3d-ng")
UPSTREAM_URL = "https://github.com/blitz3d-ng/blitz3d-ng.git"
CLONE_TIMEOUT = 120       # the blitz3d-ng repo itself, no submodules yet
PER_DEP_TIMEOUT = 180     # each dependency gets its own budget and its own clear error
WXWIDGETS_TIMEOUT = 600   # wxwidgets alone drags in ~10 more nested submodules

# Disables git's interactive credential/host-key prompts. Without a TTY (as
# in CI) a prompt just hangs forever instead of failing - this turns that
# into an immediate, diagnosable error.
GIT_ENV = {**os.environ, "GIT_TERMINAL_PROMPT": "0"}

# A handful of real submodule paths (from blitz3d-ng/.gitmodules) to check
# for before deciding the fetch already happened. Deliberately not a scan of
# every deps/*/ subfolder: several non-submodule deps (glew, for one) have
# their own ordinary "src" folder that isn't a sign of anything.
MARKER_PATHS = [
    os.path.join("zlib", "tree"),
    os.path.join("sdl", "tree"),
    os.path.join("wxwidgets", "tree"),
    os.path.join("freeimage", "src"),
]


def already_fetched(deps_dir):
    return all(
        os.path.isdir(os.path.join(deps_dir, p)) and os.listdir(os.path.join(deps_dir, p))
        for p in MARKER_PATHS
    )


def parse_gitmodules(path):
    cfg = configparser.ConfigParser()
    with open(path, "r", encoding="utf-8") as f:
        text = f.read().replace('[submodule "', "[").replace('"]', "]")
    cfg.read_string(text)
    return [cfg[section]["path"] for section in cfg.sections()]


def run(cmd, timeout, **kw):
    print(f"[deps] $ {' '.join(cmd)}", flush=True)
    kw.setdefault("env", GIT_ENV)
    try:
        subprocess.run(cmd, check=True, timeout=timeout, **kw)
    except subprocess.TimeoutExpired:
        sys.exit(f"[deps] timed out after {timeout}s running: {' '.join(cmd)}")


def main():
    deps_dir = os.path.join(BLITZ3D_NG, "deps")
    if os.path.isdir(deps_dir) and already_fetched(deps_dir):
        print("[deps] dependency sources already present, skipping fetch")
        return

    with tempfile.TemporaryDirectory() as tmp:
        clone_dir = os.path.join(tmp, "blitz3d-ng")
        print(f"[deps] cloning {UPSTREAM_URL} ...")
        run(["git", "clone", "--depth", "1", UPSTREAM_URL, clone_dir], CLONE_TIMEOUT)

        submodule_paths = parse_gitmodules(os.path.join(clone_dir, ".gitmodules"))
        # deps/llvm is build-llvm's own source, only needed to build LLVM
        # from scratch - we use the prebuilt archive instead.
        submodule_paths = [p for p in submodule_paths if p != "deps/llvm"]

        print(f"[deps] fetching {len(submodule_paths)} submodules one at a time (wxwidgets drags in ~10 more of its own) ...")
        for path in submodule_paths:
            timeout = WXWIDGETS_TIMEOUT if path == "deps/wxwidgets/tree" else PER_DEP_TIMEOUT
            run(
                ["git", "submodule", "update", "--init", "--recursive", "--depth", "1",
                 "--jobs", "4", "--", path],
                timeout,
                cwd=clone_dir,
            )

        src_deps = os.path.join(clone_dir, "deps")
        for name in os.listdir(src_deps):
            if name == "llvm":
                continue
            src = os.path.join(src_deps, name)
            dst = os.path.join(deps_dir, name)
            for sub in ("tree", "src"):
                sub_src = os.path.join(src, sub)
                if os.path.isdir(sub_src):
                    sub_dst = os.path.join(dst, sub)
                    if os.path.isdir(sub_dst):
                        shutil.rmtree(sub_dst)
                    print(f"[deps] {name}/{sub}")
                    shutil.copytree(sub_src, sub_dst)


if __name__ == "__main__":
    main()
