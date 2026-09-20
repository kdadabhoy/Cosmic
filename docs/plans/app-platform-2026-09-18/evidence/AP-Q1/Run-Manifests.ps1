# AP-Q1 driver: run a list of acceptance manifests through the real runner, one after another,
# each into evidence/AP-Q1/manifests-<Config>/<manifest>/, and append one summary line per
# manifest to summary.txt (manifest, profile, exit code, passed/failed/blocked, seconds).
param(
    [Parameter(Mandatory=$true)][string]$Config,
    [Parameter(Mandatory=$true)][string]$Manifests,     # comma-separated "name" or "name:profile" (default profile: release)
    [string]$OutRoot = '',
    [string]$TempRoot = 'C:\dev\Cosmic\build\_temp\apq1'   # repository-local (the WO-07/WO-09 fixtures refuse a run-temp outside the repo)
)
$ErrorActionPreference = 'Continue'
$repo = 'C:\dev\Cosmic'
if (-not $OutRoot) { $OutRoot = Join-Path $repo ("docs\plans\app-platform-2026-09-18\evidence\AP-Q1\manifests-" + $Config) }
New-Item -ItemType Directory -Force -Path $OutRoot | Out-Null
New-Item -ItemType Directory -Force -Path $TempRoot | Out-Null
$summary = Join-Path $OutRoot 'summary.txt'
$list = $Manifests -split ','
foreach ($m in $list) {
    $m = $m.Trim(); if (-not $m) { continue }
    $name = $m; $profile = 'release'
    if ($m -match '^(.+):(.+)$') { $name = $Matches[1]; $profile = $Matches[2] }
    $manifest = Join-Path $repo ("tests\acceptance\manifests\" + $name + ".manifest.json")
    $out = Join-Path $OutRoot $name
    if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
    $sw = [Diagnostics.Stopwatch]::StartNew()
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo 'tests\acceptance\Run-Acceptance.ps1') -Manifest $manifest -Profile $profile -Config $Config -OutDir $out -TempRoot $TempRoot > (Join-Path $OutRoot ($name + '.console.log')) 2>&1
    $code = $LASTEXITCODE
    $line = "{0}  profile={1}  exit={2}  seconds={3}" -f $name, $profile, $code, [int]$sw.Elapsed.TotalSeconds
    $res = Join-Path $out 'results.json'
    if (Test-Path -LiteralPath $res) {
        try {
            $r = Get-Content -LiteralPath $res -Raw | ConvertFrom-Json
            $cases = @($r.cases)
            $p = @($cases | Where-Object { $_.verdict -eq 'PASSED' }).Count
            $f = @($cases | Where-Object { $_.verdict -eq 'FAILED' }).Count
            $b = @($cases | Where-Object { $_.verdict -eq 'ENVIRONMENT_BLOCKED' }).Count
            $line += "  passed={0} failed={1} blocked={2}  commit={3} dirty={4}" -f $p, $f, $b, $r.git.commit, $r.git.dirty
            $ids = ($cases | ForEach-Object { "{0}:{1}" -f $_.id, $_.verdict }) -join ' '
            $line += "  [" + $ids + "]"
        } catch { $line += "  (results.json unreadable: $($_.Exception.Message))" }
    } else { $line += "  (no results.json)" }
    Add-Content -LiteralPath $summary -Value $line -Encoding utf8
    Write-Host $line
}
Add-Content -LiteralPath $summary -Value 'DONE' -Encoding utf8
