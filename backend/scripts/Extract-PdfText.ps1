param(
  [Parameter(Mandatory = $true)]
  [string]$InputPath,

  [Parameter(Mandatory = $true)]
  [string]$OutputPath
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $InputPath)) {
  throw "PDF file not found"
}

$root = Split-Path -Parent $PSScriptRoot
$pkgRoot = Join-Path $root 'tmp\pdf'

function Ensure-Package {
  param(
    [string]$Name,
    [string]$Version
  )

  $dir = Join-Path $pkgRoot "$Name.$Version"
  $nupkg = Join-Path $pkgRoot "$Name.$Version.nupkg"
  if (-not (Test-Path -LiteralPath $dir)) {
    New-Item -ItemType Directory -Force -Path $pkgRoot | Out-Null
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri "https://www.nuget.org/api/v2/package/$Name/$Version" -OutFile $nupkg
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($nupkg, $dir)
  }
  return $dir
}

$coreDir = Ensure-Package -Name 'UglyToad.PdfPig.Core' -Version '1.7.0-custom-5'
$fontsDir = Ensure-Package -Name 'UglyToad.PdfPig.Fonts' -Version '1.7.0-custom-5'
$tokensDir = Ensure-Package -Name 'UglyToad.PdfPig.Tokens' -Version '1.7.0-custom-5'
$tokenizationDir = Ensure-Package -Name 'UglyToad.PdfPig.Tokenization' -Version '1.7.0-custom-5'
$pdfPigDir = Ensure-Package -Name 'UglyToad.PdfPig' -Version '1.7.0-custom-5'
$coreDll = Join-Path $coreDir 'lib\netstandard2.0\UglyToad.PdfPig.Core.dll'
$fontsDll = Join-Path $fontsDir 'lib\netstandard2.0\UglyToad.PdfPig.Fonts.dll'
$tokensDll = Join-Path $tokensDir 'lib\netstandard2.0\UglyToad.PdfPig.Tokens.dll'
$tokenizationDll = Join-Path $tokenizationDir 'lib\netstandard2.0\UglyToad.PdfPig.Tokenization.dll'
$pdfPigDll = Join-Path $pdfPigDir 'lib\netstandard2.0\UglyToad.PdfPig.dll'

if (-not (Test-Path -LiteralPath $coreDll) -or
    -not (Test-Path -LiteralPath $fontsDll) -or
    -not (Test-Path -LiteralPath $tokensDll) -or
    -not (Test-Path -LiteralPath $tokenizationDll) -or
    -not (Test-Path -LiteralPath $pdfPigDll)) {
  New-Item -ItemType Directory -Force -Path $pkgRoot | Out-Null
  throw "PDF parser package is incomplete"
}

Add-Type -Path $coreDll
Add-Type -Path $fontsDll
Add-Type -Path $tokensDll
Add-Type -Path $tokenizationDll
Add-Type -Path $pdfPigDll

$builder = New-Object System.Text.StringBuilder
$document = [UglyToad.PdfPig.PdfDocument]::Open($InputPath)
try {
  foreach ($page in $document.GetPages()) {
    [void]$builder.AppendLine($page.Text)
    [void]$builder.AppendLine()
  }
} finally {
  $document.Dispose()
}

[IO.File]::WriteAllText($OutputPath, $builder.ToString(), [Text.Encoding]::UTF8)
