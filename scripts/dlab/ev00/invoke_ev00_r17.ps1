param(
    [ValidateSet('Preflight','Qualify')]
    [string]$Mode = 'Preflight',
    [string]$ControlRepo = '',
    [string]$LabRoot = "$env:USERPROFILE\NeoEng-DLab\EV-00"
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProductSha = 'e3fff973554a2e56b8bd7afdc1132f75f3ec337c'

if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'CS017 R17 wrapper requires PowerShell 7+.' }
if (-not $IsWindows) { throw 'CS017 R17 physical campaign requires Windows.' }

if ([string]::IsNullOrWhiteSpace($ControlRepo)) {
    $ControlRepo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
} else {
    $ControlRepo = (Resolve-Path $ControlRepo).Path
}

$planPath = 'audit/validation/CS017/VALIDATION_PLAN.json'
$head = (& git -C $ControlRepo rev-parse HEAD | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($head)) { throw 'Unable to identify control repository HEAD.' }
$planCommit = (& git -C $ControlRepo log -n 1 --format=%H HEAD -- $planPath | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($planCommit)) { throw 'Unable to derive canonical CS017 R17 plan commit.' }
if ($head -ne $planCommit) { throw "Control HEAD must equal canonical R17 plan commit. HEAD=$head plan=$planCommit" }

$branch = (& git -C $ControlRepo branch --show-current | Out-String).Trim()
if ([string]::IsNullOrWhiteSpace($branch)) { throw 'R17 requires a named local control branch; detached HEAD is rejected.' }
$status = (& git -C $ControlRepo status --porcelain | Out-String).Trim()
if (-not [string]::IsNullOrWhiteSpace($status)) { throw 'Control repository must be clean before R17 Preflight/Qualify.' }

function Assert-MsvcEnvironment {
    foreach ($name in @('VCToolsInstallDir','LIB','INCLUDE','WindowsSdkDir')) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if ([string]::IsNullOrWhiteSpace($value)) { throw "Required Visual Studio developer environment variable is empty: $name" }
    }
    if (-not (Test-Path -LiteralPath $env:VCToolsInstallDir)) { throw "VCToolsInstallDir does not exist: $env:VCToolsInstallDir" }
    $libDirs = @($env:LIB -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) -and (Test-Path -LiteralPath $_) })
    if ($libDirs.Count -eq 0) { throw 'LIB contains no existing directories.' }
    $msvcDirs = @($libDirs | Where-Object { $_ -match '\\VC\\Tools\\MSVC\\' -and $_ -match '\\lib\\x64\\?$' })
    if ($msvcDirs.Count -eq 0) { throw 'LIB does not contain an x64 MSVC library directory.' }
    foreach ($library in @('msvcrtd.lib','oldnames.lib')) {
        $found = $false
        foreach ($dir in $libDirs) {
            if (Test-Path -LiteralPath (Join-Path $dir $library)) { $found = $true; break }
        }
        if (-not $found) { throw "Required MSVC library is not reachable through LIB: $library" }
    }
}

function Invoke-ToolchainLinkSmoke {
    foreach ($tool in @('cmake','ninja','clang-cl')) {
        if ($null -eq (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Required tool not found for R17 compile-link smoke test: $tool" }
    }
    $root = Join-Path ([IO.Path]::GetTempPath()) ("neoeng-ev00-r17-link-smoke-" + [Guid]::NewGuid().ToString('N'))
    $source = Join-Path $root 'src'
    $build = Join-Path $root 'build'
    New-Item -ItemType Directory -Force -Path $source,$build | Out-Null
    try {
        [IO.File]::WriteAllText((Join-Path $source 'CMakeLists.txt'), "cmake_minimum_required(VERSION 3.20)`nproject(neoeng_ev00_r17_link_smoke C)`nadd_executable(neoeng_ev00_r17_link_smoke main.c)`n", [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText((Join-Path $source 'main.c'), "int main(void) { return 0; }`n", [Text.UTF8Encoding]::new($false))
        $configure = (& cmake -S $source -B $build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang-cl 2>&1 | Out-String)
        if ($LASTEXITCODE -ne 0) { throw "R17 CMake compile-link smoke configure failed:`n$configure" }
        $buildOutput = (& cmake --build $build --parallel 1 2>&1 | Out-String)
        if ($LASTEXITCODE -ne 0) { throw "R17 CMake compile-link smoke build failed:`n$buildOutput" }
        $exe = Get-ChildItem -Path $build -Filter 'neoeng_ev00_r17_link_smoke.exe' -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -eq $exe) { throw 'R17 compile-link smoke did not produce the expected executable.' }
    } finally {
        Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Import-CtestParser {
    param([Parameter(Mandatory=$true)][string]$Runner)
    $tokens = $null
    $parseErrors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile($Runner,[ref]$tokens,[ref]$parseErrors)
    if ($parseErrors.Count -ne 0) { throw 'R17 cannot validate CTest parser because canonical runner has syntax errors.' }
    $fn = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Parse-CtestInventory' }, $true)
    if ($null -eq $fn) { throw 'R17 canonical runner does not define Parse-CtestInventory.' }
    $definition = 'function script:Parse-CtestInventory ' + $fn.Body.Extent.Text
    Invoke-Expression $definition
    if ($null -eq (Get-Command Parse-CtestInventory -CommandType Function -ErrorAction SilentlyContinue)) {
        throw 'R17 failed to promote Parse-CtestInventory into wrapper script scope.'
    }
}

function Assert-CtestParserCompatibility {
    param([Parameter(Mandatory=$true)][string]$Runner)
    Import-CtestParser -Runner $Runner
    $normative = Join-Path $ControlRepo 'docs\changesets\015\evidence\github-actions-run-30375982639\raw\windows-clang-cl-ctest.txt'
    if (-not (Test-Path -LiteralPath $normative)) { throw "R17 normative CS015 Windows CTest reference missing: $normative" }
    $parsed = Parse-CtestInventory $normative
    if ($parsed.percent -ne 100 -or $parsed.failed -ne 0 -or $parsed.total -ne 54 -or @($parsed.names).Count -ne 54) {
        throw 'R17 canonical parser did not resolve normative CS015 inventory as 100%/0/54/54-names.'
    }
}

function Assert-DualSurfaceContract {
    param(
        [Parameter(Mandatory=$true)][string]$Runner,
        [Parameter(Mandatory=$true)][string]$VerifierSnapshot,
        [Parameter(Mandatory=$true)][string]$VerifierCurrent
    )
    Import-CtestParser -Runner $Runner
    $normative = Join-Path $ControlRepo 'docs\changesets\015\evidence\github-actions-run-30375982639\raw\windows-clang-cl-ctest.txt'
    $supported = Parse-CtestInventory $normative
    foreach ($name in @('neoeng_dcore_replay_smoke','neoeng_dcore_history_smoke')) {
        if (@($supported.names) -contains $name) { throw "R17 normative 54-test surface unexpectedly contains research-only test: $name" }
    }

    $cmakeText = (& git -C $ControlRepo show "${ProductSha}:CMakeLists.txt" | Out-String)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($cmakeText)) { throw 'Unable to read protected baseline CMakeLists.txt for R17 contract audit.' }
    foreach ($token in @('if(NEOENG_DCORE_BUILD_RESEARCH_TOOLS)','neoeng_dcore_add_tool(neoeng_dcore_preclosure apps/v28_year1_preclosure.cpp)','add_test(NAME neoeng_dcore_replay_smoke','add_test(NAME neoeng_dcore_history_smoke','neoeng_temporal_closure_tests')) {
        if (-not $cmakeText.Contains($token)) { throw "Protected baseline lacks required R17 research contract token: $token" }
    }

    $runnerText = [IO.File]::ReadAllText($Runner)
    foreach ($token in @('$ResearchBuildDir',"'-DNEOENG_DCORE_BUILD_RESEARCH_TOOLS=OFF'","'-DNEOENG_DCORE_BUILD_RESEARCH_TOOLS=ON'","-Name 'research-cmake-configure'","-Name 'research-cmake-build'","'neoeng_dcore_preclosure'","'neoeng_temporal_closure_tests'","'^(neoeng_dcore_replay_smoke|neoeng_dcore_history_smoke|neoeng_temporal_closure_tests)$'",'replay-rollback-validation.json')) {
        if (-not $runnerText.Contains($token)) { throw "R17 canonical runner lacks dual-surface contract token: $token" }
    }

    $snapshotText = [IO.File]::ReadAllText($VerifierSnapshot)
    foreach ($token in @('verify_ev00_dlab_evidence_r15.py','research_contract_self_test_fixed','started_at_utc','finished_at_utc','r15.check_replay_rollback')) {
        if (-not $snapshotText.Contains($token)) { throw "Preserved R16 verifier lacks expected adapter token: $token" }
    }
    $currentText = [IO.File]::ReadAllText($VerifierCurrent)
    if ($currentText -ne $snapshotText) { throw 'R17 runtime verifier must be byte-for-byte identical to preserved R16 verifier.' }
}

function Invoke-VerifierSelfTest {
    param([Parameter(Mandatory=$true)][string]$Verifier)
    $output = (& python $Verifier --self-test 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0) { throw "R17 independent verifier self-test failed:`n$output" }
    if (-not $output.Contains('EV00 EVIDENCE VERIFIER SELF-TEST: PASS')) { throw "R17 verifier self-test did not emit PASS marker:`n$output" }
}

Assert-MsvcEnvironment
Invoke-ToolchainLinkSmoke

$runner = Join-Path $PSScriptRoot 'run_ev00_dlab_windows.ps1'
$verifier = Join-Path $PSScriptRoot 'verify_ev00_dlab_evidence.py'
$verifierSnapshot = Join-Path $PSScriptRoot 'verify_ev00_dlab_evidence_r16.py'
foreach ($p in @($runner,$verifier,$verifierSnapshot)) { if (-not (Test-Path -LiteralPath $p)) { throw "R17 required EV-00 artifact missing: $p" } }
Assert-CtestParserCompatibility -Runner $runner
Assert-DualSurfaceContract -Runner $runner -VerifierSnapshot $verifierSnapshot -VerifierCurrent $verifier
Invoke-VerifierSelfTest -Verifier $verifier

& pwsh -NoProfile -File $runner -Mode $Mode -ControlRepo $ControlRepo -LabRoot $LabRoot
exit $LASTEXITCODE
