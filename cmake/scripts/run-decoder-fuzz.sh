#!/usr/bin/env bash
# Build fuzz_decoder (standalone replay) via preset fuzz-standalone and run on one input.
# Usage: run-decoder-fuzz.sh <file|- for stdin>
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${ROOT}/out/build/fuzz-standalone"
INPUT="${1:?usage: $0 <file|- >}"

echo "[fuzz] preset=fuzz-standalone binaryDir=${OUT}"
cmake --preset fuzz-standalone -S "${ROOT}" -B "${OUT}"
cmake --build "${OUT}" --parallel --target fuzz_decoder

EXE="${OUT}/fuzz_decoder"
if [[ -f "${OUT}/fuzz_decoder.exe" ]]; then
  EXE="${OUT}/fuzz_decoder.exe"
fi

exec "${EXE}" "${INPUT}"
