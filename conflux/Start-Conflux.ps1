param(
  [string]$AdminToken = $env:NEXUS_ADMIN_TOKEN
)

$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Exe = Join-Path $Root 'build\conflux.exe'
$RoutesFile = Join-Path $Root 'configs\routes.yaml'

if (-not (Test-Path -LiteralPath $Exe)) {
  throw "Conflux executable not found: $Exe"
}

if (-not (Test-Path -LiteralPath $RoutesFile)) {
  throw "Conflux routes file not found: $RoutesFile"
}

$env:NEXUS_ROUTES_FILE = $RoutesFile
$env:NEXUS_ADMIN_TOKEN = $AdminToken
$env:NEXUS_RATELIMIT_ENABLE = 'true'
$env:NEXUS_LB = 'round_robin'
$env:NEXUS_PROXY_TIMEOUT_MS = '90000'

Set-Location -LiteralPath $Root
& $Exe
