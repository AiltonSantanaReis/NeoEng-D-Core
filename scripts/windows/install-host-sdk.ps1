[CmdletBinding()]
param(
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [string]$Prefix,
    [switch]$BootstrapDependencies,
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($Prefix)) {
    $Prefix = Join-Path $Root "artifacts\sdk\NeoEng-D-Core-1.11.0-$Configuration"
}
& (Join-Path $PSScriptRoot 'build.ps1') `
    -Configuration $Configuration `
    -BootstrapDependencies:$BootstrapDependencies `
    -Clean:$Clean
$Preset = if ($Configuration -eq 'Debug') { 'windows-clang-debug' } else { 'windows-clang-release' }
$BuildDir = Join-Path $Root "build\$Preset"
& cmake --install $BuildDir --config $Configuration --prefix $Prefix
if ($LASTEXITCODE -ne 0) { throw 'Instalação do Host SDK falhou.' }
Write-Host "Host SDK instalado em: $Prefix"
