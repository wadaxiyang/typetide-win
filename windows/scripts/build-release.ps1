# Windows 发布构建：Release exe + Inno Setup 安装包 + 便携 zip。
# 版本号读根目录 VERSION 文件（用 scripts/bump-version.ps1 递增）。
# 产物：
#   windows/build/TypeTide-Win.exe
#   windows/build/TypeTide-Win-Setup-{v}.exe     （安装包）
#   windows/build/TypeTide-Win-Windows-{v}.zip   （便携版）
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
$win = Join-Path $root "windows"
$version = (Get-Content (Join-Path $root "VERSION") -Raw).Trim()
Write-Output "building TypeTide-Win for Windows v$version"

# --- 定位安装器工具 ---
$innoRegistryKeys = @(
    "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1",
    "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1",
    "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1"
)
$registeredIscc = $innoRegistryKeys | ForEach-Object {
    if (Test-Path $_) {
        $installLocation = (Get-ItemProperty $_ -Name InstallLocation -ErrorAction SilentlyContinue).InstallLocation
        if ($installLocation) { Join-Path $installLocation "ISCC.exe" }
    }
}
$iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
          "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
          "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") + $registeredIscc |
        Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup 6 not found (winget install JRSoftware.InnoSetup)" }

# --- 构建 exe（版本号已由 bump-version.ps1 写进 CMakeLists/rc/manifest）---
& (Join-Path $PSScriptRoot 'build.ps1')
$exe = Join-Path $win "build\TypeTide-Win.exe"
if (-not (Test-Path $exe)) { throw "TypeTide-Win.exe not produced" }

# 构建后快速自检（Start-Process：exe 起不来（如 SxS 配置损坏）也会抛错，
# 直接用 & 调用时 $LASTEXITCODE 不会更新，坏包会溜进安装器）
$previousDataDir = $env:TYPETIDE_DATA_DIR
try {
    $env:TYPETIDE_DATA_DIR = Join-Path $win 'build\release-selftest'
    $st = Start-Process -FilePath $exe -ArgumentList "--selftest" -WindowStyle Hidden -Wait -PassThru
    if ($st.ExitCode -ne 0) { throw "self-test failed (exit $($st.ExitCode))" }
} finally { $env:TYPETIDE_DATA_DIR = $previousDataDir }

# Exercise the real WinHTTP provider chain against a local fixture before packaging.
$ctestCommand = Get-Command ctest -ErrorAction SilentlyContinue
$ctest = if ($ctestCommand) { $ctestCommand.Source } else {
    & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
        -latest -products * -find '**\ctest.exe' | Select-Object -First 1
}
if (-not $ctest) { throw 'CTest not found' }
& $ctest --test-dir (Join-Path $win 'build') --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Regression tests failed' }

# --- 安装包 ---
& $iscc "/DAppVersion=$version" (Join-Path $win "installer\TypeTide-Win.iss") | Select-Object -Last 3
if ($LASTEXITCODE -ne 0) { throw "ISCC failed" }
$setup = Join-Path $win "build\TypeTide-Win-Setup-$version.exe"

# Exercise the packaged installer against both the old tray shutdown behavior
# and the current app before publishing. CI is disposable; never install on a developer's PC.
if ($env:CI -eq 'true' -and $env:GITHUB_ACTIONS -eq 'true') {
    & (Join-Path $PSScriptRoot 'test-installer-upgrade.ps1') -Setup $setup `
        -LegacyFixture (Join-Path $win 'build\TypeTide-LegacyTrayFixture.exe') -Version $version
}

# --- 便携 zip ---
$zip = Join-Path $win "build\TypeTide-Win-Windows-$version.zip"
Compress-Archive -Path $exe, (Join-Path $root 'LICENSE') -DestinationPath $zip -Force

Write-Output ""
Write-Output "artifacts:"
Get-Item $setup, $zip | ForEach-Object { "  $($_.FullName)  ($([math]::Round($_.Length/1MB,2)) MB)" }
