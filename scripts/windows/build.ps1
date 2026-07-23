[CmdletBinding()]
param(
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [switch]$BootstrapDependencies,
    [switch]$Clean,
    [switch]$FullTestSuite
)
$ErrorActionPreference = 'Stop'
# NEOENG_WINDOWS_CLANG_C_ABI_BEGIN
# C e C++ devem usar o mesmo frontend clang-cl no preset Windows.
$NeoEngClangClCompiler = (Get-Command clang-cl.exe -ErrorAction Stop).Source
$env:CC = $NeoEngClangClCompiler
$env:CXX = $NeoEngClangClCompiler
# NEOENG_WINDOWS_CLANG_C_ABI_END
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
& (Join-Path $PSScriptRoot 'bootstrap.ps1') -InstallVcpkg:$BootstrapDependencies
$Preset = if ($Configuration -eq 'Debug') { 'windows-clang-debug' } else { 'windows-clang-release' }
$BuildDir = Join-Path $Root "build\$Preset"
$ArtifactDir = Join-Path $Root "artifacts\local\$Preset"
New-Item -ItemType Directory -Force -Path $ArtifactDir | Out-Null
if ($Clean -and (Test-Path $BuildDir)) { Remove-Item -Recurse -Force $BuildDir }
& cmake --preset $Preset 2>&1 | Tee-Object -FilePath (Join-Path $ArtifactDir 'configure.log')
if ($LASTEXITCODE -ne 0) { throw 'Configuração CMake falhou; consulte configure.log.' }
& cmake --build --preset $Preset --parallel 2 2>&1 | Tee-Object -FilePath (Join-Path $ArtifactDir 'build.log')
if ($LASTEXITCODE -ne 0) { throw 'Build falhou; consulte build.log.' }
if ($FullTestSuite) {
    & ctest --preset $Preset -L dcore --output-on-failure 2>&1 | Tee-Object -FilePath (Join-Path $ArtifactDir 'ctest-dcore-full.log')
} else {
    & ctest --preset $Preset -L smoke --output-on-failure 2>&1 | Tee-Object -FilePath (Join-Path $ArtifactDir 'ctest-smoke.log')
}
if ($LASTEXITCODE -ne 0) { throw 'Testes falharam; consulte os logs.' }
Write-Host "Build e testes concluídos: $ArtifactDir"
