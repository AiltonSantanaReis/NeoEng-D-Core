[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
python (Join-Path $Root 'scripts\verify_isolation.py')
if ($LASTEXITCODE -ne 0) { throw 'Isolamento inválido.' }
python (Join-Path $Root 'scripts\generate_manifest.py') --check
if ($LASTEXITCODE -ne 0) { throw 'Manifesto SHA-256 divergente.' }
Write-Host 'Isolamento e manifesto aprovados.'
