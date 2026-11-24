#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

cmake -B build -S .
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure "$@"
