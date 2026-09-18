param([Parameter(Mandatory=$true)][string]$Report)
$ErrorActionPreference='Stop'
$acceptance=Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$repo=[IO.Path]::GetFullPath((Join-Path $acceptance '..\..')).TrimEnd('\')+'\'
if (-not [IO.Path]::GetFullPath($Report).StartsWith($repo,[StringComparison]::OrdinalIgnoreCase)) { throw 'Report must be inside Cosmic' }
$run=Get-Content -LiteralPath $Report -Raw | ConvertFrom-Json
$case=@($run.cases | Where-Object { $_.id -eq 'T05-controlled-30min' })
if ($case.Count -ne 1 -or $case[0].verdict -ne 'PASSED') { throw 'T05 must have passed' }
$folder=Join-Path $run.run_temp 'temp\recordings\SF_Telem\wo05-long'
if (-not [IO.Path]::GetFullPath($folder).StartsWith($repo,[StringComparison]::OrdinalIgnoreCase)) { throw 'Save must be inside Cosmic' }
$path=Join-Path $folder 'scene.bin'
$reader=New-Object IO.BinaryReader([IO.File]::OpenRead($path))
$expected=@{ESC_Right=8;ESC_Left=8;ESC_Weapon=9}
$descriptors=New-Object System.Collections.ArrayList
try {
    $magic=[Text.Encoding]::ASCII.GetString($reader.ReadBytes(4))
    if ($magic -ne 'CSMC' -or $reader.ReadUInt32() -ne 1) { throw 'Binary identity mismatch' }
    if ($reader.ReadUInt32() -ne 3 -or $reader.ReadSingle() -ne 60) { throw 'Entity count/sample rate mismatch' }
    [long]$dataBytes=0
    for ($index=0;$index -lt 3;$index++) {
        # Fixed buffers are NUL-terminated; Debug CRT may fill trailing padding.
        $name=[Text.Encoding]::ASCII.GetString($reader.ReadBytes(64)).Split([char]0)[0]
        $tag=[Text.Encoding]::ASCII.GetString($reader.ReadBytes(64)).Split([char]0)[0]
        $channels=$reader.ReadUInt32(); $samples=$reader.ReadUInt32()
        if (-not $expected.ContainsKey($name) -or $channels -ne $expected[$name] -or $samples -ne 108000) { throw "Unexpected descriptor $name channels=$channels samples=$samples" }
        $expected.Remove($name)
        [void]$reader.ReadBytes([int]($channels*32))
        $dataBytes += [long]$samples*($channels+1)*4
        $csv=Join-Path $folder ($name+'.csv')
        $lines=([IO.File]::ReadLines($csv) | Measure-Object).Count
        if ($lines -ne 108001) { throw "CSV $name data rows=$($lines-1), expected 108000" }
        [void]$descriptors.Add([ordered]@{name=$name;tag=$tag;channels=$channels;binary_samples=$samples;csv_rows=$lines-1;csv_sha256=(Get-FileHash -LiteralPath $csv -Algorithm SHA256).Hash.ToLower()})
    }
    if ($expected.Count -ne 0 -or $reader.BaseStream.Length -ne $reader.BaseStream.Position+$dataBytes) { throw 'Missing descriptor or binary data length mismatch' }
} finally { $reader.Dispose() }
[ordered]@{config=$run.config;source_report=$Report;binary=$path;binary_sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLower();entities=$descriptors} | ConvertTo-Json -Depth 6 | Write-Output
Write-Output '[doctest] test cases: 1 | 1 passed | 0 failed'
