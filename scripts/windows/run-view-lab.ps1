[CmdletBinding()]
param(
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [string]$OutputDirectory,
    [string]$EnvironmentId = 'WINDOWS-UNQUALIFIED',
    [switch]$BootstrapDependencies,
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
& (Join-Path $PSScriptRoot 'build.ps1') `
    -Configuration $Configuration `
    -BootstrapDependencies:$BootstrapDependencies `
    -Clean:$Clean `
    -FullTestSuite
$Preset = if ($Configuration -eq 'Debug') { 'windows-clang-debug' } else { 'windows-clang-release' }
$Executable = Join-Path $Root "build\$Preset\modules\view_lab\neoeng_dcore_view_lab_cli.exe"
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "View Lab não foi produzido: $Executable"
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $Root "artifacts\local\$Preset\view-lab"
}
& $Executable $OutputDirectory $EnvironmentId
if ($LASTEXITCODE -ne 0) { throw "View Lab falhou com código $LASTEXITCODE" }
Write-Host "Abra no navegador: $(Join-Path $OutputDirectory 'index.html')"
