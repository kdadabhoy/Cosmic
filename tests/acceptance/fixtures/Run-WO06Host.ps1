param([Parameter(Mandatory=$true)][string]$Bin,[Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
$acceptance=Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Import-Module (Join-Path $acceptance 'AcceptanceRunner.psm1') -Force
$repo=[IO.Path]::GetFullPath((Join-Path $acceptance '..\..')).TrimEnd('\')+'\'
foreach($location in @($Bin,$Output)) {if(-not [IO.Path]::GetFullPath($location).StartsWith($repo,[StringComparison]::OrdinalIgnoreCase)){throw 'WO-06 artifacts must remain inside Cosmic'}}
New-Item -ItemType Directory -Force -Path $Output|Out-Null
function Link-Asset([string]$Source,[string]$Destination) {
 if((Get-Item -LiteralPath $Source).PSIsContainer){New-Item -ItemType Junction -Path $Destination -Target $Source|Out-Null}
 else {New-Item -ItemType HardLink -Path $Destination -Target $Source|Out-Null}
}
$records=@()
foreach($transition in @('native close','launcher return')) {
 $id=if($transition -eq 'native close'){'close'}else{'launcher'}
 $child=Join-Path $Output ('child-'+$id);New-Item -ItemType Directory -Force -Path $child|Out-Null
 $assets=Join-Path $child 'assets';New-Item -ItemType Directory -Force -Path $assets|Out-Null
 foreach($asset in Get-ChildItem -LiteralPath (Join-Path $Bin 'assets')) {if($asset.Name -notin @('projects','logs')){Link-Asset $asset.FullName (Join-Path $assets $asset.Name)}}
 $projects=Join-Path $assets 'projects';New-Item -ItemType Directory -Force -Path $projects|Out-Null
 foreach($project in Get-ChildItem -LiteralPath (Join-Path $Bin 'assets\projects') -Directory) {
  $local=Join-Path $projects $project.Name;New-Item -ItemType Directory -Force -Path $local|Out-Null
  foreach($asset in Get-ChildItem -LiteralPath $project.FullName){if($asset.Name -ne 'logs'){Link-Asset $asset.FullName (Join-Path $local $asset.Name)}}
 }
 $case=[pscustomobject]@{id=$id;description='Actual two-hour pending snapshot host shutdown';tier='G';requires=@('windows');transport='fake';command=(Join-Path $Bin 'CosmicTests.exe');workingDir=$child;args=@("--test-case=WO-06 D05 host: two hour pending export $transition",'--no-skip=true','--no-colors');minTests=1;expectExit=0;deadlineSec=60}
 $vars=@{BIN=$Bin;RUNTEMP=$child;USERDATA=$child;ACCEPTANCE=$acceptance}
 $record=Invoke-AcceptanceCase -Case $case -Caps @{windows=$true} -Vars $vars -RunDir $Output -AcceptanceDir $acceptance -AcceptanceMode
 if($record.verdict -eq 'PASSED') {
  $text=Get-Content -LiteralPath $record.stdout_log -Raw;$mark=[regex]::Match($text,'WO06 close_request_unix_ms=(\d+)')
  if(-not $mark.Success){$record.verdict='FAILED';$record.detail='Missing normal process-exit deadline evidence'}
  else {$delta=[DateTimeOffset]::Parse([string]$record.end_utc).ToUnixTimeMilliseconds()-[long]$mark.Groups[1].Value;$record['close_exit_upper_bound_ms']=$delta;if($delta -lt 0 -or $delta -gt 30000){$record.verdict='FAILED';$record.detail="Close-to-normal-exit $delta ms exceeds 30000 ms"}}
 }
 $records+= $record;$records|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $Output 'children.json') -Encoding UTF8
 Write-Host "$id $($record.verdict) $($record.detail)"
 if($record.verdict -ne 'PASSED'){exit 1}
}
Write-Host '[doctest] test cases: 2 | 2 passed | 0 failed'
exit 0
