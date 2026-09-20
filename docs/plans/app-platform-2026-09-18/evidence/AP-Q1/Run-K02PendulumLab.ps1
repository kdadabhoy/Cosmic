# K02 for PendulumLab (stability catalog K02, "K02 and Y02 add PendulumLab to the packaged set"):
# package PendulumLab through the supported CLI path (installer/Stage-AppPackage.ps1, the script
# package.bat and release.yml call) and compare it file-for-file with the tree the REAL editor
# path produced in Y02 (dist/PendulumLab from Run-Y02Package.ps1); inspect the payload: the
# renamed exe, only this app's DLL, Cosmic.dll, boot.cfg semantics (names the app, sets the
# user:// identity), assets minus other projects, licenses/** per installer/licenses/MANIFEST.txt, user/README.txt, and
# NOTHING dev-only (CosmicTests/CosmicRenderTests/PDB/.lib/.exp/another app's DLL or content/src/build).
# Writes k02-PendulumLab/{cli.files.txt,editor.files.txt,report.json}; exits 1 on any finding.
param(
    [string]$EditorDist = 'C:\dev\Cosmic\dist\PendulumLab',
    [string]$External = 'C:\dev\Cosmic\build\apq1-y02-external\PendulumLab'
)
$ErrorActionPreference = 'Stop'
$repo = 'C:\dev\Cosmic'
$out = Join-Path $repo 'docs\plans\app-platform-2026-09-18\evidence\AP-Q1\k02-PendulumLab'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$cliDist = Join-Path $repo 'build\_temp\apq1\k02-cli\PendulumLab'
$findings = New-Object System.Collections.ArrayList
$appDll = Join-Path $External 'build\Release\PendulumLab.dll'
if (-not (Test-Path -LiteralPath $appDll)) { throw "external PendulumLab.dll missing (run Y02 first): $appDll" }
if (-not (Test-Path -LiteralPath (Join-Path $EditorDist 'PendulumLab.exe'))) { throw "editor dist missing (run Y02 first): $EditorDist" }

# 1. CLI path (the same script package.bat / release.yml call), external project like the editor did.
$cliList = Join-Path $out 'cli.files.txt'
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo 'installer\Stage-AppPackage.ps1') -SdkRoot $repo -App PendulumLab -RuntimeDir (Join-Path $repo 'build\Runtime\Release') -OutDir $cliDist -ProjectContentDir $External -AppDllPath $appDll -ListOut $cliList > (Join-Path $out 'stage-cli.log') 2>&1
if ($LASTEXITCODE -ne 0) { [void]$findings.Add("Stage-AppPackage.ps1 exit $LASTEXITCODE") }

function RelList([string]$root) {
    Get-ChildItem -LiteralPath $root -Recurse -File | ForEach-Object { $_.FullName.Substring($root.TrimEnd('\').Length + 1).Replace('\', '/') } | Sort-Object
}
$cli = @(RelList $cliDist)
$editorAll = @(RelList $EditorDist)
# The editor dist was RUN by Y02 (portable mode writes user://); runtime output under user/ other than the staged README is not payload.
$editorRuntime = @($editorAll | Where-Object { $_ -like 'user/*' -and $_ -ne 'user/README.txt' })
$editor = @($editorAll | Where-Object { -not ($_ -like 'user/*' -and $_ -ne 'user/README.txt') })
$editor | Set-Content -LiteralPath (Join-Path $out 'editor.files.txt') -Encoding utf8
$editorRuntime | Set-Content -LiteralPath (Join-Path $out 'editor.runtime-output.txt') -Encoding utf8
$diff = Compare-Object -ReferenceObject $cli -DifferenceObject $editor
if ($diff) { foreach ($d in $diff) { [void]$findings.Add("payload differs: $($d.InputObject) $($d.SideIndicator)") } }

# 2. Byte-identity of the staged binaries between the two paths.
$notes = New-Object System.Collections.ArrayList
foreach ($f in @('PendulumLab.exe', 'PendulumLab.dll', 'Cosmic.dll', 'licenses/THIRD-PARTY.txt', 'user/README.txt')) {
    $a = Join-Path $cliDist $f; $b = Join-Path $EditorDist $f
    if (-not (Test-Path -LiteralPath $a)) { [void]$findings.Add("cli missing $f"); continue }
    if (-not (Test-Path -LiteralPath $b)) { [void]$findings.Add("editor missing $f"); continue }
    if ((Get-FileHash -LiteralPath $a).Hash -ne (Get-FileHash -LiteralPath $b).Hash) {
        if ($f -eq 'user/README.txt') {
            # Text placeholder only: the editor (Packager.cpp std::ofstream, text mode) writes CRLF + a trailing
            # newline, the CLI (Stage-AppPackage.ps1) LF without one. Same words. Recorded as a note for the
            # packaging owner, not a payload failure (nothing reads the file).
            $ta = (Get-Content -LiteralPath $a -Raw) -replace "`r`n", "`n"; $tb = (Get-Content -LiteralPath $b -Raw) -replace "`r`n", "`n"
            if ($ta.TrimEnd() -eq $tb.TrimEnd()) { [void]$notes.Add('user/README.txt: same text, CRLF+trailing newline (editor) vs LF (CLI)') } else { [void]$findings.Add("$f differs in content between cli and editor staging") }
        } else { [void]$findings.Add("$f differs between cli and editor staging") }
    }
}
# 3. Payload inspection.
foreach ($f in @('PendulumLab.exe', 'PendulumLab.dll', 'Cosmic.dll', 'boot.cfg', 'licenses/THIRD-PARTY.txt', 'user/README.txt', 'assets/projects/PendulumLab/project.cproj', 'assets/projects/PendulumLab/flows/Main.cflow')) {
    if ($cli -notcontains $f) { [void]$findings.Add("required payload missing: $f") }
}
$forbidden = $cli | Where-Object { $_ -match '(?i)(^|/)(CosmicTests|CosmicRenderTests|CosmicApp)\.exe$|\.pdb$|\.lib$|\.exp$|^assets/projects/(?!PendulumLab/)|^assets/projects/PendulumLab/(src|build|\.git)/|(^|/)(SF_Telem|AnalysisSample|Starforge|ForgePlayground)\.dll$|Starforge\.exe$' }
foreach ($f in $forbidden) { [void]$findings.Add("forbidden in payload: $f") }
$otherDlls = $cli | Where-Object { $_ -like '*.dll' -and $_ -ne 'PendulumLab.dll' -and $_ -ne 'Cosmic.dll' }
foreach ($f in $otherDlls) { [void]$findings.Add("unexpected DLL: $f") }
$boot = Get-Content -LiteralPath (Join-Path $cliDist 'boot.cfg') -Raw
if ($boot -notmatch 'PendulumLab') { [void]$findings.Add('boot.cfg does not name PendulumLab') }
$bootEditor = Get-Content -LiteralPath (Join-Path $EditorDist 'boot.cfg') -Raw
if ($boot -ne $bootEditor) { [void]$findings.Add('boot.cfg differs between cli and editor staging') }
# Every license file installer/licenses/MANIFEST.txt lists is staged (the manifest itself is the packer's input, not payload).
$licNames = Get-Content -LiteralPath (Join-Path $repo 'installer\licenses\MANIFEST.txt') | ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') -and $_.Contains('|') } | ForEach-Object { ($_ -split '\|')[1].Trim() }
foreach ($ln in $licNames) { if (-not (Test-Path -LiteralPath (Join-Path $cliDist ('licenses\' + $ln)))) { [void]$findings.Add("license file from MANIFEST.txt not staged: $ln") } }
$stagedLic = @(Get-ChildItem -LiteralPath (Join-Path $cliDist 'licenses') -File | Select-Object -ExpandProperty Name)
if ($stagedLic.Count -ne $licNames.Count) { [void]$findings.Add("licenses/ holds $($stagedLic.Count) files, MANIFEST.txt lists $($licNames.Count)") }
# Runtime build identity: the staged Cosmic.dll is the qualified Release DLL.
$rtDll = (Get-FileHash -LiteralPath (Join-Path $repo 'build\Runtime\Release\Cosmic.dll')).Hash
if ((Get-FileHash -LiteralPath (Join-Path $cliDist 'Cosmic.dll')).Hash -ne $rtDll) { [void]$findings.Add('staged Cosmic.dll is not build/Runtime/Release/Cosmic.dll') }
$shaders = @($cli | Where-Object { $_ -like 'assets/shaders/*.glsl' }).Count
$fonts = @($cli | Where-Object { $_ -like 'assets/fonts/*' }).Count

$report = [pscustomobject]@{
    case = 'K02 (PendulumLab)'; commit = (git -C $repo rev-parse HEAD); generated_utc = [DateTime]::UtcNow.ToString('o')
    verdict = $(if ($findings.Count -eq 0) { 'PASS' } else { 'FAIL' })
    cli_dist = $cliDist; editor_dist = $EditorDist; cli_files = $cli.Count; editor_files = $editor.Count; editor_runtime_output_files = $editorRuntime.Count
    shaders = $shaders; fonts = $fonts; boot_cfg = ("" + $boot)
    hashes = @{ exe = (Get-FileHash -LiteralPath (Join-Path $cliDist 'PendulumLab.exe')).Hash.ToLower(); app_dll = (Get-FileHash -LiteralPath (Join-Path $cliDist 'PendulumLab.dll')).Hash.ToLower(); cosmic_dll = $rtDll.ToLower() }
    findings = @($findings); notes = @($notes)
}
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $out 'report.json') -Encoding utf8
Write-Host ("K02 PendulumLab: {0}  cli={1} files, editor={2} files (+{3} runtime output), findings={4}" -f $report.verdict, $cli.Count, $editor.Count, $editorRuntime.Count, $findings.Count)
foreach ($f in $findings) { Write-Host "  FINDING: $f" }
foreach ($n in $notes) { Write-Host "  NOTE: $n" }
if ($findings.Count -eq 0) { exit 0 } else { exit 1 }
