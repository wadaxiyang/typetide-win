$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not (Select-String -Quiet -Path (Join-Path $root 'windows/src/UITheme.h') -Pattern 'Accent = RGB\(0x7C, 0x5C, 0xFF\)')) {
    throw 'Windows brand accent token changed unexpectedly'
}
[xml]$manifest = Get-Content (Join-Path $root 'windows/src/TypeTide-Win.manifest') -Raw
$controls = $manifest.assembly.dependency.dependentAssembly.assemblyIdentity
if ($controls.name -ne 'Microsoft.Windows.Common-Controls' -or $controls.version -ne '6.0.0.0') {
    throw 'Windows Common-Controls v6 manifest entry is missing'
}
Write-Output 'ui-quality: passed'
