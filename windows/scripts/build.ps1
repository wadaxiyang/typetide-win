# Build the native Windows executable without requiring installer tooling.
$ErrorActionPreference = 'Stop'
$win = Split-Path -Parent $PSScriptRoot
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer not found' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio with the Desktop development with C++ workload is required' }
$devcmd = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
$eap = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'
    # Re-detect the compiler under UTF-8 so localized /showIncludes prefixes
    # are recorded correctly, including caches created under a legacy codepage.
    cmd /c "`"$devcmd`" -arch=amd64 -no_logo && chcp 65001 >nul && cd /d `"$win`" && cmake --fresh -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build" 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw 'CMake build failed' }
} finally { $ErrorActionPreference = $eap }
$exe = Join-Path $win 'build\TypeTide-Win.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw 'TypeTide-Win.exe not produced' }
Write-Output "Built: $exe"
