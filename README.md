# tcp_server_cpp

C++23 TCP echo server core: length-prefixed framing, worker pool, graceful shutdown, tests, benchmarks, and tooling.

## Prerequisites

| Tool | Notes |
|------|--------|
| **CMake** | 3.25 or newer |
| **Ninja** | Used by all presets in `CMakePresets.json` |
| **C++ compiler** | **C++23**. Presets use **g++** (MinGW on Windows) or **clang++** (Linux / clang-tidy preset) |
| **Git** | For FetchContent (Catch2, Google Benchmark) |

**Windows (MinGW)**  
Install a MinGW-w64 toolchain with `g++` on `PATH` (for example via MSYS2 or Scoop). The static library links **`ws2_32`**.

**Linux**  
Install `build-essential`, `ninja-build`, `cmake`, and (for sanitizer presets) a compiler with sanitizer runtimes.

**Optional**  
`clang-tidy`, `clang++` (for `clang-tidy-ci` and libFuzzer fuzz preset), Python 3 (for `cmake/scripts/check_benchmark_regression.py`).

---

## Quick start (configure, build, test)

From the repository root:

```bash
cmake --preset mingw-debug
cmake --build --preset mingw-debug
ctest --preset mingw-debug --output-on-failure
```

On Linux without MinGW, use **`debug`** instead of **`mingw-debug`**:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

Build outputs go under **`out/build/<preset-name>/`**.

---

## CMake presets (`CMakePresets.json`)

| Preset | Role |
|--------|------|
| **`mingw-debug`** | Default Windows dev: **Debug**, **g++**, tests + benchmarks |
| **`debug`** | Non-MinGW: **Debug**, default compiler |
| **`mingw-release`** / **`release`** | **Release** optimizations |
| **`mingw-asan-ubsan`** / **`asan-ubsan`** | **ASan + UBSan**, RelWithDebInfo |
| **`mingw-tsan`** / **`tsan`** | **ThreadSanitizer**, RelWithDebInfo (requires a TSan-capable toolchain; many MinGW builds lack `libtsan`) |
| **`clang-tidy-ci`** | **clang++** Debug tree for **clang-tidy** (matches CI) |
| **`fuzz-standalone`** | Debug + **`TCP_SERVER_BUILD_FUZZ`**: `fuzz_decoder` file/stdin replay |
| **`fuzz-libfuzzer`** | Clang + fuzz + **`TCP_SERVER_FUZZ_LIBFUZZER`** (not MSVC) |

**CTest** presets exist for **`debug`**, **`mingw-debug`**, and the four sanitizer presets above.

---

## Main CMake options

| Option | Default | Meaning |
|--------|---------|---------|
| `TCP_SERVER_BUILD_TESTS` | ON | Catch2 unit + integration tests |
| `TCP_SERVER_BUILD_BENCHMARKS` | ON | `benchmark_decoder`, `benchmark_e2e_echo` |
| `TCP_SERVER_ENABLE_CLANG_TIDY` | ON | `clang-tidy` custom target when `clang-tidy` is found |
| `TCP_SERVER_ADD_SANITIZER_DRIVER_TARGETS` | ON | `sanitizer-*-test` aggregate targets (see below) |
| `TCP_SERVER_BUILD_FUZZ` | OFF | Build **`fuzz_decoder`** |
| `TCP_SERVER_FUZZ_LIBFUZZER` | OFF | Link fuzzer with **`-fsanitize=fuzzer`** (CMakeDependentOption; requires `TCP_SERVER_BUILD_FUZZ`) |

---

## Targets and binaries

| Target / executable | Description |
|---------------------|-------------|
| **`tcp_server_core`** | Static library |
| **`tcp_server`** | Executable (minimal `main` today) |
| **`unit_config_tests`** | Unit tests + `ctest` entry |
| **`integration_*_tests`** | Integration executables |
| **`benchmark_decoder`** | Decoder / stream micro-benchmarks |
| **`benchmark_e2e_echo`** | In-process echo TCP latency (Google Benchmark) |
| **`fuzz_decoder`** | Decoder harness (standalone or libFuzzer; see `fuzz/decoder_fuzz.cpp`) |
| **`clang-tidy`** | Static analysis over first-party sources (on **MinGW + g++** the target **prints a skip**; use **`clang-tidy-ci`** or CI) |
| **`sanitizer-asan-ubsan-test`**, **`sanitizer-tsan-test`**, **`sanitizer-mingw-asan-ubsan-test`**, **`sanitizer-mingw-tsan-test`** | Configure + build + `ctest` for the matching preset |

---

## Scripts (`cmake/scripts/`)

| Script | Purpose |
|--------|---------|
| **`run-sanitizer-tests.sh`** / **`run-sanitizer-tests.bat`** | `cmake --preset <preset>`, build, `ctest` for `asan-ubsan`, `tsan`, `mingw-asan-ubsan`, or `mingw-tsan` |
| **`run-clang-tidy.sh`** | Same flow as GitHub Actions: `clang-tidy-ci` preset + **`clang-tidy`** target |
| **`run-decoder-fuzz.sh`** | Build **`fuzz-standalone`** and run `fuzz_decoder` on a file or `-` (stdin) |
| **`run-performance-baselines.sh`** | Build benchmarks and write JSON under **`docs/baselines/generated/`** |
| **`check_benchmark_regression.py`** | Compare benchmark JSON exports to **`docs/baselines/regression-gates.json`** |

Examples:

```bash
bash cmake/scripts/run-sanitizer-tests.sh mingw-asan-ubsan
bash cmake/scripts/run-clang-tidy.sh
bash cmake/scripts/run-decoder-fuzz.sh /path/to/corpus.bin
bash cmake/scripts/run-performance-baselines.sh mingw-debug
py -3 cmake/scripts/check_benchmark_regression.py docs/baselines/regression-gates.json docs/baselines/generated/decoder-*.json docs/baselines/generated/e2e-echo-*.json
```

---

## CI

- **`.github/workflows/clang-tidy.yml`** — Ubuntu: **`clang-tidy-ci`** preset, build, then **`clang-tidy`** target.

---

## Documentation

| Document | Content |
|----------|---------|
| **`docs/INVARIANTS.md`** | Runtime `assert` contracts (lifecycle, buffers, pools, codec) |

---

## Troubleshooting

- **`ctest` fails to find DLLs on Windows** — CMake sets `PATH` for tests to the compiler’s `bin` directory for MinGW presets.
- **TSan preset fails to link on MinGW** — install a toolchain that ships **`libtsan`**, or run TSan on Linux / use WSL.
- **`clang-tidy` skipped locally** — expected on **Windows + g++**; use **`clang-tidy-ci`** with **`clang++`**, **`run-clang-tidy.sh`**, or rely on GitHub Actions.
