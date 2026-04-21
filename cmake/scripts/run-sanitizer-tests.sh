#!/usr/bin/env bash
# Configure, build, and run CTest for a sanitizer CMake preset (see CMakePresets.json).
# Usage: run-sanitizer-tests.sh <asan-ubsan|tsan|mingw-asan-ubsan|mingw-tsan>
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PRESET="${1:-}"

usage() {
  echo "Usage: $0 <asan-ubsan|tsan|mingw-asan-ubsan|mingw-tsan>" >&2
  exit 2
}

case "${PRESET}" in
  asan-ubsan|tsan|mingw-asan-ubsan|mingw-tsan) ;;
  *) usage ;;
esac

OUT="${ROOT}/out/build/${PRESET}"

echo "[sanitizer] preset=${PRESET} binaryDir=${OUT}"
cmake --preset "${PRESET}" -S "${ROOT}" -B "${OUT}"
cmake --build "${OUT}" --parallel
ctest --test-dir "${OUT}" --output-on-failure
