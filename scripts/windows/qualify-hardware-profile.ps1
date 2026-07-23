[CmdletBinding()]
param(
    [ValidateSet('P0','P1','P2','P3')]
    [string]$Profile,
    [Parameter(Mandatory=$true)][string]$EnvironmentId,
    [Parameter(Mandatory=$true)][string]$CpuSku,
    [Parameter(Mandatory=$true)][string]$GpuSku,
    [Parameter(Mandatory=$true)][string]$DriverVersion,
    [Parameter(Mandatory=$true)][string]$OsBuild,
    [Parameter(Mandatory=$true)][string]$PowerProfile,
    [UInt64]$RollbackP99Ns,
    [UInt64]$EcsMaintenanceP99Ns,
    [Parameter(Mandatory=$true)][bool]$DeterminismPassed,
    [Parameter(Mandatory=$true)][bool]$SerializationPassed,
    [string]$BuildDirectory = 'build/windows-clang-release',
    [string]$OutputDirectory = 'artifacts/hardware-qualification'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build = Join-Path $root $BuildDirectory
$probe = Join-Path $build 'neoeng_hardware_profile_probe.exe'
if (-not (Test-Path $probe)) {
    throw "Hardware qualification probe not found: $probe. Build the project first."
}

$timestamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')
$out = Join-Path $root (Join-Path $OutputDirectory "$Profile-$timestamp")
New-Item -ItemType Directory -Force -Path $out | Out-Null

$determinism = if ($DeterminismPassed) { '1' } else { '0' }
$serialization = if ($SerializationPassed) { '1' } else { '0' }
$resultFile = Join-Path $out 'qualification-result.json'
& $probe $Profile $EnvironmentId $CpuSku $GpuSku $DriverVersion $OsBuild $PowerProfile `
    $RollbackP99Ns $EcsMaintenanceP99Ns $determinism $serialization | Tee-Object -FilePath $resultFile
$exitCode = $LASTEXITCODE

$environment = [ordered]@{
    schema = 'neoeng.dcore.hardware-environment.v1'
    collected_at_utc = (Get-Date).ToUniversalTime().ToString('o')
    profile = $Profile
    environment_id = $EnvironmentId
    cpu_sku = $CpuSku
    gpu_sku = $GpuSku
    driver_version = $DriverVersion
    os_build = $OsBuild
    power_profile = $PowerProfile
    rollback_p99_ns = $RollbackP99Ns
    ecs_maintenance_p99_ns = $EcsMaintenanceP99Ns
    determinism_passed = $DeterminismPassed
    serialization_passed = $SerializationPassed
    machine = $env:COMPUTERNAME
    powershell = $PSVersionTable.PSVersion.ToString()
}
$environment | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 (Join-Path $out 'environment.json')
Get-FileHash -Algorithm SHA256 $resultFile, (Join-Path $out 'environment.json') |
    Select-Object Hash, Path | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $out 'SHA256SUMS.json')

if ($exitCode -ne 0) {
    throw "Hardware profile is not qualified. See $resultFile."
}
Write-Host "Hardware profile qualification passed. Evidence: $out"
