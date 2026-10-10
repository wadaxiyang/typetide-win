# Trigger the GitHub Actions release workflow from committed release code.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $version = (Get-Content VERSION -Raw).Trim()
    $tag = "v$version"
    & (Join-Path $PSScriptRoot 'check-release.ps1') -Tag $tag
    if (git status --porcelain) { throw 'Commit release code before creating the release tag.' }
    $remote = git remote get-url origin
    if ($remote -notmatch 'github\.com[:/]wadaxiyang/typetide-win(?:\.git)?$') {
        throw 'origin must point to wadaxiyang/typetide-win'
    }
    if (-not (Test-Path "docs/releases/$tag.md")) { throw "Add docs/releases/$tag.md first." }
    $existing = git tag --list $tag
    if ($existing) {
        $tagCommit = git rev-list -n 1 $tag
        $headCommit = git rev-parse HEAD
        if ($tagCommit -ne $headCommit) { throw "$tag already points to another commit; do not move published tags." }
    } else {
        git tag -a $tag -m "TypeTide-Win $version"
        if ($LASTEXITCODE -ne 0) { throw 'Could not create release tag' }
    }
    git push origin HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Branch push failed' }
    git push origin "refs/tags/$tag"
    if ($LASTEXITCODE -ne 0) { throw 'Tag push failed' }
    Write-Output "GitHub Actions release triggered: https://github.com/wadaxiyang/typetide-win/actions/workflows/release.yml"
    Write-Output "Release will appear after checks pass: https://github.com/wadaxiyang/typetide-win/releases/tag/$tag"
} finally { Pop-Location }
