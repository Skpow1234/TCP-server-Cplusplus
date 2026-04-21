@echo off
setlocal enabledelayedexpansion
REM Configure, build, and run CTest for a sanitizer CMake preset (see CMakePresets.json).
REM Usage: run-sanitizer-tests.bat <asan-ubsan^|tsan^|mingw-asan-ubsan^|mingw-tsan>

set "PRESET=%~1"
if "%PRESET%"=="" (
  echo Usage: %~nx0 ^<asan-ubsan^|tsan^|mingw-asan-ubsan^|mingw-tsan^> 1>&2
  exit /b 2
)

set "ROOT=%~dp0..\.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
set "OUT=%ROOT%\out\build\%PRESET%"

echo [sanitizer] preset=%PRESET% binaryDir=%OUT%
cmake --preset %PRESET% -S "%ROOT%" -B "%OUT%" || exit /b 1
cmake --build "%OUT%" --parallel || exit /b 1
ctest --test-dir "%OUT%" --output-on-failure || exit /b 1

endlocal
