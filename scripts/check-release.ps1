param([string]$Tag, [string]$Executable)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$version = (Get-Content (Join-Path $root 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid VERSION' }
if ($Tag -and $Tag -ne "v$version") { throw "Tag $Tag does not match VERSION $version" }
$cmake = Get-Content (Join-Path $root 'windows/CMakeLists.txt') -Raw
if ($cmake -notmatch "project\(TypeTide-Win VERSION $([regex]::Escape($version)) ") {
    throw 'CMake version does not match VERSION'
}
$resource = Get-Content (Join-Path $root 'windows/src/TypeTide-Win.rc') -Raw
foreach ($key in 'FileVersion', 'ProductVersion') {
    if ($resource -notmatch "VALUE `"$key`", `"$([regex]::Escape($version))`"") {
        throw "Resource $key does not match VERSION"
    }
}
[xml]$manifest = Get-Content (Join-Path $root 'windows/src/TypeTide-Win.manifest') -Raw
if ($manifest.assembly.assemblyIdentity.version -ne "$version.0") { throw 'Manifest version mismatch' }
& (Join-Path $PSScriptRoot 'check-ui-quality.ps1')
$updater = Get-Content (Join-Path $root 'windows/src/UpdateChecker.cpp') -Raw
foreach ($url in 'https://api.github.com/repos/wadaxiyang/typetide-win/releases/latest',
                 'https://github.com/wadaxiyang/typetide-win/releases') {
    if (-not $updater.Contains($url)) { throw "Missing fork update URL: $url" }
}
if ($updater.Contains('everettjf/')) { throw 'Updater still points to upstream' }
$installer = Get-Content (Join-Path $root 'windows/installer/TypeTide-Win.iss') -Raw
if (-not $installer.Contains('AppUpdatesURL=https://github.com/wadaxiyang/typetide-win/releases')) {
    throw 'Installer update URL does not point to the fork'
}
if ($Executable) {
    $info = (Get-Item -LiteralPath $Executable).VersionInfo
    if ($info.ProductName -ne 'TypeTide-Win' -or $info.ProductVersion -ne $version -or $info.FileVersion -ne $version) {
        throw 'Executable version/branding mismatch'
    }
}
Write-Output "release-check: $version; fork update URLs verified"
