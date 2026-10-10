# Sync Windows version metadata: default patch +1, -Minor, -Major, or -Version 1.2.3.
[CmdletBinding(DefaultParameterSetName = 'Patch')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Explicit', Position = 0)]
    [ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [Parameter(Mandatory, ParameterSetName = 'Minor')][switch]$Minor,
    [Parameter(Mandatory, ParameterSetName = 'Major')][switch]$Major
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$current = (Get-Content (Join-Path $root 'VERSION') -Raw).Trim()
$parts = $current.Split('.') | ForEach-Object { [int]$_ }
if ($Major) { $Version = "$($parts[0] + 1).0.0" }
elseif ($Minor) { $Version = "$($parts[0]).$($parts[1] + 1).0" }
elseif (-not $Version) { $Version = "$($parts[0]).$($parts[1]).$($parts[2] + 1)" }
foreach ($part in $Version.Split('.')) {
    if ([long]$part -gt 65535) { throw 'Version components must fit Windows 16-bit version fields.' }
}
$utf8 = New-Object System.Text.UTF8Encoding $false
$commaVersion = $Version.Replace('.', ',') + ',0'
$updates = @{}
$cmakePath = Join-Path $root 'windows/CMakeLists.txt'
$updates[$cmakePath] = [regex]::Replace([IO.File]::ReadAllText($cmakePath),
    'project\(TypeTide-Win VERSION [0-9.]+', "project(TypeTide-Win VERSION $Version")
$resourcePath = Join-Path $root 'windows/src/TypeTide-Win.rc'
$resource = [IO.File]::ReadAllText($resourcePath)
$resource = $resource -replace '(FILEVERSION|PRODUCTVERSION) [0-9,]+', "`$1 $commaVersion"
$resource = $resource -replace '("(?:FileVersion|ProductVersion)", ")[0-9.]+', "`${1}$Version"
$updates[$resourcePath] = $resource
$manifestPath = Join-Path $root 'windows/src/TypeTide-Win.manifest'
$manifest = [IO.File]::ReadAllText($manifestPath)
# Match only the app identity; leave the Common-Controls dependency untouched.
$manifest = $manifest -replace '(version=")[0-9.]+(" processorArchitecture="\*" name="wadaxiyang.TypeTide-Win")', "`${1}$Version.0`${2}"
$updates[$manifestPath] = $manifest
$updates[(Join-Path $root 'VERSION')] = "$Version`n"
foreach ($path in $updates.Keys) { [IO.File]::WriteAllText($path, $updates[$path], $utf8) }
Write-Output "version: $current -> $Version"
