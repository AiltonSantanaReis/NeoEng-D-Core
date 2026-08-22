param(
    [ValidateSet('Preflight','Qualify')]
    [string]$Mode = 'Preflight',
    [string]$ControlRepo = '',
    [string]$LabRoot = "$env:USERPROFILE\NeoEng-DLab\EV-00"
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'CS017 R20 wrapper requires PowerShell 7+.' }
if (-not $IsWindows) { throw 'CS017 R20 physical campaign requires Windows.' }

if ([string]::IsNullOrWhiteSpace($ControlRepo)) {
    $ControlRepo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
} else {
    $ControlRepo = (Resolve-Path $ControlRepo).Path
}

$planPath = 'audit/validation/CS017/VALIDATION_PLAN.json'
$head = (& git -C $ControlRepo rev-parse HEAD | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($head)) { throw 'Unable to identify control repository HEAD.' }
$planCommit = (& git -C $ControlRepo log -n 1 --format=%H HEAD -- $planPath | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($planCommit)) { throw 'Unable to derive canonical CS017 R20 plan commit.' }
if ($head -ne $planCommit) { throw "Control HEAD must equal canonical R20 plan commit. HEAD=$head plan=$planCommit" }

$branch = (& git -C $ControlRepo branch --show-current | Out-String).Trim()
if ([string]::IsNullOrWhiteSpace($branch)) { throw 'R20 requires a named local control branch; detached HEAD is rejected.' }

function Get-ControlStatus {
    $text = (& git -C $ControlRepo status --porcelain --untracked-files=all | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Unable to read control repository status.' }
    return $text
}

$statusBefore = Get-ControlStatus
if (-not [string]::IsNullOrWhiteSpace($statusBefore)) { throw "Control repository must be clean before R20 wrapper checks:`n$statusBefore" }

$preservedR19 = Join-Path $PSScriptRoot 'invoke_ev00_r19.ps1'
if (-not (Test-Path -LiteralPath $preservedR19)) { throw "Preserved R19 wrapper missing: $preservedR19" }
$preservedHash = (& git -C $ControlRepo hash-object $preservedR19 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $preservedHash -ne '1c7e0f7eff28ebe8c66c2d93888e4e6324cef41b') {
    throw "Preserved R19 wrapper hash mismatch: $preservedHash"
}

# Prevent importlib-driven Python bytecode caches from mutating the control tree.
$env:PYTHONDONTWRITEBYTECODE = '1'
$statusPreDelegate = Get-ControlStatus
if (-not [string]::IsNullOrWhiteSpace($statusPreDelegate)) { throw "R20 wrapper-side setup dirtied the control tree before delegation:`n$statusPreDelegate" }

& pwsh -NoProfile -File $preservedR19 -Mode $Mode -ControlRepo $ControlRepo -LabRoot $LabRoot
$delegateExit = $LASTEXITCODE

$statusAfter = Get-ControlStatus
if (-not [string]::IsNullOrWhiteSpace($statusAfter)) {
    throw "R20 wrapper/runner delegation changed the control tree; refusing result:`n$statusAfter"
}

exit $delegateExit
