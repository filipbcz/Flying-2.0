#!/usr/bin/env python3
"""Narrow CI entry point for the Flying native build skeleton."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SKELETON_BUILD_TARGETS = [
    "flying_core_sim",
    "flying_geo_terrain",
    "flying_data_pipeline_cli",
    "flying_skeleton_smoke",
]


def run(args: list[str]) -> None:
    subprocess.run(args, cwd=REPO_ROOT, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate the Flying native build skeleton.")
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Run structure and architecture validation without configuring or building CMake targets.",
    )
    args = parser.parse_args()

    run([sys.executable, "tools/forge_validate.py", "contract"])
    run([sys.executable, "tools/forge_validate.py", "architecture"])
    run([sys.executable, "tools/forge_validate.py", "architecture-boundary-selftest"])

    if not args.skip_build:
        run(["cmake", "--preset", "test"])
        run(["cmake", "--build", "--preset", "test", "--target", *SKELETON_BUILD_TARGETS])

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
