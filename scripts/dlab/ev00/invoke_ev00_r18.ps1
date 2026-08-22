param(
    [ValidateSet('Preflight','Qualify')]
    [string]$Mode = 'Preflight',
    [string]$ControlRepo = '',
    [string]$LabRoot = "$env:USERPROFILE\NeoEng-DLab\EV-00"
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'CS017 R18 wrapper requires PowerShell 7+.' }
if (-not $IsWindows) { throw 'CS017 R18 physical campaign requires Windows.' }

if ([string]::IsNullOrWhiteSpace($ControlRepo)) {
    $ControlRepo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
} else {
    $ControlRepo = (Resolve-Path $ControlRepo).Path
}

$planPath = 'audit/validation/CS017/VALIDATION_PLAN.json'
$head = (& git -C $ControlRepo rev-parse HEAD | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($head)) { throw 'Unable to identify control repository HEAD.' }
$planCommit = (& git -C $ControlRepo log -n 1 --format=%H HEAD -- $planPath | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($planCommit)) { throw 'Unable to derive canonical CS017 R18 plan commit.' }
if ($head -ne $planCommit) { throw "Control HEAD must equal canonical R18 plan commit. HEAD=$head plan=$planCommit" }

$branch = (& git -C $ControlRepo branch --show-current | Out-String).Trim()
if ([string]::IsNullOrWhiteSpace($branch)) { throw 'R18 requires a named local control branch; detached HEAD is rejected.' }

function Get-ControlStatus {
    $text = (& git -C $ControlRepo status --porcelain --untracked-files=all | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Unable to read control repository status.' }
    return $text
}

$statusBefore = Get-ControlStatus
if (-not [string]::IsNullOrWhiteSpace($statusBefore)) { throw "Control repository must be clean before R18 wrapper checks:`n$statusBefore" }

$preservedR17 = Join-Path $PSScriptRoot 'invoke_ev00_r17.ps1'
if (-not (Test-Path -LiteralPath $preservedR17)) { throw "Preserved R17 wrapper missing: $preservedR17" }
$preservedHash = (& git -C $ControlRepo hash-object $preservedR17 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $preservedHash -ne '09540ceafbeb1336489911897e9b2f7f156f5114') {
    throw "Preserved R17 wrapper hash mismatch: $preservedHash"
}

# Prevent importlib-driven Python bytecode caches from mutating the control tree.
$env:PYTHONDONTWRITEBYTECODE = '1'
$statusPreDelegate = Get-ControlStatus
if (-not [string]::IsNullOrWhiteSpace($statusPreDelegate)) { throw "R18 wrapper-side setup dirtied the control tree before delegation:`n$statusPreDelegate" }

& pwsh -NoProfile -File $preservedR17 -Mode $Mode -ControlRepo $ControlRepo -LabRoot $LabRoot
$delegateExit = $LASTEXITCODE

$statusAfter = Get-ControlStatus
if (-not [string]::IsNullOrWhiteSpace($statusAfter)) {
    throw "R18 wrapper/runner delegation changed the control tree; refusing result:`n$statusAfter"
}

exit $delegateExit
