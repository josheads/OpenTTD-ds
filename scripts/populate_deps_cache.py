#!/usr/bin/env python3
"""Shallow-clone pinned CMake FetchContent dependency sources into a cache directory."""

from __future__ import annotations

import argparse
import os
import shutil
import stat
import subprocess
from pathlib import Path

DEPENDENCIES = (
    {
        "name": "SDL3",
        "repo": "https://github.com/libsdl-org/SDL.git",
        "tag": "release-3.4.20",
        "platforms": ("android", "windows"),
    },
    {
        "name": "zlib",
        "repo": "https://github.com/madler/zlib.git",
        "tag": "v1.3.1",
        "platforms": ("windows",),
    },
    {
        "name": "xz",
        "repo": "https://github.com/tukaani-project/xz.git",
        "tag": "v5.6.3",
        "platforms": ("android", "windows"),
    },
    {
        "name": "libpng",
        "repo": "https://github.com/pnggroup/libpng.git",
        "tag": "v1.6.44",
        "platforms": ("android", "windows"),
    },
    {
        "name": "freetype",
        "repo": "https://github.com/freetype/freetype.git",
        "tag": "VER-2-13-3",
        "platforms": ("android",),
    },
)


def _remove_readonly(func, path, _exc_info) -> None:
    os.chmod(path, stat.S_IWRITE)
    func(path)


def remove_tree(path: Path) -> None:
    """Remove an incomplete cache directory, including read-only .git files."""
    if not path.exists():
        return
    try:
        shutil.rmtree(path, onexc=lambda f, p, e: _remove_readonly(f, p, e))
    except TypeError:
        shutil.rmtree(path, onerror=_remove_readonly)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Populate cached FetchContent source trees for CMake builds."
    )
    parser.add_argument(
        "--cache-dir",
        type=Path,
        default=Path("deps-cache"),
        help="Destination directory for cloned dependency source trees.",
    )
    parser.add_argument(
        "--platform",
        choices=("android", "windows", "all"),
        default="all",
        help="Target platform whose dependencies should be populated.",
    )
    args = parser.parse_args()

    cache_dir = args.cache_dir
    cache_dir.mkdir(parents=True, exist_ok=True)

    for dep in DEPENDENCIES:
        if args.platform != "all" and args.platform not in dep["platforms"]:
            continue

        dest = cache_dir / dep["name"]
        if (dest / "CMakeLists.txt").is_file():
            print(f"[cache hit] {dep['name']} ({dep['tag']})")
            continue

        remove_tree(dest)
        print(f"[cache miss] Cloning {dep['name']} {dep['tag']} -> {dest}")
        subprocess.run(
            [
                "git",
                "clone",
                "--depth",
                "1",
                "--branch",
                dep["tag"],
                dep["repo"],
                str(dest),
            ],
            check=True,
        )
        if not (dest / "CMakeLists.txt").is_file():
            raise RuntimeError(f"Cloned {dep['name']} at {dest} is missing CMakeLists.txt")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
