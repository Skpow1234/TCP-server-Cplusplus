#!/usr/bin/env bash
# Build benchmarks (Debug preset) and write Google Benchmark JSON under docs/baselines/generated/.
# Usage: run-performance-baselines.sh [mingw-debug|debug]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PRESET="${1:-mingw-debug}"
OUT="${ROOT}/out/build/${PRESET}"
GEN="${ROOT}/docs/baselines/generated"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"

mkdir -p "${GEN}"

echo "[perf] preset=${PRESET} out=${OUT}"
cmake --preset "${PRESET}" -S "${ROOT}" -B "${OUT}"
cmake --build "${OUT}" --parallel --target benchmark_decoder benchmark_e2e_echo

DEC="${GEN}/decoder-${STAMP}.json"
E2E="${GEN}/e2e-echo-${STAMP}.json"

EXE_DEC="${OUT}/benchmark_decoder"
EXE_E2E="${OUT}/benchmark_e2e_echo"
[[ -f "${OUT}/benchmark_decoder.exe" ]] && EXE_DEC="${OUT}/benchmark_decoder.exe"
[[ -f "${OUT}/benchmark_e2e_echo.exe" ]] && EXE_E2E="${OUT}/benchmark_e2e_echo.exe"

"${EXE_DEC}" --benchmark_min_time=0.12s --benchmark_format=json --benchmark_out="${DEC}"
"${EXE_E2E}" --benchmark_min_time=0.12s --benchmark_format=json --benchmark_out="${E2E}"

echo "[perf] wrote ${DEC}"
echo "[perf] wrote ${E2E}"
echo "[perf] check (Python): python3 cmake/scripts/check_benchmark_regression.py ${ROOT}/docs/baselines/regression-gates.json ${DEC} ${E2E}"
echo "[perf]           (Win): py -3 cmake/scripts/check_benchmark_regression.py ${ROOT}/docs/baselines/regression-gates.json ${DEC} ${E2E}"
