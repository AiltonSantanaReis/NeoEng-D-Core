[CmdletBinding()]
param(
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [switch]$BootstrapDependencies,
    [switch]$Clean,
    [switch]$FullTestSuite
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
& (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration -BootstrapDependencies:$BootstrapDependencies -Clean:$Clean -FullTestSuite:$FullTestSuite
$Preset = if ($Configuration -eq 'Debug') { 'windows-clang-debug' } else { 'windows-clang-release' }
$BuildDir = Join-Path $Root "build\$Preset"
$Out = Join-Path $Root "artifacts\local\$Preset\manual-smoke"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
& (Join-Path $BuildDir 'neoeng_dcore_preclosure.exe') replay (Join-Path $Out 'replay') 10000
if ($LASTEXITCODE -ne 0) { throw 'Replay smoke falhou.' }
& (Join-Path $BuildDir 'neoeng_dcore_preclosure.exe') history (Join-Path $Out 'history') 900
if ($LASTEXITCODE -ne 0) { throw 'History smoke falhou.' }
& (Join-Path $BuildDir 'neoeng_dcore_preclosure.exe') network (Join-Path $Out 'network') 2000
if ($LASTEXITCODE -ne 0) { throw 'Network smoke falhou.' }
Write-Host "NeoEng D-Core executado no perfil $Preset. Artefatos: $Out"
