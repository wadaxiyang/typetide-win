# Real silent upgrade smoke test on the disposable Windows CI runner only.
# The installer writes its normal registration, so never run this on a user's PC.
param(
    [Parameter(Mandatory)][string]$Setup,
    [Parameter(Mandatory)][string]$LegacyFixture,
    [Parameter(Mandatory)][string]$Version
)
$ErrorActionPreference = 'Stop'
if ($env:CI -ne 'true' -or $env:GITHUB_ACTIONS -ne 'true') {
    throw 'Installer upgrade smoke tests require a disposable GitHub Actions runner.'
}
$testRoot = Join-Path $env:RUNNER_TEMP ('typetide-upgrade-' + [guid]::NewGuid().ToString('N'))
$installDir = Join-Path $testRoot 'app'
$dataDir = Join-Path $testRoot 'data'
New-Item -ItemType Directory -Path $installDir, $dataDir | Out-Null
$app = Join-Path $installDir 'TypeTide-Win.exe'
$settings = Join-Path $dataDir 'settings.json'
'{"enabled":false,"backend":"openai","hasCompletedFirstLaunch":true,"lastUpdateCheck":4102444800}' |
    Set-Content -LiteralPath $settings -Encoding ascii
$settingsHash = (Get-FileHash -LiteralPath $settings).Hash
Copy-Item -LiteralPath $LegacyFixture -Destination $app
$previousDataDir = $env:TYPETIDE_DATA_DIR
$env:TYPETIDE_DATA_DIR = $dataDir
try {
    foreach ($scenario in 'legacy-tray', 'current-tray') {
        $running = Start-Process -FilePath $app -WindowStyle Hidden -PassThru
        $installer = $null
        try {
            if (-not $running.WaitForInputIdle(10000) -or $running.HasExited) {
                throw "Could not start the $scenario upgrade fixture"
            }
            $log = Join-Path $testRoot "$scenario.log"
            $installer = Start-Process -FilePath $Setup -WindowStyle Hidden -PassThru -ArgumentList @(
                '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/NOICONS', '/TASKS=""',
                "/DIR=`"$installDir`"", "/LOG=`"$log`""
            )
            if (-not $installer.WaitForExit(120000)) { throw "Installer timed out: $scenario (log: $log)" }
            if ($installer.ExitCode -ne 0) { throw "Installer failed: $scenario, exit $($installer.ExitCode) (log: $log)" }
            if (-not $running.WaitForExit(5000)) { throw "Installer left the $scenario process running" }
            if ((Get-Item -LiteralPath $app).VersionInfo.ProductVersion -ne $Version) {
                throw "Installed executable version does not match $Version"
            }
            if ((Get-FileHash -LiteralPath $settings).Hash -ne $settingsHash) {
                throw 'Upgrade modified user settings'
            }
            Write-Output "PASS: $scenario closed, version $Version installed, settings preserved"
        } finally {
            # Handles refer only to the two processes started by this test.
            foreach ($process in @($installer, $running)) {
                if ($process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit(5000) | Out-Null }
                if ($process) { $process.Dispose() }
            }
        }
    }
} finally { $env:TYPETIDE_DATA_DIR = $previousDataDir }
