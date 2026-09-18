param([Parameter(Mandatory=$true)][string]$Bin, [Parameter(Mandatory=$true)][string]$Output,
    [int]$Iterations=100, [ValidateRange(0,8)][int]$FirstSchedule=0,
    [ValidateRange(0,8)][int]$LastSchedule=8, [switch]$Policy)
$ErrorActionPreference='Stop'
$acceptance = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Import-Module (Join-Path $acceptance 'AcceptanceRunner.psm1') -Force
$repo = [IO.Path]::GetFullPath((Join-Path $acceptance '..\..')).TrimEnd('\') + '\'
foreach ($location in @($Bin,$Output)) {
    if (-not [IO.Path]::GetFullPath($location).StartsWith($repo,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'WO-05 fixtures and artifacts must remain inside Cosmic'
    }
}
if ($FirstSchedule -gt $LastSchedule -or $Iterations -lt 1) { throw 'Invalid schedule/iteration range' }
if ($Policy) { $FirstSchedule=0; $LastSchedule=0; $Iterations=1 }
function Link-ImmutableAsset([string]$Source,[string]$Destination) {
    if (Test-Path -LiteralPath $Destination) { return }
    if ((Get-Item -LiteralPath $Source).PSIsContainer) {
        New-Item -ItemType Junction -Path $Destination -Target $Source | Out-Null
    } else {
        New-Item -ItemType HardLink -Path $Destination -Target $Source | Out-Null
    }
}
function Initialize-ChildAssets([string]$Child) {
    $assets = Join-Path $Child 'assets'
    New-Item -ItemType Directory -Force -Path $assets | Out-Null
    foreach ($asset in Get-ChildItem -LiteralPath (Join-Path $Bin 'assets')) {
        if ($asset.Name -in @('projects','logs')) { continue }
        Link-ImmutableAsset $asset.FullName (Join-Path $assets $asset.Name)
    }
    $projects=Join-Path $assets 'projects'
    New-Item -ItemType Directory -Force -Path $projects | Out-Null
    foreach ($project in Get-ChildItem -LiteralPath (Join-Path $Bin 'assets\projects') -Directory) {
        $local=Join-Path $projects $project.Name
        New-Item -ItemType Directory -Force -Path $local | Out-Null
        foreach ($asset in Get-ChildItem -LiteralPath $project.FullName) {
            if ($asset.Name -eq 'logs') { continue }
            Link-ImmutableAsset $asset.FullName (Join-Path $local $asset.Name)
        }
        # Project logging must be isolated too, even when native shards run together.
        New-Item -ItemType Directory -Force -Path (Join-Path $local 'logs') | Out-Null
    }
}
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$scratch = Join-Path $Output ('scratch-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
$records = New-Object System.Collections.ArrayList
$failed = 0
for ($schedule=$FirstSchedule; $schedule -le $LastSchedule; $schedule++) {
    $lastTransition=1
    if ($Policy) { $lastTransition=0 }
    for ($transition=0; $transition -le $lastTransition; $transition++) {
        for ($iteration=0; $iteration -lt $Iterations; $iteration++) {
            $env:COSMIC_WO05_HOST_CASE = "$schedule,$transition,$($iteration % 5)"
            $id = "s$schedule-t$transition-i$iteration"
            $temp = Join-Path $scratch $id
            New-Item -ItemType Directory -Force -Path $temp | Out-Null
            # Application ignores COSMIC_USER_DATA. Its writable '.' user root and
            # raw recording paths must therefore have an isolated CWD. Link only
            # the repository-local runtime assets; never copy/modify real user data.
            Initialize-ChildAssets $temp
            $filter='--test-case=WO-05 T03 host:*'
            if ($Policy) { $filter='--test-case=WO-05 T05 host:*' }
            $case = [pscustomobject]@{
                id=$id; description='Fresh real host, actual SF_Telem and DLL unload'; tier='G';
                requires=@('windows'); transport='fake';
                command=(Join-Path $Bin 'CosmicTests.exe'); workingDir=$temp;
                args=@($filter,'--no-skip=true','--no-colors');
                minTests=1; expectExit=0; deadlineSec=60
            }
            $vars=@{BIN=$Bin;RUNTEMP=$temp;USERDATA=$temp;ACCEPTANCE=$acceptance}
            $record=Invoke-AcceptanceCase -Case $case -Caps @{windows=$true} -Vars $vars -RunDir $Output -AcceptanceDir $acceptance -AcceptanceMode
            if ($record.verdict -eq 'PASSED') {
                $text = Get-Content -LiteralPath $record.stdout_log -Raw
                $mark = [regex]::Match($text,'WO05 close_request_unix_ms=(\d+)')
                if (-not $mark.Success) {
                    $record.verdict='FAILED'; $record.detail='Missing process close deadline evidence'
                } else {
                    # Complete's end time is after normal child exit and count/log
                    # validation, so this is a conservative upper bound on exit.
                    $endMs = [DateTimeOffset]::Parse([string]$record.end_utc).ToUnixTimeMilliseconds()
                    $delta = $endMs - [long]$mark.Groups[1].Value
                    $record['close_exit_upper_bound_ms'] = $delta
                    if ($delta -lt 0 -or $delta -gt 2000) {
                        $record.verdict='FAILED'; $record.detail="Close-to-process-exit upper bound $delta ms exceeds 2000 ms (or clock invalid)"
                    }
                }
            }
            [void]$records.Add($record)
            if ($record.verdict -ne 'PASSED') {
                $failed++; Write-Host "$id $($record.verdict): $($record.detail)"
                $records | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $Output 'children.json') -Encoding utf8
                exit 1
            }
        }
        Write-Host "host schedule=$schedule transition=$transition completed=$Iterations"
        $records | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $Output 'children.json') -Encoding utf8
    }
}
$total=$records.Count
Write-Host "[doctest] test cases: $total | $($total-$failed) passed | $failed failed"
exit $failed
