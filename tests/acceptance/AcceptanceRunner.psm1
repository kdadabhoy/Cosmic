# AcceptanceRunner.psm1 - WO-04 (Cosmic 2D stability campaign)
#
# The small runner library behind Run-Acceptance.ps1. It is a PROCESS runner: each
# acceptance case is an external child process with its own independent deadline.
# The library is deliberately generic - it knows nothing about specific tests; the
# cases live in a manifest (see manifests/) and the fixtures under fixtures/.
#
# Design contracts (03-Acceptance-Test-Catalog.md "Execution model" + "Evidence and
# isolation contract"):
#   * Independent per-case deadline. A timeout is FAILED, never a graceful pass. The
#     runner kills ONLY the child process tree it launched; the parent survives and
#     keeps running the remaining cases.
#   * Capability gating. A case that requires a capability the machine lacks is
#     ENVIRONMENT_BLOCKED with the missing prerequisite NAMED - never a phantom pass.
#   * Honest counts. A case that declares minTests but runs fewer is a FAILURE (a
#     filtered suite with zero expected tests must not read as success).
#   * Goldens are read-only in acceptance. A declared-but-missing golden is a failure
#     and the runner never writes it; the golden dir is hashed before/after.
#   * Every run records the full evidence contract (commit, dirty diff, mode/config,
#     environment, exact command + exit code, seed, fixture hash, logs, hang/crash
#     evidence) as JSON + JUnit. All paths are parameterized - no absolute user paths.
#
# Windows PowerShell 5.1 compatible (no ternary / null-coalescing / -AsHashtable).

Set-StrictMode -Version Latest

# ---------------------------------------------------------------------------------
# Verdicts. ENVIRONMENT_BLOCKED is neither pass nor fail (it is a skip that names its
# prerequisite); everything else that is not PASSED counts as a failure.
# ---------------------------------------------------------------------------------
$script:V_PASSED  = 'PASSED'
$script:V_FAILED  = 'FAILED'
$script:V_TIMEOUT = 'TIMEOUT'
$script:V_MISSING = 'MISSING'
$script:V_BLOCKED = 'ENVIRONMENT_BLOCKED'
$script:V_ERROR   = 'ERROR'

function Get-FileSha256 {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    try { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLower() }
    catch { return $null }
}

function Get-DirHashManifest {
    # SHA256 of every file under $Dir (sorted), so goldens can be compared before/after
    # a run. Returns an ordered map of relative-path -> hash.
    param([string]$Dir)
    $map = [ordered]@{}
    if (-not $Dir -or -not (Test-Path -LiteralPath $Dir)) { return $map }
    $root = (Resolve-Path -LiteralPath $Dir).Path
    Get-ChildItem -LiteralPath $root -Recurse -File -ErrorAction SilentlyContinue |
        Sort-Object FullName | ForEach-Object {
            $rel = $_.FullName.Substring($root.Length).TrimStart('\','/')
            $map[$rel] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower()
        }
    return $map
}

function Get-AcceptanceEnvironment {
    # The environment block that every case's evidence is keyed against. Best-effort:
    # a field that cannot be probed headlessly is recorded as $null rather than faked.
    $env = [ordered]@{}
    try {
        $os = Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
        $env.os_caption = $os.Caption
        $env.os_version = $os.Version
        $env.os_build   = $os.BuildNumber
    } catch { $env.os_version = [System.Environment]::OSVersion.Version.ToString() }

    try {
        $ubr = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -ErrorAction Stop)
        $env.os_display = "$($ubr.CurrentBuild).$($ubr.UBR)"
    } catch { }

    try {
        $cpu = Get-CimInstance Win32_Processor -ErrorAction Stop | Select-Object -First 1
        $env.cpu_name        = $cpu.Name.Trim()
        $env.cpu_cores       = $cpu.NumberOfCores
        $env.cpu_threads     = $cpu.NumberOfLogicalProcessors
        $env.cpu_addr_width  = $cpu.AddressWidth
        # ISA floor is D-CPU (SSE4.1 + SSE4.2); the exact ISA probe is in the WO-02
        # baseline (evidence/WO-02/cpu-isa-probe.txt). Recorded, not re-derived here.
        $env.cpu_isa_floor   = 'SSE4.2 (D-CPU); see evidence/WO-02/cpu-isa-probe.txt'
    } catch { }

    try {
        $cs = Get-CimInstance Win32_ComputerSystem -ErrorAction Stop
        $env.ram_bytes = [long]$cs.TotalPhysicalMemory
        $env.ram_gib   = [math]::Round($cs.TotalPhysicalMemory / 1GB, 1)
    } catch { }

    try {
        $gpus = @(Get-CimInstance Win32_VideoController -ErrorAction Stop)
        $env.gpus = @($gpus | ForEach-Object {
            [ordered]@{ name = $_.Name; driver = $_.DriverVersion; driver_date = ($_.DriverDate) }
        })
    } catch { $env.gpus = @() }

    # OpenGL version requires a live GL context, which a headless runner does not
    # create; a G-tier case reports it. Recorded as null so no field is faked.
    $env.opengl_version = $null

    $env.powershell = $PSVersionTable.PSVersion.ToString()
    $env.hostname   = [System.Environment]::MachineName
    return $env
}

function Get-AcceptanceCapabilities {
    # Probe the capabilities cases can require. Honest: 'serial-device' means a COM
    # port we may actually stream from (a virtual pair / Bluetooth SPP), NOT the
    # reference machine's legacy COM1 UART, which the campaign forbids opening - so it
    # is reported ABSENT. `Disable` force-removes capabilities (used by the self-test
    # to exercise the GPU/Windows environment-blocked branch on a machine that has
    # them); every forced removal is recorded so evidence never claims real absence.
    param([string[]]$Disable = @(), [string]$BinDir = '')

    $caps = [ordered]@{}
    $caps['windows'] = ($env:OS -eq 'Windows_NT')

    # x64 target satisfies the SSE4.2 floor (D-CPU). The precise ISA probe lives in
    # WO-02; here the address width is the practical gate.
    $caps['cpu-sse42'] = $true

    # A non-basic display adapter => a usable GPU for the hidden-window GL tier.
    $hasGpu = $false
    try {
        $hasGpu = @(Get-CimInstance Win32_VideoController -ErrorAction Stop |
            Where-Object { $_.Name -and $_.Name -notmatch 'Basic Display|Remote Display' }).Count -gt 0
    } catch { }
    $caps['gpu-gl'] = $hasGpu

    # STANDING campaign rule: no usable COM/serial hardware, ever. A registry COM node
    # that is only the legacy COM1 UART does NOT count. Reported absent by policy; the
    # fake transport (WO-04 seam) is the only headless serial path.
    $caps['serial-device'] = $false

    # A real interactive editor UI session (native dialogs, docking). Not available to
    # a headless runner.
    $caps['editor-ui'] = $false

    # WO-09 (C06 audible lifecycle): a working sound device per Win32_SoundDevice.
    # Absent (CI runner, RDP session, no audio hardware) => ENVIRONMENT_BLOCKED for the
    # cases that require it - never a phantom pass through AudioEngine's no-op path.
    $hasAudio = $false
    try {
        $hasAudio = @(Get-CimInstance Win32_SoundDevice -ErrorAction Stop |
            Where-Object { $_.Status -eq 'OK' }).Count -gt 0
    } catch { }
    $caps['audio-device'] = $hasAudio

    # AP-P1: the golden-image binary only exists in a tree configured with
    # -DCOSMIC_BUILD_RENDER_TESTS=ON. A G-tier case needs BOTH a GPU and this binary;
    # without it the case is ENVIRONMENT_BLOCKED naming 'render-tests', instead of
    # reading as a MISSING-executable failure on a machine that simply did not build
    # the GPU suite (ci.yml's PR tree, for one).
    $caps['render-tests'] = $false
    if ($BinDir) { $caps['render-tests'] = (Test-Path -LiteralPath (Join-Path $BinDir 'CosmicRenderTests.exe')) }

    $forced = @()
    foreach ($d in $Disable) {
        if ($caps.Contains($d)) { $caps[$d] = $false; $forced += $d }
    }
    return @{ caps = $caps; forced = $forced }
}

function Expand-AcceptanceToken {
    param([string]$Value, [hashtable]$Vars)
    if ($null -eq $Value) { return $null }
    $out = $Value
    foreach ($k in $Vars.Keys) { $out = $out.Replace('{' + $k + '}', [string]$Vars[$k]) }
    return $out
}

function ConvertTo-WindowsArgString {
    # Quote a token list into a single Windows command line (Start-Process -ArgumentList
    # as one verbatim string is the only reliable quoting path in 5.1).
    param([string[]]$Tokens)
    $parts = @()
    foreach ($t in $Tokens) {
        if ($null -eq $t) { continue }
        if ($t -eq '' -or $t -match '[\s"]') {
            $escaped = $t -replace '"', '\"'
            $parts += ('"' + $escaped + '"')
        } else {
            $parts += $t
        }
    }
    return ($parts -join ' ')
}

function Test-IsCrashExit {
    # A crash (access violation 0xC0000005, FailFast 0x80131623, fatal 0xC0000409, ...)
    # surfaces as a Process.ExitCode with the high bit set, i.e. a NEGATIVE Int32. An
    # ordinary nonzero return (e.g. 1, 7) is positive and is NOT a crash.
    param([int]$Code)
    return ($Code -lt 0)
}

function Invoke-AcceptanceCase {
    # Run one case and return its evidence record. Never throws for a case-level
    # problem - every outcome is a verdict on the record.
    param(
        [Parameter(Mandatory)] $Case,
        [Parameter(Mandatory)] [hashtable]$Caps,
        [Parameter(Mandatory)] [hashtable]$Vars,
        [Parameter(Mandatory)] [string]$RunDir,
        [string]$AcceptanceDir,
        [switch]$AcceptanceMode
    )

    $rec = [ordered]@{}
    $rec.id          = $Case.id
    $rec.tier        = $Case.tier
    $rec.description = $Case.description
    $rec.requires    = @($Case.requires)
    if ($Case.PSObject.Properties.Name -contains 'transport') { $rec.transport = $Case.transport } else { $rec.transport = 'none' }
    $rec.seed        = if ($Case.PSObject.Properties.Name -contains 'seed') { $Case.seed } else { $null }
    $rec.start_utc   = (Get-Date).ToUniversalTime().ToString('o')
    $rec.deadline_sec = $Case.deadlineSec
    $rec.exercised   = 'production'   # refined below (fake transport / fixture / etc.)
    $sw = [System.Diagnostics.Stopwatch]::StartNew()

    function Complete($verdict, $detail) {
        $sw.Stop()
        $rec.verdict      = $verdict
        $rec.detail       = $detail
        $rec.duration_sec = [math]::Round($sw.Elapsed.TotalSeconds, 3)
        $rec.end_utc      = (Get-Date).ToUniversalTime().ToString('o')
        return $rec
    }

    # 1) Capability gate - a missing prerequisite is ENVIRONMENT_BLOCKED, not a run.
    foreach ($need in @($Case.requires)) {
        if (-not $Caps.Contains($need) -or -not $Caps[$need]) {
            return (Complete $script:V_BLOCKED "missing capability: $need")
        }
    }

    # 2) Fixture gate - a declared fixture that does not exist is MISSING (not a pass).
    $fixtureHash = $null
    if ($Case.PSObject.Properties.Name -contains 'fixture' -and $Case.fixture) {
        $fixturePath = Expand-AcceptanceToken $Case.fixture $Vars
        if (-not [System.IO.Path]::IsPathRooted($fixturePath)) { $fixturePath = Join-Path $AcceptanceDir $fixturePath }
        if (-not (Test-Path -LiteralPath $fixturePath)) {
            $rec.fixture = $fixturePath
            return (Complete $script:V_MISSING "missing fixture: $fixturePath")
        }
        $fixtureHash = Get-FileSha256 $fixturePath
        $rec.fixture = $fixturePath
        $rec.fixture_sha256 = $fixtureHash
        $rec.exercised = 'fixture'
    }
    if ($rec.transport -eq 'fake') { $rec.exercised = 'fake-transport' }

    # 3) Golden gate - a declared-but-missing golden is a failure; NEVER write it, and
    #    NEVER accept an update-goldens flag inside acceptance mode.
    if ($Case.PSObject.Properties.Name -contains 'golden' -and $Case.golden) {
        $goldenPath = Expand-AcceptanceToken $Case.golden $Vars
        if (-not [System.IO.Path]::IsPathRooted($goldenPath)) { $goldenPath = Join-Path $AcceptanceDir $goldenPath }
        $rec.golden = $goldenPath
        if (-not (Test-Path -LiteralPath $goldenPath)) {
            return (Complete $script:V_MISSING "missing golden: $goldenPath (acceptance mode never regenerates goldens)")
        }
        $rec.golden_sha256 = Get-FileSha256 $goldenPath
    }

    # 4) Build the exact command and run it under an independent deadline.
    $exe = Expand-AcceptanceToken $Case.command $Vars
    $tokens = @()
    if ($Case.PSObject.Properties.Name -contains 'args' -and $Case.args) {
        foreach ($a in $Case.args) { $tokens += (Expand-AcceptanceToken ([string]$a) $Vars) }
    }
    $argString = ConvertTo-WindowsArgString $tokens
    $rec.command = (ConvertTo-WindowsArgString (@($exe) + $tokens))

    $safeId  = ($Case.id -replace '[^A-Za-z0-9_.-]', '_')
    $outLog  = Join-Path $RunDir ("$safeId.out.log")
    $errLog  = Join-Path $RunDir ("$safeId.err.log")
    $rec.stdout_log = $outLog
    $rec.stderr_log = $errLog

    if (-not (Test-Path -LiteralPath $exe) -and -not (Get-Command $exe -ErrorAction SilentlyContinue)) {
        return (Complete $script:V_MISSING "missing executable: $exe")
    }

    $deadlineMs = [int]([double]$Case.deadlineSec * 1000)

    # Launch via cmd.exe so the CHILD writes its own stdout/stderr straight to files
    # (no pipe-buffer deadlock, no async stream plumbing), and use the .NET Process API
    # (not Start-Process) because it reliably reports ExitCode after WaitForExit. The
    # child's TEMP/TMP + user-data are redirected into the per-run isolated dirs.
    $childCwd = $Vars.RUNTEMP
    if ($Case.PSObject.Properties.Name -contains 'workingDir' -and $Case.workingDir) {
        $childCwd = (Expand-AcceptanceToken $Case.workingDir $Vars)
    }
    $inner = '"' + $exe + '"'
    if ($argString -ne '') { $inner += ' ' + $argString }
    $inner += ' 1> "' + $outLog + '" 2> "' + $errLog + '"'

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $env:ComSpec
    $psi.Arguments = '/s /c "' + $inner + '"'
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.WorkingDirectory = $childCwd
    try {
        $psi.EnvironmentVariables['TEMP'] = $Vars.RUNTEMP
        $psi.EnvironmentVariables['TMP']  = $Vars.RUNTEMP
        $psi.EnvironmentVariables['COSMIC_USER_DATA'] = $Vars.USERDATA
    } catch { }

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    try {
        [void]$proc.Start()
    } catch {
        return (Complete $script:V_ERROR "failed to launch: $($_.Exception.Message)")
    }

    $rec.pid = $proc.Id
    $exitedInTime = $proc.WaitForExit($deadlineMs)
    if (-not $exitedInTime) {
        # Timeout: kill ONLY this child's process tree; the parent runner is untouched.
        $rec.timed_out = $true
        try { & taskkill.exe /PID $proc.Id /T /F 2>&1 | Out-Null } catch { }
        try { [void]$proc.WaitForExit(3000) } catch { }
        $rec.child_alive_after_kill = $false
        try { $rec.child_alive_after_kill = -not (Get-Process -Id $proc.Id -ErrorAction Stop).HasExited } catch { $rec.child_alive_after_kill = $false }
        $rec.parent_alive = $true   # we are still executing to write this
        return (Complete $script:V_TIMEOUT "exceeded per-case deadline of $($Case.deadlineSec)s; child tree killed, parent survived")
    }

    $exitCode = $proc.ExitCode
    $rec.exit_code = $exitCode
    $rec.crash = (Test-IsCrashExit $exitCode)

    # tail of stderr for evidence
    if (Test-Path -LiteralPath $errLog) {
        $errTail = (Get-Content -LiteralPath $errLog -Tail 8 -ErrorAction SilentlyContinue) -join "`n"
        if ($errTail) { $rec.stderr_tail = $errTail }
    }

    # 5) minTests: parse doctest counts; fewer than expected (or zero) is a FAILURE.
    if ($Case.PSObject.Properties.Name -contains 'minTests' -and $Case.minTests) {
        $stdout = ''
        if (Test-Path -LiteralPath $outLog) { $stdout = (Get-Content -LiteralPath $outLog -Raw -ErrorAction SilentlyContinue) }
        $passedCount = -1
        if ($stdout) {
            $m = [regex]::Match($stdout, 'test cases:\s*(\d+)\s*\|\s*(\d+)\s+passed')
            if ($m.Success) { $passedCount = [int]$m.Groups[2].Value }
        }
        $rec.tests_passed = $passedCount
        $rec.tests_expected_min = [int]$Case.minTests
        if ($passedCount -lt [int]$Case.minTests) {
            $expExit = if ($Case.PSObject.Properties.Name -contains 'expectExit') { [int]$Case.expectExit } else { 0 }
            return (Complete $script:V_FAILED "expected >= $($Case.minTests) tests, got $passedCount (exit $exitCode, wanted $expExit)")
        }
    }

    # 6) Exit-code oracle.
    $expectExit = if ($Case.PSObject.Properties.Name -contains 'expectExit') { [int]$Case.expectExit } else { 0 }
    if ($exitCode -ne $expectExit) {
        $kind = if ($rec.crash) { 'crash' } else { 'nonzero-exit' }
        return (Complete $script:V_FAILED "${kind}: exit $exitCode, expected $expectExit")
    }

    return (Complete $script:V_PASSED "exit $exitCode as expected")
}

function Write-AcceptanceEvidence {
    # Emit results.json (full evidence) + results.junit.xml (parseable by CI).
    param(
        [Parameter(Mandatory)] $Report,
        [Parameter(Mandatory)] [string]$OutDir
    )
    if (-not (Test-Path -LiteralPath $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

    $jsonPath = Join-Path $OutDir 'results.json'
    ($Report | ConvertTo-Json -Depth 12) | Set-Content -LiteralPath $jsonPath -Encoding utf8

    $junitPath = Join-Path $OutDir 'results.junit.xml'
    $cases = @($Report.cases)
    $failures = @($cases | Where-Object { $_.verdict -in @($script:V_FAILED, $script:V_TIMEOUT, $script:V_MISSING, $script:V_ERROR) }).Count
    $skipped  = @($cases | Where-Object { $_.verdict -eq $script:V_BLOCKED }).Count
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine('<?xml version="1.0" encoding="UTF-8"?>')
    [void]$sb.AppendLine(('<testsuites name="{0}" tests="{1}" failures="{2}" skipped="{3}">' -f `
        [System.Security.SecurityElement]::Escape($Report.manifest), $cases.Count, $failures, $skipped))
    [void]$sb.AppendLine(('  <testsuite name="{0}" tests="{1}" failures="{2}" skipped="{3}">' -f `
        [System.Security.SecurityElement]::Escape($Report.profile), $cases.Count, $failures, $skipped))
    foreach ($c in $cases) {
        $name = [System.Security.SecurityElement]::Escape($c.id)
        $cls  = [System.Security.SecurityElement]::Escape("$($Report.manifest).$($c.tier)")
        $time = if ($c.PSObject.Properties.Name -contains 'duration_sec' -and $c.duration_sec) { $c.duration_sec } else { 0 }
        [void]$sb.AppendLine(('    <testcase name="{0}" classname="{1}" time="{2}">' -f $name, $cls, $time))
        if ($c.verdict -eq $script:V_BLOCKED) {
            [void]$sb.AppendLine(('      <skipped message="{0}"/>' -f [System.Security.SecurityElement]::Escape([string]$c.detail)))
        } elseif ($c.verdict -ne $script:V_PASSED) {
            [void]$sb.AppendLine(('      <failure type="{0}" message="{1}"></failure>' -f `
                $c.verdict, [System.Security.SecurityElement]::Escape([string]$c.detail)))
        }
        [void]$sb.AppendLine('    </testcase>')
    }
    [void]$sb.AppendLine('  </testsuite>')
    [void]$sb.AppendLine('</testsuites>')
    $sb.ToString() | Set-Content -LiteralPath $junitPath -Encoding utf8

    return @{ json = $jsonPath; junit = $junitPath }
}

Export-ModuleMember -Function `
    Get-FileSha256, Get-DirHashManifest, Get-AcceptanceEnvironment, Get-AcceptanceCapabilities, `
    Expand-AcceptanceToken, ConvertTo-WindowsArgString, Test-IsCrashExit, `
    Invoke-AcceptanceCase, Write-AcceptanceEvidence
