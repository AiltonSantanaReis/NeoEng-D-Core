param(
    [ValidateSet('Preflight','Qualify')]
    [string]$Mode = 'Preflight',
    [string]$ControlRepo = '',
    [string]$LabRoot = "$env:USERPROFILE\NeoEng-DLab\EV-00"
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'CS017 R21 wrapper requires PowerShell 7+.' }
if (-not $IsWindows) { throw 'CS017 R21 physical campaign requires Windows.' }

if ([string]::IsNullOrWhiteSpace($ControlRepo)) {
    $ControlRepo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
} else {
    $ControlRepo = (Resolve-Path $ControlRepo).Path
}

$planPath = 'audit/validation/CS017/VALIDATION_PLAN.json'
$head = (& git -C $ControlRepo rev-parse HEAD | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($head)) { throw 'Unable to identify control repository HEAD.' }
$planCommit = (& git -C $ControlRepo log -n 1 --format=%H HEAD -- $planPath | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($planCommit)) { throw 'Unable to derive canonical CS017 R21 plan commit.' }
if ($head -ne $planCommit) { throw "Control HEAD must equal canonical R21 plan commit. HEAD=$head plan=$planCommit" }

$branch = (& git -C $ControlRepo branch --show-current | Out-String).Trim()
if ([string]::IsNullOrWhiteSpace($branch)) { throw 'R21 requires a named local control branch; detached HEAD is rejected.' }

function Get-ControlStatus {
    $text = (& git -C $ControlRepo status --porcelain --untracked-files=all | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Unable to read control repository status.' }
    return $text
}

$statusBefore = Get-ControlStatus
if (-not [string]::IsNullOrWhiteSpace($statusBefore)) { throw "Control repository must be clean before R21 wrapper checks:`n$statusBefore" }

$preservedR20 = Join-Path $PSScriptRoot 'invoke_ev00_r20.ps1'
if (-not (Test-Path -LiteralPath $preservedR20)) { throw "Preserved R20 wrapper missing: $preservedR20" }
$preservedHash = (& git -C $ControlRepo hash-object $preservedR20 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $preservedHash -ne 'df435a64d589f2bd68943a534810359e7af126cc') {
    throw "Preserved R20 wrapper hash mismatch: $preservedHash"
}

# Prevent importlib-driven Python bytecode caches from mutating the control tree.
$env:PYTHONDONTWRITEBYTECODE = '1'
$statusPreDelegate = Get-ControlStatus
if (-not [string]::IsNullOrWhiteSpace($statusPreDelegate)) { throw "R21 wrapper-side setup dirtied the control tree before delegation:`n$statusPreDelegate" }

& pwsh -NoProfile -File $preservedR20 -Mode $Mode -ControlRepo $ControlRepo -LabRoot $LabRoot
$delegateExit = $LASTEXITCODE

$statusAfter = Get-ControlStatus
if (-not [string]::IsNullOrWhiteSpace($statusAfter)) {
    throw "R21 wrapper/runner delegation changed the control tree; refusing result:`n$statusAfter"
}

exit $delegateExit
