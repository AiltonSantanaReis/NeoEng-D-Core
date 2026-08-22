param(
    [ValidateSet('Preflight','Qualify')]
    [string]$Mode = 'Preflight',
    [string]$ControlRepo = '',
    [string]$LabRoot = "$env:USERPROFILE\NeoEng-DLab\EV-00"
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProductSha = 'e3fff973554a2e56b8bd7afdc1132f75f3ec337c'

if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'CS017 R15 wrapper requires PowerShell 7+.' }
if (-not $IsWindows) { throw 'CS017 R15 physical campaign requires Windows.' }

if ([string]::IsNullOrWhiteSpace($ControlRepo)) {
    $ControlRepo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
} else {
    $ControlRepo = (Resolve-Path $ControlRepo).Path
}

$planPath = 'audit/validation/CS017/VALIDATION_PLAN.json'
$head = (& git -C $ControlRepo rev-parse HEAD | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($head)) { throw 'Unable to identify control repository HEAD.' }
$planCommit = (& git -C $ControlRepo log -n 1 --format=%H HEAD -- $planPath | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($planCommit)) { throw 'Unable to derive canonical CS017 R15 plan commit.' }
if ($head -ne $planCommit) { throw "Control HEAD must equal canonical R15 plan commit. HEAD=$head plan=$planCommit" }

$branch = (& git -C $ControlRepo branch --show-current | Out-String).Trim()
if ([string]::IsNullOrWhiteSpace($branch)) { throw 'R15 requires a named local control branch; detached HEAD is rejected.' }
$status = (& git -C $ControlRepo status --porcelain | Out-String).Trim()
if (-not [string]::IsNullOrWhiteSpace($status)) { throw 'Control repository must be clean before R15 Preflight/Qualify.' }

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
        if ($null -eq (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Required tool not found for R15 compile-link smoke test: $tool" }
    }
    $root = Join-Path ([IO.Path]::GetTempPath()) ("neoeng-ev00-r15-link-smoke-" + [Guid]::NewGuid().ToString('N'))
    $source = Join-Path $root 'src'
    $build = Join-Path $root 'build'
    New-Item -ItemType Directory -Force -Path $source,$build | Out-Null
    try {
        [IO.File]::WriteAllText((Join-Path $source 'CMakeLists.txt'), "cmake_minimum_required(VERSION 3.20)`nproject(neoeng_ev00_r15_link_smoke C)`nadd_executable(neoeng_ev00_r15_link_smoke main.c)`n", [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText((Join-Path $source 'main.c'), "int main(void) { return 0; }`n", [Text.UTF8Encoding]::new($false))
        $configure = (& cmake -S $source -B $build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang-cl 2>&1 | Out-String)
        if ($LASTEXITCODE -ne 0) { throw "R15 CMake compile-link smoke configure failed:`n$configure" }
        $buildOutput = (& cmake --build $build --parallel 1 2>&1 | Out-String)
        if ($LASTEXITCODE -ne 0) { throw "R15 CMake compile-link smoke build failed:`n$buildOutput" }
        $exe = Get-ChildItem -Path $build -Filter 'neoeng_ev00_r15_link_smoke.exe' -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -eq $exe) { throw 'R15 compile-link smoke did not produce the expected executable.' }
    } finally {
        Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Import-CtestParser {
    param([Parameter(Mandatory=$true)][string]$Runner)
    $tokens = $null
    $parseErrors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile($Runner,[ref]$tokens,[ref]$parseErrors)
    if ($parseErrors.Count -ne 0) { throw 'R15 cannot validate CTest parser because canonical runner has syntax errors.' }
    $fn = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Parse-CtestInventory' }, $true)
    if ($null -eq $fn) { throw 'R15 canonical runner does not define Parse-CtestInventory.' }
    Invoke-Expression $fn.Extent.Text
}

function Assert-CtestParserCompatibility {
    param([Parameter(Mandatory=$true)][string]$Runner)
    Import-CtestParser -Runner $Runner
    $normative = Join-Path $ControlRepo 'docs\changesets\015\evidence\github-actions-run-30375982639\raw\windows-clang-cl-ctest.txt'
    if (-not (Test-Path -LiteralPath $normative)) { throw "R15 normative CS015 Windows CTest reference missing: $normative" }
    $parsed = Parse-CtestInventory $normative
    if ($parsed.percent -ne 100 -or $parsed.failed -ne 0 -or $parsed.total -ne 54 -or @($parsed.names).Count -ne 54) {
        throw 'R15 canonical parser did not resolve normative CS015 inventory as 100%/0/54/54-names.'
    }

    $root = Join-Path ([IO.Path]::GetTempPath()) ("neoeng-ev00-r15-ctest-parser-" + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    try {
        $modern = Join-Path $root 'modern.txt'
        $ambiguous = Join-Path $root 'ambiguous.txt'
        [IO.File]::WriteAllText($modern, "Start 1: a`nStart 2: b`n100% tests passed, 0 tests failed out of 2`n", [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($ambiguous, "Start 1: a`nStart 2: b`n98% tests passed out of 2`n", [Text.UTF8Encoding]::new($false))
        $modernParsed = Parse-CtestInventory $modern
        if ($modernParsed.percent -ne 100 -or $modernParsed.failed -ne 0 -or $modernParsed.total -ne 2 -or @($modernParsed.names).Count -ne 2) {
            throw 'R15 canonical parser rejected or misparsed modern explicit-failure CTest summary.'
        }
        $ambiguousAccepted = $false
        try { $null = Parse-CtestInventory $ambiguous; $ambiguousAccepted = $true } catch { $ambiguousAccepted = $false }
        if ($ambiguousAccepted) { throw 'R15 canonical parser accepted ambiguous historical summary without explicit failure count.' }
    } finally {
        Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Assert-DualSurfaceContract {
    param(
        [Parameter(Mandatory=$true)][string]$Runner,
        [Parameter(Mandatory=$true)][string]$Verifier
    )
    Import-CtestParser -Runner $Runner
    $normative = Join-Path $ControlRepo 'docs\changesets\015\evidence\github-actions-run-30375982639\raw\windows-clang-cl-ctest.txt'
    $supported = Parse-CtestInventory $normative
    foreach ($name in @('neoeng_dcore_replay_smoke','neoeng_dcore_history_smoke')) {
        if (@($supported.names) -contains $name) { throw "R15 normative 54-test surface unexpectedly contains research-only test: $name" }
    }

    $cmakeText = (& git -C $ControlRepo show "${ProductSha}:CMakeLists.txt" | Out-String)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($cmakeText)) { throw 'Unable to read protected baseline CMakeLists.txt for R15 contract audit.' }
    foreach ($token in @(
        'if(NEOENG_DCORE_BUILD_RESEARCH_TOOLS)',
        'neoeng_dcore_add_tool(neoeng_dcore_preclosure apps/v28_year1_preclosure.cpp)',
        'add_test(NAME neoeng_dcore_replay_smoke',
        'add_test(NAME neoeng_dcore_history_smoke',
        'neoeng_temporal_closure_tests'
    )) {
        if (-not $cmakeText.Contains($token)) { throw "Protected baseline lacks required R15 research contract token: $token" }
    }

    $runnerText = [IO.File]::ReadAllText($Runner)
    foreach ($token in @(
        '$ResearchBuildDir',
        "'-DNEOENG_DCORE_BUILD_RESEARCH_TOOLS=OFF'",
        "'-DNEOENG_DCORE_BUILD_RESEARCH_TOOLS=ON'",
        "-Name 'research-cmake-configure'",
        "-Name 'research-cmake-build'",
        "'neoeng_dcore_preclosure'",
        "'neoeng_temporal_closure_tests'",
        "'^(neoeng_dcore_replay_smoke|neoeng_dcore_history_smoke|neoeng_temporal_closure_tests)$'",
        "total -ne 3"
    )) {
        if (-not $runnerText.Contains($token)) { throw "R15 canonical runner lacks dual-surface contract token: $token" }
    }

    $verifierText = [IO.File]::ReadAllText($Verifier)
    foreach ($token in @(
        'EXPECTED_RESEARCH_TESTS',
        'research-cmake-configure',
        'research-cmake-build',
        'supported-surface configure is not research-OFF',
        'replay/history configure is not research-ON',
        'supported-surface CTest inventory differs from normative CS015 54-test inventory',
        'replay/history surface must be exactly 3/0 and exact names'
    )) {
        if (-not $verifierText.Contains($token)) { throw "R15 verifier lacks dual-surface contract token: $token" }
    }
}

Assert-MsvcEnvironment
Invoke-ToolchainLinkSmoke

$runner = Join-Path $PSScriptRoot 'run_ev00_dlab_windows.ps1'
$verifier = Join-Path $PSScriptRoot 'verify_ev00_dlab_evidence.py'
if (-not (Test-Path -LiteralPath $runner)) { throw "R15 EV-00 runner missing: $runner" }
if (-not (Test-Path -LiteralPath $verifier)) { throw "R15 EV-00 verifier missing: $verifier" }
Assert-CtestParserCompatibility -Runner $runner
Assert-DualSurfaceContract -Runner $runner -Verifier $verifier

& pwsh -NoProfile -File $runner -Mode $Mode -ControlRepo $ControlRepo -LabRoot $LabRoot
exit $LASTEXITCODE
