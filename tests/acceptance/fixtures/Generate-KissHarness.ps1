param([Parameter(Mandatory=$true)][string]$Repo, [Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference = 'Stop'
$source = Get-Content -LiteralPath (Join-Path $Repo 'Projects/SF_Telem/src/FirmwareTemplates.h') -Raw
$module = [regex]::Match($source, '(?s)return R"FW\((.*?)\)FW";')
$data = [regex]::Match($source, '(?s)struct ESC_Data\s*\{.*?\};')
if (!$module.Success -or !$data.Success) { throw 'Shipping KISS source anchors missing' }
# Compile the exact shipping sketch block against Stream/Serial OS-boundary mocks.
($data.Value + "`n" + $module.Groups[1].Value) | Set-Content -LiteralPath $Output -Encoding UTF8
