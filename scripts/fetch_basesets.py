#!/usr/bin/env python3
"""Download and extract OpenGFX and OpenSFX base sets for OpenTTD."""

from __future__ import annotations

import argparse
import shutil
import tarfile
import tempfile
import urllib.request
import zipfile
from pathlib import Path

BASESETS = {
    "opengfx": {
        "url": "https://cdn.openttd.org/opengfx-releases/8.0/opengfx-8.0-all.zip",
        "archive": "opengfx-8.0-all.zip",
        "marker": "opengfx.obg",
    },
    "opensfx": {
        "url": "https://cdn.openttd.org/opensfx-releases/1.0.3/opensfx-1.0.3-all.zip",
        "archive": "opensfx-1.0.3-all.zip",
        "marker": "opensfx.obs",
    },
}


def download_file(url: str, dest: Path) -> None:
    """Download url to dest atomically if not already present."""
    if dest.exists() and dest.stat().st_size > 0:
        print(f"Using cached {dest}")
        return

    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".tmp")
    print(f"Downloading {url} -> {dest}")
    req = urllib.request.Request(url, headers={"User-Agent": "OpenTTD-DS-Builder/1.0"})
    with urllib.request.urlopen(req, timeout=60) as resp, open(tmp, "wb") as out:
        shutil.copyfileobj(resp, out)
    tmp.replace(dest)


def has_marker(directory: Path, marker_name: str) -> bool:
    """Check whether marker_name already exists anywhere under directory."""
    return directory.exists() and any(directory.rglob(marker_name))


def extract_archive(archive_path: Path, target_dir: Path) -> None:
    """Extract a baseset zip (and any nested .tar archive inside it) into target_dir."""
    target_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp_str:
        tmp_dir = Path(tmp_str)
        with zipfile.ZipFile(archive_path, "r") as zf:
            zf.extractall(tmp_dir)

        tar_files = list(tmp_dir.rglob("*.tar"))
        if tar_files:
            for tf_path in tar_files:
                with tarfile.open(tf_path, "r:*") as tf:
                    tf.extractall(target_dir)
        else:
            for item in tmp_dir.iterdir():
                dest = target_dir / item.name
                if dest.exists():
                    if dest.is_dir():
                        shutil.rmtree(dest)
                    else:
                        dest.unlink()
                shutil.move(str(item), str(dest))


def main() -> int:
    parser = argparse.ArgumentParser(description="Fetch OpenGFX and OpenSFX base sets.")
    parser.add_argument(
        "--cache-dir",
        type=Path,
        default=Path("baseset-cache"),
        help="Directory to store downloaded archive files.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="Destination baseset directory.",
    )
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    for name, info in BASESETS.items():
        if has_marker(args.output, info["marker"]):
            print(f"{name} already extracted in {args.output}")
            continue
        archive_path = args.cache_dir / info["archive"]
        download_file(info["url"], archive_path)
        extract_archive(archive_path, args.output)
        if not has_marker(args.output, info["marker"]):
            raise RuntimeError(f"Extraction of {name} failed: missing {info['marker']} under {args.output}")
        print(f"Extracted {name} into {args.output}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
