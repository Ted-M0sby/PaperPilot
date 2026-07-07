param(
  [int]$Port = 8080,
  [string]$ApiKey = ""
)

$ErrorActionPreference = "Stop"

$root = Join-Path $PSScriptRoot ".."
Push-Location $root
try {
  if ($ApiKey) {
    $env:DASHSCOPE_API_KEY = $ApiKey
    Write-Host "DASHSCOPE_API_KEY set for current session." -ForegroundColor Cyan
  }

  if (-not (Test-Path "build/paperpilot.exe")) {
    Write-Host "paperpilot.exe not found, building first..." -ForegroundColor Yellow
    & "./scripts/build-gcc.ps1"
    if ($LASTEXITCODE -ne 0) {
      throw "Build failed"
    }
  }

  Write-Host "Starting PaperPilot Web on http://127.0.0.1:$Port" -ForegroundColor Green
  & "./build/paperpilot.exe" serve "$Port"
}
finally {
  Pop-Location
}
