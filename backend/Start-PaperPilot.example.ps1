$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Exe = Join-Path $Root 'build\Release\paperpilot.exe'
$LogDir = Join-Path $Root 'logs'
$TempDir = Join-Path $Root 'tmp'
$DataDir = Join-Path $Root 'data'

if (-not (Test-Path -LiteralPath $Exe)) {
  throw "PaperPilot executable not found: $Exe"
}

foreach ($Dir in @($LogDir, $TempDir, $DataDir)) {
  if (-not (Test-Path -LiteralPath $Dir)) {
    New-Item -ItemType Directory -Path $Dir | Out-Null
  }
}

if (-not $env:DASHSCOPE_API_KEY) {
  throw "Please set DASHSCOPE_API_KEY before starting PaperPilot."
}

$env:TEMP = $TempDir
$env:TMP = $TempDir

Set-Location -LiteralPath $Root
& $Exe serve 3000 *> (Join-Path $LogDir 'paperpilot.log')

