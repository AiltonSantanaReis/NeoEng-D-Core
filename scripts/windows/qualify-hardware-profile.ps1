[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Request,
    [string]$BuildDirectory = 'build/windows-clang-release',
    [string]$OutputDirectory = 'artifacts/hardware-qualification'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$requestPath = (Resolve-Path $Request).Path
$buildPath = (Resolve-Path (Join-Path $root $BuildDirectory)).Path
$timestamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')
$requestDocument = Get-Content -Raw $requestPath | ConvertFrom-Json
$profile = [string]$requestDocument.profile
if ($profile -notin @('P0','P1','P2','P3','P4')) {
    throw "Request profile must be P0, P1, P2, P3 or P4."
}
$outputPath = Join-Path $root (Join-Path $OutputDirectory "$profile-$timestamp")
$runner = Join-Path $root 'scripts\qualification\run_qualification_campaign.py'
$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) { $python = Get-Command py -ErrorAction SilentlyContinue }
if (-not $python) { throw 'Python 3 is required for the qualification harness.' }

Write-Host "Starting NeoEng D-Core qualification campaign"
Write-Host "  Request: $requestPath"
Write-Host "  Build:   $buildPath"
Write-Host "  Output:  $outputPath"
& $python.Source $runner --request $requestPath --build-dir $buildPath --output-dir $outputPath
if ($LASTEXITCODE -ne 0) {
    throw "Qualification campaign execution failed. Evidence, when available, remains at $outputPath"
}
Write-Host "Campaign completed and verified: $outputPath"
