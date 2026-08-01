#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v cmake >/dev/null 2>&1; then
  echo "CMake 3.25+ is required to run the native skeleton validation." >&2
  exit 127
fi

cd "$repo_root"

cmake --preset test
cmake --build --preset test
ctest --preset smoke --output-on-failure
