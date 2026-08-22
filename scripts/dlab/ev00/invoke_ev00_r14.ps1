param(
    [ValidateSet('Preflight','Qualify')]
    [string]$Mode = 'Preflight',
    [string]$ControlRepo = '',
    [string]$LabRoot = "$env:USERPROFILE\NeoEng-DLab\EV-00"
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'CS017 R14 wrapper requires PowerShell 7+.' }
if (-not $IsWindows) { throw 'CS017 R14 physical campaign requires Windows.' }

if ([string]::IsNullOrWhiteSpace($ControlRepo)) {
    $ControlRepo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
} else {
    $ControlRepo = (Resolve-Path $ControlRepo).Path
}

$planPath = 'audit/validation/CS017/VALIDATION_PLAN.json'
$head = (& git -C $ControlRepo rev-parse HEAD | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($head)) { throw 'Unable to identify control repository HEAD.' }
$planCommit = (& git -C $ControlRepo log -n 1 --format=%H HEAD -- $planPath | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($planCommit)) { throw 'Unable to derive canonical CS017 R14 plan commit.' }
if ($head -ne $planCommit) { throw "Control HEAD must equal canonical R14 plan commit. HEAD=$head plan=$planCommit" }

$branch = (& git -C $ControlRepo branch --show-current | Out-String).Trim()
if ([string]::IsNullOrWhiteSpace($branch)) { throw 'R14 requires a named local control branch; detached HEAD is rejected.' }
$status = (& git -C $ControlRepo status --porcelain | Out-String).Trim()
if (-not [string]::IsNullOrWhiteSpace($status)) { throw 'Control repository must be clean before R14 Preflight/Qualify.' }

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
        if ($null -eq (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Required tool not found for R14 compile-link smoke test: $tool" }
    }
    $root = Join-Path ([IO.Path]::GetTempPath()) ("neoeng-ev00-r14-link-smoke-" + [Guid]::NewGuid().ToString('N'))
    $source = Join-Path $root 'src'
    $build = Join-Path $root 'build'
    New-Item -ItemType Directory -Force -Path $source,$build | Out-Null
    try {
        [IO.File]::WriteAllText((Join-Path $source 'CMakeLists.txt'), "cmake_minimum_required(VERSION 3.20)`nproject(neoeng_ev00_r14_link_smoke C)`nadd_executable(neoeng_ev00_r14_link_smoke main.c)`n", [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText((Join-Path $source 'main.c'), "int main(void) { return 0; }`n", [Text.UTF8Encoding]::new($false))
        $configure = (& cmake -S $source -B $build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang-cl 2>&1 | Out-String)
        if ($LASTEXITCODE -ne 0) { throw "R14 CMake compile-link smoke configure failed:`n$configure" }
        $buildOutput = (& cmake --build $build --parallel 1 2>&1 | Out-String)
        if ($LASTEXITCODE -ne 0) { throw "R14 CMake compile-link smoke build failed:`n$buildOutput" }
        $exe = Get-ChildItem -Path $build -Filter 'neoeng_ev00_r14_link_smoke.exe' -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -eq $exe) { throw 'R14 compile-link smoke did not produce the expected executable.' }
    } finally {
        Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Assert-CtestParserCompatibility {
    param([Parameter(Mandatory=$true)][string]$Runner)
    $tokens = $null
    $parseErrors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile($Runner,[ref]$tokens,[ref]$parseErrors)
    if ($parseErrors.Count -ne 0) { throw 'R14 cannot validate CTest parser because canonical runner has syntax errors.' }
    $fn = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Parse-CtestInventory' }, $true)
    if ($null -eq $fn) { throw 'R14 canonical runner does not define Parse-CtestInventory.' }
    Invoke-Expression $fn.Extent.Text

    $normative = Join-Path $ControlRepo 'docs\changesets\015\evidence\github-actions-run-30375982639\raw\windows-clang-cl-ctest.txt'
    if (-not (Test-Path -LiteralPath $normative)) { throw "R14 normative CS015 Windows CTest reference missing: $normative" }
    $parsed = Parse-CtestInventory $normative
    if ($parsed.percent -ne 100 -or $parsed.failed -ne 0 -or $parsed.total -ne 54 -or @($parsed.names).Count -ne 54) {
        throw "R14 canonical parser did not resolve normative CS015 inventory as 100%/0/54/54-names."
    }

    $root = Join-Path ([IO.Path]::GetTempPath()) ("neoeng-ev00-r14-ctest-parser-" + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    try {
        $modern = Join-Path $root 'modern.txt'
        $ambiguous = Join-Path $root 'ambiguous.txt'
        [IO.File]::WriteAllText($modern, "Start 1: a`nStart 2: b`n100% tests passed, 0 tests failed out of 2`n", [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($ambiguous, "Start 1: a`nStart 2: b`n98% tests passed out of 2`n", [Text.UTF8Encoding]::new($false))
        $modernParsed = Parse-CtestInventory $modern
        if ($modernParsed.percent -ne 100 -or $modernParsed.failed -ne 0 -or $modernParsed.total -ne 2 -or @($modernParsed.names).Count -ne 2) {
            throw 'R14 canonical parser rejected or misparsed modern explicit-failure CTest summary.'
        }
        $ambiguousAccepted = $false
        try {
            $null = Parse-CtestInventory $ambiguous
            $ambiguousAccepted = $true
        } catch {
            $ambiguousAccepted = $false
        }
        if ($ambiguousAccepted) { throw 'R14 canonical parser accepted ambiguous historical summary without explicit failure count.' }
    } finally {
        Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Assert-MsvcEnvironment
Invoke-ToolchainLinkSmoke

$runner = Join-Path $PSScriptRoot 'run_ev00_dlab_windows.ps1'
if (-not (Test-Path -LiteralPath $runner)) { throw "R14 EV-00 runner missing: $runner" }
Assert-CtestParserCompatibility -Runner $runner

& pwsh -NoProfile -File $runner -Mode $Mode -ControlRepo $ControlRepo -LabRoot $LabRoot
exit $LASTEXITCODE
