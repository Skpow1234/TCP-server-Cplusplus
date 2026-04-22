#!/usr/bin/env python3
"""Check Google Benchmark JSON (schema v1) against docs/baselines/regression-gates.json.

Usage:
  python3 cmake/scripts/check_benchmark_regression.py docs/baselines/regression-gates.json dec.json [e2e.json ...]

Exit code 0 if every gate name present in the merged benchmark exports has cpu_time <= max_cpu_time_ns
(after unit conversion).
"""
from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


def cpu_time_to_ns(entry: dict[str, Any]) -> float:
    t = float(entry["cpu_time"])
    unit = entry.get("time_unit", "ns")
    mult = {"ns": 1.0, "us": 1e3, "ms": 1e6, "s": 1e9}.get(unit)
    if mult is None:
        raise ValueError(f"unsupported time_unit: {unit!r}")
    return t * mult


def load_gates(path: Path) -> dict[str, float]:
    data = json.loads(path.read_text(encoding="utf-8"))
    out: dict[str, float] = {}
    for g in data.get("gates", []):
        name = g["name"]
        out[name] = float(g["max_cpu_time_ns"])
    return out


def load_current_map(path: Path) -> dict[str, float]:
    data = json.loads(path.read_text(encoding="utf-8"))
    m: dict[str, float] = {}
    for b in data.get("benchmarks", []):
        if b.get("run_type") != "iteration":
            continue
        name = b.get("name")
        if not name:
            continue
        m[name] = cpu_time_to_ns(b)
    return m


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    gates_path = Path(sys.argv[1])
    gates = load_gates(gates_path)
    cur: dict[str, float] = {}
    for p in sys.argv[2:]:
        cur.update(load_current_map(Path(p)))

    failed = False
    for name, cap_ns in gates.items():
        if name not in cur:
            print(f"missing benchmark in current file: {name}", file=sys.stderr)
            failed = True
            continue
        got = cur[name]
        if got > cap_ns:
            print(f"REGRESSION {name}: cpu {got:.0f} ns > cap {cap_ns:.0f} ns", file=sys.stderr)
            failed = True
        else:
            print(f"ok {name}: cpu {got:.0f} ns <= {cap_ns:.0f} ns")

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
