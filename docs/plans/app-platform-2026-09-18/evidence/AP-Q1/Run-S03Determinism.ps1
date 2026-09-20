# S03 (stability catalog): repeat the deterministic logical fixtures 5 times with the same
# seed/input sequence, each repetition a NEW process, and compare the run-to-run checksums.
#
# Fixtures (all pure U-tier, in-exe, real code paths):
#   F-PENDULUM  "AP-04 Y01: two runs publish a bit-identical angle series"  - the FNV-1a of the
#               2401-sample RK4 angle series (doctest -s exposes the compared values)
#   F-PENDULUM  "AP-04 Y01: undamped ... matches F-PENDULUM within 1e-4"   - pass + assertion count
#   F02         "AP-04 F02: a scripted 200-step sequence gives the same state trace on two runs"
#               - the compared trace strings (doctest -s)
#   N04         "WO-10 N04*" - n04-hashes.txt (gaussian / transcendental stream hashes) + the
#               sha256 of the three n04-*.bin streams written per run
#   V01         "V01*" DataBus suite - pass + assertion count (integer/state checks)
#   D01-D03     "WO-06 D01*/D02*/D03*" - storage / interpolation / F-CORRUPT seed-6 fixtures, pass +
#               assertion count (D04 concurrency and D05/D06 real-clock/OS cases are not logical fixtures)
# Oracle: for every fixture the per-run checksum string is identical across the 5 runs, every run
# passes, and the assertion counts are identical. Writes s03-<Config>/run-<n>/... and
# s03-<Config>/summary.json; exits 1 on any variation or failure.
param([Parameter(Mandatory=$true)][string]$Config, [int]$Runs = 5)
$ErrorActionPreference = 'Continue'
$repo = 'C:\dev\Cosmic'
$exe = Join-Path $repo ("build\Runtime\" + $Config + "\CosmicTests.exe")
$out = Join-Path $repo ("docs\plans\app-platform-2026-09-18\evidence\AP-Q1\s03-" + $Config)
if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
New-Item -ItemType Directory -Force -Path $out | Out-Null
$fixtures = @(
    @{ name = 'F-PENDULUM-series-hash'; tc = 'AP-04 Y01: two runs*';              values = $true  },
    @{ name = 'F-PENDULUM-analytic';    tc = 'AP-04 Y01: undamped*';              values = $false },
    @{ name = 'F-PENDULUM-period';      tc = 'AP-04 Y01: the zero-crossing*';     values = $false },
    @{ name = 'F02-200-step-trace';     tc = 'AP-04 F02: a scripted 200-step*';   values = $true  },
    @{ name = 'N04-determinism';        tc = 'WO-10 N04*';                         values = $false; n04 = $true },
    @{ name = 'V01-DataBus';            tc = 'V01*';                               values = $false },
    @{ name = 'WO06-D01-storage';       tc = 'WO-06 D01*';                         values = $false },
    @{ name = 'WO06-D02-interpolation'; tc = 'WO-06 D02*';                         values = $false },
    @{ name = 'WO06-D03-corrupt-seed6'; tc = 'WO-06 D03*';                         values = $false }
)
function Sha([string]$text) {
    $sha = [Security.Cryptography.SHA256]::Create()
    $bytes = [Text.Encoding]::UTF8.GetBytes($text)
    return ([BitConverter]::ToString($sha.ComputeHash($bytes)) -replace '-', '').ToLower()
}
$results = @()
$anyBad = $false
foreach ($f in $fixtures) {
    $checksums = @(); $asserts = @(); $passes = @()
    for ($i = 1; $i -le $Runs; $i++) {
        $runDir = Join-Path $out ("run-" + $i + "\" + $f.name)
        New-Item -ItemType Directory -Force -Path $runDir | Out-Null
        $log = Join-Path $runDir 'doctest.log'
        $env:COSMIC_WO10_EVIDENCE_DIR = $runDir
        $env:TEMP = $runDir; $env:TMP = $runDir
        $args = @('"-tc=' + $f.tc + '"')
        if ($f.values) { $args += '-s' }
        $p = Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $runDir -NoNewWindow -PassThru -Wait -RedirectStandardOutput $log -RedirectStandardError (Join-Path $runDir 'stderr.log')
        Remove-Item Env:COSMIC_WO10_EVIDENCE_DIR -ErrorAction SilentlyContinue
        $text = Get-Content -LiteralPath $log -Raw
        $tcLine = [regex]::Match($text, '\[doctest\] test cases:\s+(\d+) \|\s+(\d+) passed \|\s+(\d+) failed')
        $asLine = [regex]::Match($text, '\[doctest\] assertions:\s+(\d+) \|\s+(\d+) passed \|\s+(\d+) failed')
        $ok = ($p.ExitCode -eq 0) -and $tcLine.Success -and ([int]$tcLine.Groups[1].Value -gt 0) -and ([int]$tcLine.Groups[3].Value -eq 0)
        $passes += $ok
        $asserts += $(if ($asLine.Success) { [int]$asLine.Groups[1].Value } else { -1 })
        $material = ''
        if ($f.values) {
            $material = (($text -split "`r?`n") | Where-Object { $_ -match '^\s+values:' } | ForEach-Object { $_.Trim() }) -join "`n"
        }
        if ($f.n04) {
            $h = Join-Path $runDir 'n04-hashes.txt'
            if (Test-Path -LiteralPath $h) { $material += (Get-Content -LiteralPath $h -Raw) }
            foreach ($bin in (Get-ChildItem -LiteralPath $runDir -Filter 'n04-*.bin' | Sort-Object Name)) {
                $material += ("`n" + $bin.Name + ' ' + (Get-FileHash -LiteralPath $bin.FullName -Algorithm SHA256).Hash.ToLower())
                Remove-Item -LiteralPath $bin.FullName -Force
            }
        }
        if (-not $material) { $material = 'tests=' + $tcLine.Groups[1].Value + ' asserts=' + $asserts[-1] }
        Set-Content -LiteralPath (Join-Path $runDir 'checksum-material.txt') -Value $material -Encoding utf8
        $checksums += (Sha $material)
    }
    $identical = (@($checksums | Select-Object -Unique).Count -eq 1) -and (@($asserts | Select-Object -Unique).Count -eq 1)
    $allPass = -not ($passes -contains $false)
    $verdict = if ($identical -and $allPass) { 'PASS' } else { 'FAIL' }
    if ($verdict -ne 'PASS') { $anyBad = $true }
    $results += [pscustomobject]@{ fixture = $f.name; test_case = $f.tc; runs = $Runs; verdict = $verdict; all_pass = $allPass; identical = $identical; checksum = $checksums[0]; checksums = $checksums; assertions = $asserts }
    Write-Host ("{0,-26} {1}  checksum={2}  assertions={3}" -f $f.name, $verdict, $checksums[0].Substring(0, 16), ($asserts -join ','))
}
$summary = [pscustomobject]@{ case = 'S03'; config = $Config; runs = $Runs; exe = $exe; commit = (git -C $repo rev-parse HEAD); generated_utc = [DateTime]::UtcNow.ToString('o'); verdict = $(if ($anyBad) { 'FAIL' } else { 'PASS' }); fixtures = $results }
$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $out 'summary.json') -Encoding utf8
Write-Host ("S03 {0}: {1}" -f $Config, $summary.verdict)
if ($anyBad) { exit 1 } else { exit 0 }
