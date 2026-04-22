#!/usr/bin/env bash
# Match GitHub Actions: configure with clang++, build, then the CMake clang-tidy target.
# Expects Ninja, cmake, clang++, and clang-tidy on PATH (typical Linux/macOS dev image).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PRESET="${1:-clang-tidy-ci}"
OUT="${ROOT}/out/build/${PRESET}"

echo "[clang-tidy] preset=${PRESET} binaryDir=${OUT}"
cmake --preset "${PRESET}" -S "${ROOT}" -B "${OUT}"
cmake --build "${OUT}" --parallel
cmake --build "${OUT}" --target clang-tidy
