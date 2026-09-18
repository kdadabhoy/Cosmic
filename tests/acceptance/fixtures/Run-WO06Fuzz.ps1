param([Parameter(Mandatory=$true)][string]$Bin)
$env:COSMIC_WO06_FUZZ_CASES='50000'
& (Join-Path $Bin 'CosmicTests.exe') '--test-case=WO-06 D03: bounded*' '--no-colors'
exit $LASTEXITCODE
