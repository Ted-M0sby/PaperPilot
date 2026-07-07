param(
  [ValidateSet("Debug", "Release")]
  [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

$buildDir = Join-Path $PSScriptRoot "..\build"
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
  throw "cmake is not installed or not in PATH. Use ./scripts/build-gcc.ps1 as fallback."
}

cmake -S (Join-Path $PSScriptRoot "..") -B $buildDir
cmake --build $buildDir --config $Config
ctest --test-dir $buildDir -C $Config --output-on-failure
