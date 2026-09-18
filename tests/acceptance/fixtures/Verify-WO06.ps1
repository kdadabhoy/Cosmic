param([Parameter(Mandatory=$true)][string]$Report,[Parameter(Mandatory=$true)][string]$Python,[Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop';$env:PYTHONDONTWRITEBYTECODE='1'
$runner=Get-Content -LiteralPath $Report -Raw|ConvertFrom-Json
$folder=Join-Path $runner.run_temp 'temp\recordings\SF_Telem\wo06-two-hour'
$script=Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) 'Verify-WO06.py'
& $Python $script $folder --output $Output
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
Write-Host '[doctest] test cases: 1 | 1 passed | 0 failed'
exit 0
