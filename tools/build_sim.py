#!/usr/bin/env python3
"""Build the simulator with nothing installed but Python.

Creates a private toolchain (zig as the C/C++ compiler, CMake, Ninja, all pinned), builds,
runs the tests, and copies the executable to build/. Files outside the repo stay in --root,
so OneDrive never syncs the build tree.

    python tools/build_sim.py            build and test
    python tools/build_sim.py --system   use the compiler already on PATH
"""
import argparse
import os
import shutil
import subprocess
import sys
import venv
from pathlib import Path

PINNED = ["ziglang==0.17.1", "cmake==4.4.4", "ninja==1.13.2"]
REPO = Path(__file__).resolve().parent.parent
WIN = os.name == "nt"
EXE = ".exe" if WIN else ""


def run(*cmd, **kw):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def venv_bin(root):
    return root / "venv" / ("Scripts" if WIN else "bin")


def setup_toolchain(root):
    bin_dir = venv_bin(root)
    python = bin_dir / f"python{EXE}"
    if not python.exists():
        venv.create(root / "venv", with_pip=True)
    run(python, "-m", "pip", "install", "-q", "--disable-pip-version-check", *PINNED)

    out = subprocess.run([python, "-c", "import ziglang,os;print(os.path.dirname(ziglang.__file__))"],
                         check=True, capture_output=True, text=True)
    zig = Path(out.stdout.strip()) / f"zig{EXE}"

    wrappers = root / "wrappers"
    wrappers.mkdir(exist_ok=True)
    tools = {}
    for name, sub in (("cc", "cc"), ("cxx", "c++"), ("ar", "ar"), ("ranlib", "ranlib")):
        path = wrappers / (f"zig-{name}.cmd" if WIN else f"zig-{name}")
        if WIN:
            path.write_text(f'@"{zig}" {sub} %*\n')
        else:
            path.write_text(f'#!/bin/sh\nexec "{zig}" {sub} "$@"\n')
            path.chmod(0o755)
        tools[name] = path.as_posix()

    toolchain = root / "zig-toolchain.cmake"
    lines = []
    if WIN:
        lines += ["set(CMAKE_SYSTEM_NAME Windows)", "set(CMAKE_SYSTEM_PROCESSOR x86_64)"]
        tools_target = "x86_64-windows-gnu"
        lines += [f"set(CMAKE_C_COMPILER_TARGET {tools_target})", f"set(CMAKE_CXX_COMPILER_TARGET {tools_target})"]
    lines += [
        f'set(CMAKE_C_COMPILER "{tools["cc"]}")',
        f'set(CMAKE_CXX_COMPILER "{tools["cxx"]}")',
        f'set(CMAKE_AR "{tools["ar"]}" CACHE FILEPATH "")',
        f'set(CMAKE_RANLIB "{tools["ranlib"]}" CACHE FILEPATH "")',
        "set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)",
    ]
    toolchain.write_text("\n".join(lines) + "\n")
    return bin_dir, toolchain


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", type=Path, default=Path.home() / ".danger-build", help="toolchain and build tree")
    ap.add_argument("--system", action="store_true", help="use CMake and the compiler on PATH")
    ap.add_argument("--no-tests", action="store_true")
    args = ap.parse_args()

    root = args.root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    build = root / "sim"

    if args.system:
        cmake, ctest, extra = "cmake", "ctest", []
    else:
        bin_dir, toolchain = setup_toolchain(root)
        cmake, ctest = bin_dir / f"cmake{EXE}", bin_dir / f"ctest{EXE}"
        extra = ["-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={(bin_dir / f'ninja{EXE}').as_posix()}",
                 f"-DCMAKE_TOOLCHAIN_FILE={toolchain.as_posix()}"]

    run(cmake, "-S", REPO / "sim", "-B", build, "-DCMAKE_BUILD_TYPE=Release", *extra)
    run(cmake, "--build", build, "--parallel")
    if not args.no_tests:
        run(ctest, "--test-dir", build, "--output-on-failure")

    dest = REPO / "build"
    dest.mkdir(exist_ok=True)
    shutil.copy2(build / f"danger_sim{EXE}", dest / f"danger_sim{EXE}")
    print(f"\nDone: {dest / ('danger_sim' + EXE)}")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as e:
        sys.exit(e.returncode)
