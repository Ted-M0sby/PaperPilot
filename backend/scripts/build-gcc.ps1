param()

$ErrorActionPreference = "Stop"

$root = Join-Path $PSScriptRoot ".."
$buildDir = Join-Path $root "build"

if (-not (Test-Path $buildDir)) {
  New-Item -ItemType Directory -Path $buildDir | Out-Null
}

$commonFlags = @("-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Iinclude")
$coreSources = @(
  "src/cli.c",
  "src/config.c",
  "src/document.c",
  "src/index.c",
  "src/query.c",
  "src/logging.c",
  "src/stats.c",
  "src/ai_client.c",
  "src/server.c"
)

Push-Location $root
try {
  gcc @commonFlags @coreSources "src/main.c" "-o" "build/paperpilot.exe" "-lws2_32"
  if ($LASTEXITCODE -ne 0) { throw "Build app failed." }

  gcc @commonFlags @coreSources "tests/test_basic.c" "-o" "build/test_basic.exe" "-lws2_32"
  if ($LASTEXITCODE -ne 0) { throw "Build test failed." }

  & "build/test_basic.exe"
  if ($LASTEXITCODE -ne 0) { throw "Tests failed." }

  Write-Host "Build and tests succeeded with gcc." -ForegroundColor Green
}
finally {
  Pop-Location
}
