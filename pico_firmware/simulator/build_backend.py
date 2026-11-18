#!/usr/bin/env python3
"""Build the native Pico simulator backend from the production portable core."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


SIMULATOR_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SIMULATOR_DIR.parent
CORE_INCLUDE = PROJECT_DIR / "lib" / "pico_core" / "include"
CORE_SOURCE_DIR = PROJECT_DIR / "lib" / "pico_core" / "src"
BACKEND_SOURCE = SIMULATOR_DIR / "backend" / "main.cpp"
BUILD_DIR = SIMULATOR_DIR / "build"
BACKEND_BINARY = BUILD_DIR / (
    "pico_sim_backend.exe" if os.name == "nt" else "pico_sim_backend"
)


def source_files() -> list[Path]:
    """Discover every portable core implementation automatically."""
    return sorted(CORE_SOURCE_DIR.glob("*.cpp")) + [BACKEND_SOURCE]


def dependency_files() -> list[Path]:
    return source_files() + sorted(CORE_INCLUDE.rglob("*.hpp"))


def needs_rebuild(output: Path = BACKEND_BINARY) -> bool:
    if not output.exists():
        return True
    output_time = output.stat().st_mtime_ns
    return any(path.stat().st_mtime_ns > output_time for path in dependency_files())


def find_compiler() -> str:
    requested = os.environ.get("PICO_SIM_CXX") or os.environ.get("CXX")
    if requested:
        resolved = shutil.which(requested)
        if resolved:
            return resolved
        raise RuntimeError(f"Requested C++ compiler was not found: {requested}")

    candidates = (
        ("cl", "clang++", "g++", "c++")
        if os.name == "nt"
        else ("c++", "g++", "clang++")
    )
    for candidate in candidates:
        resolved = shutil.which(candidate)
        if resolved:
            return resolved
    raise RuntimeError(
        "No desktop C++ compiler was found. Install Visual Studio Build Tools "
        "on Windows, Xcode Command Line Tools on macOS, or g++/clang++ on Linux."
    )


def build(*, force: bool = False) -> Path:
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    if not force and not needs_rebuild():
        return BACKEND_BINARY

    compiler = find_compiler()
    name = Path(compiler).name.lower()
    sources = [str(path) for path in source_files()]
    if name in {"cl", "cl.exe"}:
        command = [
            compiler,
            "/nologo",
            "/std:c++17",
            "/EHsc",
            "/W4",
            f"/I{CORE_INCLUDE}",
            *sources,
            f"/Fe{BACKEND_BINARY}",
        ]
    else:
        command = [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Werror",
            f"-I{CORE_INCLUDE}",
            *sources,
            "-o",
            str(BACKEND_BINARY),
        ]

    print("Building Pico simulator backend from lib/pico_core...")
    subprocess.run(command, cwd=PROJECT_DIR, check=True)
    return BACKEND_BINARY


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--force", action="store_true", help="rebuild even if current")
    args = parser.parse_args()
    try:
        print(build(force=args.force))
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Simulator build failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
