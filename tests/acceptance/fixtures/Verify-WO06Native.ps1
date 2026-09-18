param([Parameter(Mandatory=$true)][string]$Root,[Parameter(Mandatory=$true)][string]$Python)
$ErrorActionPreference='Stop';$env:PYTHONDONTWRITEBYTECODE='1'
$script=Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) 'Verify-WO06.py'
foreach($child in @('close','launcher')) {
 $folder=Join-Path $Root "child-$child\recordings\SF_Telem\wo06-native-two-hour"
 & $Python $script $folder --zeros --output (Join-Path $Root "$child-independent.json")
 if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
}
Write-Host '[doctest] test cases: 2 | 2 passed | 0 failed'
exit 0
