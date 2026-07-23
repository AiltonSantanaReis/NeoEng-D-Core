[CmdletBinding()]
param(
    [switch]$InstallVcpkg
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

function Require-Command([string]$Name, [string]$Hint) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Dependência ausente: $Name. $Hint"
    }
}

Require-Command cmake 'Instale CMake 3.25+ e reabra o Developer PowerShell.'
Require-Command ninja 'Instale Ninja e adicione-o ao PATH.'
Require-Command clang-cl 'Instale LLVM x64 com clang-cl e adicione-o ao PATH.'
Require-Command git 'Git é necessário para o bootstrap opcional do vcpkg.'
if (-not (Get-Command link.exe -ErrorAction SilentlyContinue)) {
    throw 'link.exe/Windows SDK não encontrado. Execute dentro do Developer PowerShell for VS 2022.'
}

if (-not $env:VCPKG_ROOT) {
    $Marker = Join-Path $Root '.vcpkg-root'
    if (Test-Path $Marker) {
        $env:VCPKG_ROOT = (Get-Content $Marker -Raw).Trim()
    }
}

if (-not $env:VCPKG_ROOT) {
    if (-not $InstallVcpkg) {
        throw 'VCPKG_ROOT não definido. Execute novamente com -InstallVcpkg ou defina VCPKG_ROOT.'
    }
    $LocalVcpkg = Join-Path $Root '.deps\vcpkg'
    if (-not (Test-Path $LocalVcpkg)) {
        New-Item -ItemType Directory -Force -Path (Split-Path $LocalVcpkg) | Out-Null
        git clone --filter=blob:none --no-checkout https://github.com/microsoft/vcpkg.git $LocalVcpkg
        if ($LASTEXITCODE -ne 0) { throw 'Falha ao clonar vcpkg. Verifique rede/proxy.' }
        git -C $LocalVcpkg fetch origin 0878b5224d4a4968940ee296a2e7fae2d3b62983 --depth 1
        if ($LASTEXITCODE -ne 0) { throw 'Falha ao obter o baseline vcpkg fixado.' }
        git -C $LocalVcpkg checkout --detach FETCH_HEAD
        if ($LASTEXITCODE -ne 0) { throw 'Falha ao fixar o baseline vcpkg.' }
    }
    & (Join-Path $LocalVcpkg 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw 'Falha no bootstrap do vcpkg.' }
    $env:VCPKG_ROOT = $LocalVcpkg
    Set-Content -Path (Join-Path $Root '.vcpkg-root') -Value $LocalVcpkg -Encoding ascii
}

$Toolchain = Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'
if (-not (Test-Path $Toolchain)) {
    throw "VCPKG_ROOT inválido: $env:VCPKG_ROOT"
}

Write-Host "Ambiente validado. VCPKG_ROOT=$env:VCPKG_ROOT"
