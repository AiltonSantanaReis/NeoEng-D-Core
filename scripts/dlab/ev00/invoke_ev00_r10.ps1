param(
    [ValidateSet('Preflight','Qualify')]
    [string]$Mode = 'Preflight',
    [string]$ControlRepo = '',
    [string]$LabRoot = "$env:USERPROFILE\NeoEng-DLab\EV-00"
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($ControlRepo)) {
    $ControlRepo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
} else {
    $ControlRepo = (Resolve-Path $ControlRepo).Path
}

$head = (& git -C $ControlRepo rev-parse HEAD | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($head)) {
    throw 'Unable to resolve control repository HEAD.'
}

$planCommit = (& git -C $ControlRepo log -n 1 --format=%H -- audit/validation/CS017/VALIDATION_PLAN.json | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($planCommit)) {
    throw 'Unable to derive canonical CS017 plan commit.'
}
if ($head -ne $planCommit) {
    throw "R10 requires HEAD at the canonical plan commit. HEAD=$head plan=$planCommit"
}

$branch = (& git -C $ControlRepo branch --show-current | Out-String).Trim()
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to resolve control repository branch state.'
}
if ([string]::IsNullOrWhiteSpace($branch)) {
    throw 'R10 control checkout must use a named local branch at the canonical plan commit; detached HEAD is prohibited because the preserved runner records control_branch.'
}

$status = (& git -C $ControlRepo status --porcelain | Out-String).Trim()
if (-not [string]::IsNullOrWhiteSpace($status)) {
    throw 'R10 control repository must be clean before invoking the preserved EV-00 runner.'
}

$runner = Join-Path $PSScriptRoot 'run_ev00_dlab_windows.ps1'
if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) {
    throw "Preserved EV-00 runner missing: $runner"
}

& pwsh -NoProfile -File $runner -Mode $Mode -ControlRepo $ControlRepo -LabRoot $LabRoot
exit $LASTEXITCODE
