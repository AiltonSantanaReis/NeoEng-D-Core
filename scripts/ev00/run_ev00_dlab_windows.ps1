param(
    [ValidateSet('Preflight','Qualify')]
    [string]$Mode = 'Preflight',
    [string]$ControlRepo = '',
    [string]$LabRoot = "$env:USERPROFILE\NeoEng-DLab\EV-00"
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProductSha = 'e3fff973554a2e56b8bd7afdc1132f75f3ec337c'
$BaselineTag = 'v1.14.1'
$PinnedVcpkg = '0878b5224d4a4968940ee296a2e7fae2d3b62983'
$Stage = 'EV-00'
$ChangeSet = 'CS017'
$Utf8NoBom = [System.Text.UTF8Encoding]::new($false)

if ($PSVersionTable.PSVersion.Major -lt 7) {
    throw 'EV-00 D-Lab harness requires PowerShell 7+ (pwsh) for deterministic process/UTF-8 handling.'
}
if (-not $IsWindows) {
    throw 'EV-00 qualifying laboratory requires physical Windows.'
}

if ([string]::IsNullOrWhiteSpace($ControlRepo)) {
    $ControlRepo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
} else {
    $ControlRepo = (Resolve-Path $ControlRepo).Path
}

function Write-Utf8NoBom {
    param([Parameter(Mandatory=$true)][string]$Path, [Parameter(Mandatory=$true)][AllowEmptyString()][string]$Text)
    [System.IO.File]::WriteAllText($Path, $Text, $script:Utf8NoBom)
}

function Write-JsonFile {
    param([Parameter(Mandatory=$true)]$Value, [Parameter(Mandatory=$true)][string]$Path)
    $json = ($Value | ConvertTo-Json -Depth 30) + "`n"
    Write-Utf8NoBom -Path $Path -Text $json
}

function Require-Tool {
    param([Parameter(Mandatory=$true)][string]$Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $cmd) {
        throw "Required tool not found in PATH: $Name"
    }
    return $cmd.Source
}

function Tool-Version {
    param([Parameter(Mandatory=$true)][string]$Name)
    try {
        $text = (& $Name --version 2>&1 | Out-String).Trim()
        if ([string]::IsNullOrWhiteSpace($text)) { return '<empty-version-output>' }
        return $text
    } catch {
        return "<version-query-failed: $($_.Exception.Message)>"
    }
}

function Invoke-RecordedCommand {
    param(
        [Parameter(Mandatory=$true)][string]$Name,
        [Parameter(Mandatory=$true)][string]$Executable,
        [Parameter(Mandatory=$true)][string[]]$Arguments,
        [Parameter(Mandatory=$true)][string]$WorkingDirectory,
        [Parameter(Mandatory=$true)][string]$EvidenceRoot
    )

    $commandsDir = Join-Path $EvidenceRoot 'raw\commands'
    $logsDir = Join-Path $EvidenceRoot 'raw\logs'
    New-Item -ItemType Directory -Force -Path $commandsDir,$logsDir | Out-Null

    $stdoutPath = Join-Path $logsDir "$Name.stdout.txt"
    $stderrPath = Join-Path $logsDir "$Name.stderr.txt"
    $started = [DateTime]::UtcNow.ToString('o')
    $exitCode = 9001
    $stdoutText = ''
    $stderrText = ''

    try {
        $psi = [System.Diagnostics.ProcessStartInfo]::new()
        $psi.FileName = $Executable
        $psi.WorkingDirectory = $WorkingDirectory
        $psi.UseShellExecute = $false
        $psi.CreateNoWindow = $true
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        foreach ($argument in $Arguments) {
            [void]$psi.ArgumentList.Add([string]$argument)
        }

        $process = [System.Diagnostics.Process]::new()
        $process.StartInfo = $psi
        if (-not $process.Start()) { throw "Unable to start process: $Executable" }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $stdoutText = $stdoutTask.GetAwaiter().GetResult()
        $stderrText = $stderrTask.GetAwaiter().GetResult()
        $exitCode = $process.ExitCode
        $process.Dispose()
    } catch {
        $stderrText += "`nPROCESS-LAUNCH-ERROR: $($_.Exception.Message)`n"
        $exitCode = 9001
    }

    Write-Utf8NoBom -Path $stdoutPath -Text $stdoutText
    Write-Utf8NoBom -Path $stderrPath -Text $stderrText

    $finished = [DateTime]::UtcNow.ToString('o')
    $record = [ordered]@{
        schema = 'neoeng.dlab.command-record.v1'
        name = $Name
        executable = $Executable
        arguments = @($Arguments)
        working_directory = $WorkingDirectory
        started_at_utc = $started
        finished_at_utc = $finished
        exit_code = [int]$exitCode
        classification = $(if ($exitCode -eq 0) { 'PASS' } else { 'FAIL' })
        stdout = ('raw/logs/' + [IO.Path]::GetFileName($stdoutPath))
        stderr = ('raw/logs/' + [IO.Path]::GetFileName($stderrPath))
    }
    Write-JsonFile $record (Join-Path $commandsDir "$Name.json")
    return $record
}

function Find-BuiltExecutable {
    param([Parameter(Mandatory=$true)][string]$BuildDir, [Parameter(Mandatory=$true)][string]$Name)
    $candidate = Get-ChildItem -Path $BuildDir -Filter "$Name.exe" -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $candidate) { throw "Required executable not found under build tree: $Name.exe" }
    return $candidate.FullName
}

function Parse-CtestInventory {
    param([Parameter(Mandatory=$true)][string]$Path)
    $text = [System.IO.File]::ReadAllText($Path)
    $summary = [regex]::Match($text, '(\d+)% tests passed,\s+(\d+) tests failed out of (\d+)')
    if (-not $summary.Success) { throw "Unable to parse CTest summary from $Path" }
    $names = New-Object System.Collections.Generic.HashSet[string]
    foreach ($match in [regex]::Matches($text, 'Test\s+#?\d+:\s+([A-Za-z0-9_.-]+)')) {
        [void]$names.Add($match.Groups[1].Value)
    }
    if ($names.Count -eq 0) {
        foreach ($match in [regex]::Matches($text, 'Start\s+\d+:\s+([A-Za-z0-9_.-]+)')) {
            [void]$names.Add($match.Groups[1].Value)
        }
    }
    return [ordered]@{
        percent = [int]$summary.Groups[1].Value
        failed = [int]$summary.Groups[2].Value
        total = [int]$summary.Groups[3].Value
        names = @($names | Sort-Object)
    }
}

function Write-EvidenceManifest {
    param([Parameter(Mandatory=$true)][string]$EvidenceRoot)
    $manifestPath = Join-Path $EvidenceRoot 'evidence-manifest.json'
    $rows = @()
    Get-ChildItem -Path $EvidenceRoot -File -Recurse | Where-Object { $_.FullName -ne $manifestPath } | Sort-Object FullName | ForEach-Object {
        $relative = $_.FullName.Substring($EvidenceRoot.Length).TrimStart('\').Replace('\','/')
        $rows += [ordered]@{
            path = $relative
            size = [int64]$_.Length
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant()
        }
    }
    if ($rows.Count -eq 0) { throw 'Evidence manifest would be empty.' }
    Write-JsonFile ([ordered]@{
        schema = 'neoeng.dlab.evidence-manifest.v1'
        algorithm = 'sha256'
        files = $rows
    }) $manifestPath
}

function Copy-QualificationPackage {
    param([string]$EvidenceRoot,[string]$RunId,[string]$ControlRepo)
    $destination = Join-Path $ControlRepo "docs\changesets\017\evidence\local-windows\$RunId"
    if (Test-Path $destination) { throw "Destination evidence package already exists: $destination" }
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Copy-Item -Path (Join-Path $EvidenceRoot '*') -Destination $destination -Recurse
    return $destination
}

function Get-PhysicalHostAssessment {
    $computer = Get-CimInstance Win32_ComputerSystem
    $joined = (($computer.Manufacturer + ' ' + $computer.Model).ToLowerInvariant())
    $markers = @('virtual','vmware','virtualbox','qemu','xen','hyper-v','kvm','parallels')
    $matches = @($markers | Where-Object { $joined.Contains($_) })
    return [ordered]@{
        physical_host = ($matches.Count -eq 0)
        virtualization_indicators = $matches
        manufacturer = $computer.Manufacturer
        model = $computer.Model
    }
}

function Run-Preflight {
    $required = @('git','python','cmake','ctest','ninja','clang-cl')
    $tools = [ordered]@{}
    foreach ($tool in $required) {
        [void](Require-Tool $tool)
        $tools[$tool] = Tool-Version $tool
    }
    $tools['link.exe'] = Require-Tool 'link.exe'
    $tools['rc.exe'] = Require-Tool 'rc.exe'

    $head = (& git -C $ControlRepo rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Unable to read control repository HEAD.' }
    $status = (& git -C $ControlRepo status --porcelain | Out-String).Trim()
    & git -C $ControlRepo cat-file -e "$ProductSha`^{commit}" 2>$null
    $productExists = ($LASTEXITCODE -eq 0)
    $hostAssessment = Get-PhysicalHostAssessment

    $result = [ordered]@{
        mode = 'Preflight'
        control_repo = $ControlRepo
        harness_head = $head
        powershell = $PSVersionTable.PSVersion.ToString()
        control_tree_clean = [string]::IsNullOrWhiteSpace($status)
        protected_product_commit_present = $productExists
        physical_host = $hostAssessment.physical_host
        virtualization_indicators = $hostAssessment.virtualization_indicators
        tools = $tools
        lab_root = $LabRoot
    }
    $result | ConvertTo-Json -Depth 10
    if (-not $result.control_tree_clean) { throw 'Control repository must be clean before a qualifying run.' }
    if (-not $productExists) { throw "Historical product commit is not available locally: $ProductSha. Fetch repository history before qualification." }
    if (-not $hostAssessment.physical_host) { throw "Virtualization indicator detected: $($hostAssessment.virtualization_indicators -join ', ')" }
}

if ($Mode -eq 'Preflight') {
    Run-Preflight
    exit 0
}

Run-Preflight | Out-Null

$HarnessSha = (& git -C $ControlRepo rev-parse HEAD).Trim()
$timestamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
$suffix = [Guid]::NewGuid().ToString('N').Substring(0,8)
$RunId = "ev00-$timestamp-$suffix"
$RunRoot = Join-Path $LabRoot "runs\$RunId"
$SourceDir = Join-Path $RunRoot 'source'
$BuildDir = Join-Path $RunRoot 'build'
$InstallDir = Join-Path $RunRoot 'install'
$DepsDir = Join-Path $RunRoot 'deps'
$EvidenceRoot = Join-Path $RunRoot 'evidence'
$VcpkgDir = Join-Path $DepsDir 'vcpkg'

if (Test-Path $RunRoot) { throw "Run workspace already exists: $RunRoot" }
New-Item -ItemType Directory -Force -Path $RunRoot,$BuildDir,$InstallDir,$DepsDir,$EvidenceRoot,(Join-Path $EvidenceRoot 'raw\commands'),(Join-Path $EvidenceRoot 'raw\logs') | Out-Null

$hostAssessment = Get-PhysicalHostAssessment
$processor = Get-CimInstance Win32_Processor | Select-Object -First 1
$computer = Get-CimInstance Win32_ComputerSystem
$video = Get-CimInstance Win32_VideoController | Select-Object Name,AdapterRAM,DriverVersion

$identity = [ordered]@{
    schema = 'neoeng.dlab.ev00-run-identity.v1'
    run_id = $RunId
    stage = $Stage
    changeset = $ChangeSet
    baseline_tag = $BaselineTag
    product_sha = $ProductSha
    harness_sha = $HarnessSha
    control_branch = (& git -C $ControlRepo branch --show-current).Trim()
    source_head = $null
    source_dirty = $null
    workspace_fresh = $true
    preexisting_build_used = $false
    created_at_utc = [DateTime]::UtcNow.ToString('o')
    run_root = $RunRoot
}
Write-JsonFile $identity (Join-Path $EvidenceRoot 'run-identity.json')

$environment = [ordered]@{
    schema = 'neoeng.dlab.ev00-environment.v1'
    collected_at_utc = [DateTime]::UtcNow.ToString('o')
    os_family = 'Windows'
    os_version = [System.Environment]::OSVersion.VersionString
    architecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    physical_host = $hostAssessment.physical_host
    virtualization_indicators = $hostAssessment.virtualization_indicators
    computer_manufacturer = $hostAssessment.manufacturer
    computer_model = $hostAssessment.model
    logical_processors = [Environment]::ProcessorCount
    cpu = $processor.Name
    physical_cores = $processor.NumberOfCores
    memory_bytes = [int64]$computer.TotalPhysicalMemory
    video_controllers = @($video)
    tools = [ordered]@{
        git = Tool-Version 'git'
        python = Tool-Version 'python'
        cmake = Tool-Version 'cmake'
        ctest = Tool-Version 'ctest'
        ninja = Tool-Version 'ninja'
        'clang-cl' = Tool-Version 'clang-cl'
        'link.exe' = Require-Tool 'link.exe'
        'rc.exe' = Require-Tool 'rc.exe'
    }
    vcpkg_commit_expected = $PinnedVcpkg
    vcpkg_commit_observed = $null
}
Write-JsonFile $environment (Join-Path $EvidenceRoot 'environment.json')

$terminalState = 'FAILED'
$terminalReason = 'qualification did not reach completion'

try {
    $sourceWorktree = Invoke-RecordedCommand -Name 'source-worktree' -Executable 'git' -Arguments @('-C',$ControlRepo,'worktree','add','--detach',$SourceDir,$ProductSha) -WorkingDirectory $ControlRepo -EvidenceRoot $EvidenceRoot
    if ($sourceWorktree.exit_code -ne 0) { throw 'Unable to create detached historical source worktree.' }

    $sourceHead = (& git -C $SourceDir rev-parse HEAD).Trim()
    $sourceStatus = (& git -C $SourceDir status --porcelain | Out-String).Trim()
    if ($sourceHead -ne $ProductSha) { throw "Historical source worktree SHA mismatch: $sourceHead" }
    if (-not [string]::IsNullOrWhiteSpace($sourceStatus)) { throw 'Historical source worktree is dirty before build.' }
    $identity['source_head'] = $sourceHead
    $identity['source_dirty'] = $false
    Write-JsonFile $identity (Join-Path $EvidenceRoot 'run-identity.json')

    New-Item -ItemType Directory -Force -Path $VcpkgDir | Out-Null
    $vcpkgInit = Invoke-RecordedCommand -Name 'vcpkg-init' -Executable 'git' -Arguments @('init') -WorkingDirectory $VcpkgDir -EvidenceRoot $EvidenceRoot
    if ($vcpkgInit.exit_code -ne 0) { throw 'vcpkg git init failed' }
    $remote = Invoke-RecordedCommand -Name 'vcpkg-remote' -Executable 'git' -Arguments @('remote','add','origin','https://github.com/microsoft/vcpkg.git') -WorkingDirectory $VcpkgDir -EvidenceRoot $EvidenceRoot
    if ($remote.exit_code -ne 0) { throw 'vcpkg remote setup failed' }
    $fetch = Invoke-RecordedCommand -Name 'vcpkg-fetch' -Executable 'git' -Arguments @('fetch','--depth','1','origin',$PinnedVcpkg) -WorkingDirectory $VcpkgDir -EvidenceRoot $EvidenceRoot
    if ($fetch.exit_code -ne 0) { throw 'pinned vcpkg fetch failed' }
    $checkout = Invoke-RecordedCommand -Name 'vcpkg-checkout' -Executable 'git' -Arguments @('checkout','--detach','FETCH_HEAD') -WorkingDirectory $VcpkgDir -EvidenceRoot $EvidenceRoot
    if ($checkout.exit_code -ne 0) { throw 'pinned vcpkg checkout failed' }
    $vcpkgHead = Invoke-RecordedCommand -Name 'vcpkg-head' -Executable 'git' -Arguments @('rev-parse','HEAD') -WorkingDirectory $VcpkgDir -EvidenceRoot $EvidenceRoot
    if ($vcpkgHead.exit_code -ne 0) { throw 'unable to identify pinned vcpkg checkout' }
    $observedVcpkg = ([System.IO.File]::ReadAllText((Join-Path $EvidenceRoot $vcpkgHead.stdout))).Trim()
    if ($observedVcpkg -ne $PinnedVcpkg) { throw "vcpkg SHA mismatch: $observedVcpkg" }
    $environment['vcpkg_commit_observed'] = $observedVcpkg
    Write-JsonFile $environment (Join-Path $EvidenceRoot 'environment.json')

    $bootstrapPath = Join-Path $VcpkgDir 'bootstrap-vcpkg.bat'
    $bootstrapCommand = '""' + $bootstrapPath + '" -disableMetrics"'
    $bootstrap = Invoke-RecordedCommand -Name 'vcpkg-bootstrap' -Executable 'cmd.exe' -Arguments @('/d','/s','/c',$bootstrapCommand) -WorkingDirectory $VcpkgDir -EvidenceRoot $EvidenceRoot
    if ($bootstrap.exit_code -ne 0) { throw 'vcpkg bootstrap failed' }

    $toolchain = Join-Path $VcpkgDir 'scripts\buildsystems\vcpkg.cmake'
    $configureArgs = @(
        '-S',$SourceDir,'-B',$BuildDir,'-G','Ninja',
        '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_C_COMPILER=clang-cl',
        '-DCMAKE_CXX_COMPILER=clang-cl',
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
        '-DNEOENG_DCORE_BUILD_FULL_TOOLSET=OFF',
        '-DNEOENG_DCORE_BUILD_RELEASE_TOOLS=ON',
        '-DNEOENG_DCORE_BUILD_RESEARCH_TOOLS=OFF',
        '-DNEOENG_DCORE_BUILD_VIEW_LAB=OFF'
    )
    $configure = Invoke-RecordedCommand -Name 'cmake-configure' -Executable 'cmake' -Arguments $configureArgs -WorkingDirectory $RunRoot -EvidenceRoot $EvidenceRoot
    if ($configure.exit_code -ne 0) { throw 'CMake configure failed' }

    $build = Invoke-RecordedCommand -Name 'cmake-build' -Executable 'cmake' -Arguments @('--build',$BuildDir,'--parallel','2') -WorkingDirectory $RunRoot -EvidenceRoot $EvidenceRoot
    if ($build.exit_code -ne 0) { throw 'CMake build failed' }

    $ctest = Invoke-RecordedCommand -Name 'ctest-dcore' -Executable 'ctest' -Arguments @('--test-dir',$BuildDir,'-C','Release','--output-on-failure','-L','dcore') -WorkingDirectory $RunRoot -EvidenceRoot $EvidenceRoot
    if ($ctest.exit_code -ne 0) { throw 'Supported D-Core CTest surface failed' }

    $determinism = Find-BuiltExecutable $BuildDir 'neoeng_determinism_probe'
    $det1 = Invoke-RecordedCommand -Name 'determinism-1' -Executable $determinism -Arguments @() -WorkingDirectory $BuildDir -EvidenceRoot $EvidenceRoot
    $det2 = Invoke-RecordedCommand -Name 'determinism-2' -Executable $determinism -Arguments @() -WorkingDirectory $BuildDir -EvidenceRoot $EvidenceRoot
    if ($det1.exit_code -ne 0 -or $det2.exit_code -ne 0) { throw 'Determinism probe failed' }
    $detHash1 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $EvidenceRoot $det1.stdout)).Hash.ToLowerInvariant()
    $detHash2 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $EvidenceRoot $det2.stdout)).Hash.ToLowerInvariant()
    if ($detHash1 -ne $detHash2) { throw 'Repeated determinism probe outputs differ' }

    $hostSdk = Invoke-RecordedCommand -Name 'ctest-host-sdk' -Executable 'ctest' -Arguments @('--test-dir',$BuildDir,'-C','Release','--output-on-failure','-L','host-sdk') -WorkingDirectory $RunRoot -EvidenceRoot $EvidenceRoot
    if ($hostSdk.exit_code -ne 0) { throw 'Host SDK boundary failed' }

    $replay = Invoke-RecordedCommand -Name 'ctest-replay-rollback' -Executable 'ctest' -Arguments @('--test-dir',$BuildDir,'-C','Release','--output-on-failure','-R','neoeng_dcore_replay_smoke|neoeng_dcore_history_smoke|neoeng_temporal_closure_tests') -WorkingDirectory $RunRoot -EvidenceRoot $EvidenceRoot
    if ($replay.exit_code -ne 0) { throw 'Replay/rollback surface failed' }

    $stateProbe = Find-BuiltExecutable $BuildDir 'neoeng_state_evidence_probe'
    $stateEvidence = Invoke-RecordedCommand -Name 'state-evidence-probe' -Executable $stateProbe -Arguments @() -WorkingDirectory $BuildDir -EvidenceRoot $EvidenceRoot
    if ($stateEvidence.exit_code -ne 0) { throw 'State evidence probe failed' }

    $supportProbe = Find-BuiltExecutable $BuildDir 'neoeng_support_bundle_probe'
    $supportBundle = Invoke-RecordedCommand -Name 'support-bundle-probe' -Executable $supportProbe -Arguments @() -WorkingDirectory $BuildDir -EvidenceRoot $EvidenceRoot
    if ($supportBundle.exit_code -ne 0) { throw 'Support bundle probe failed' }

    $releaseGate = Invoke-RecordedCommand -Name 'release-gate' -Executable 'ctest' -Arguments @('--test-dir',$BuildDir,'-C','Release','--output-on-failure','-L','release-gate') -WorkingDirectory $RunRoot -EvidenceRoot $EvidenceRoot
    if ($releaseGate.exit_code -ne 0) { throw 'Historical release-gate revalidation failed' }

    $localCtest = Parse-CtestInventory (Join-Path $EvidenceRoot $ctest.stdout)
    $historicalPath = Join-Path $ControlRepo 'docs\changesets\015\evidence\windows-x86_64-clang-20260810\raw\ctest-output.txt'
    if (-not (Test-Path $historicalPath)) { throw "Historical accepted CTest reference missing: $historicalPath" }
    $historicalCtest = Parse-CtestInventory $historicalPath
    $inventoryEqual = ($localCtest.total -eq $historicalCtest.total) -and (($localCtest.names -join "`n") -eq ($historicalCtest.names -join "`n"))
    Write-JsonFile ([ordered]@{
        schema = 'neoeng.dlab.ev00-historical-comparison.v1'
        historical_reference = 'docs/changesets/015/evidence/windows-x86_64-clang-20260810/raw/ctest-output.txt'
        historical_total_tests = $historicalCtest.total
        local_total_tests = $localCtest.total
        historical_failed_tests = $historicalCtest.failed
        local_failed_tests = $localCtest.failed
        inventory_equal = $inventoryEqual
        compared_at_utc = [DateTime]::UtcNow.ToString('o')
    }) (Join-Path $EvidenceRoot 'historical-comparison.json')
    if (-not $inventoryEqual -or $localCtest.failed -ne 0 -or $historicalCtest.failed -ne 0) {
        throw 'Local CTest inventory/results differ from accepted historical reference'
    }

    $terminalState = 'PASSED'
    $terminalReason = 'all local EV-00 qualifying commands and local historical comparison passed'
} catch {
    $terminalState = 'FAILED'
    $terminalReason = $_.Exception.Message
} finally {
    if (-not (Test-Path (Join-Path $EvidenceRoot 'historical-comparison.json'))) {
        Write-JsonFile ([ordered]@{
            schema = 'neoeng.dlab.ev00-historical-comparison.v1'
            historical_reference = 'docs/changesets/015/evidence/windows-x86_64-clang-20260810/raw/ctest-output.txt'
            historical_total_tests = $null
            local_total_tests = $null
            historical_failed_tests = $null
            local_failed_tests = $null
            inventory_equal = $false
            compared_at_utc = [DateTime]::UtcNow.ToString('o')
            note = 'comparison unavailable because the run terminated earlier'
        }) (Join-Path $EvidenceRoot 'historical-comparison.json')
    }
    Write-JsonFile ([ordered]@{
        schema = 'neoeng.dlab.ev00-terminal-state.v1'
        state = $terminalState
        reason = $terminalReason
        finished_at_utc = [DateTime]::UtcNow.ToString('o')
    }) (Join-Path $EvidenceRoot 'terminal-state.json')
    Write-EvidenceManifest $EvidenceRoot
}

$published = Copy-QualificationPackage -EvidenceRoot $EvidenceRoot -RunId $RunId -ControlRepo $ControlRepo
Write-Host "EV-00 local D-Lab run: $RunId"
Write-Host "Terminal state: $terminalState"
Write-Host "Archived workspace: $RunRoot"
Write-Host "Repository evidence package: $published"
Write-Host 'No result is accepted until the independent verifier and Trusted ChangeSet validation gate pass.'

if ($terminalState -ne 'PASSED') { exit 1 }
exit 0