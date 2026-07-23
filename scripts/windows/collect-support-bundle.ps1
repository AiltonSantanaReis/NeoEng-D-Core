[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string]$BuildDirectory = "build/windows-clang-release",

    [Parameter(Mandatory = $false)]
    [string]$OutputDirectory = "support-bundle",

    [Parameter(Mandatory = $false)]
    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$probe = Join-Path $projectRoot "$BuildDirectory/neoeng_support_bundle_probe.exe"
$verifier = Join-Path $projectRoot "scripts/verify_support_bundle.py"
$output = Join-Path $projectRoot $OutputDirectory

if (-not (Test-Path -LiteralPath $probe -PathType Leaf)) {
    throw "Support-bundle probe not found: $probe. Build NeoEng D-Core first."
}

if (Test-Path -LiteralPath $output) {
    Remove-Item -LiteralPath $output -Recurse -Force
}

& $probe $output
if ($LASTEXITCODE -ne 0) {
    throw "neoeng_support_bundle_probe failed with exit code $LASTEXITCODE"
}

& $Python $verifier $output
if ($LASTEXITCODE -ne 0) {
    throw "support-bundle verification failed with exit code $LASTEXITCODE"
}

Write-Host "Support bundle generated and verified: $output"
