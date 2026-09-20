# check_docs_links.ps1 - Markdown link and anchor checker (App Platform AP-D1, DOC01/DOC04).
#
# Scans README.md, docs/**/*.md, Projects/*/README.md, Projects/*/docs/*.md and
# tests/**/*.md. For every Markdown link it finds it resolves:
#   - relative FILE links        -> the target must exist on disk (file or directory)
#   - "#anchor" links            -> the target document must contain a heading whose
#                                   GitHub slug equals the anchor (lowercase, spaces -> "-",
#                                   punctuation other than "-" and "_" stripped; a repeated
#                                   heading gets "-1", "-2", ...) or an explicit
#                                   <a name="..."> / <a id="..."> / {#id} marker.
# Skips http(s)://, mailto:, and bare "<" autolinks. Links inside fenced code blocks and
# inline code spans are ignored.
#
# Tiers:
#   STRICT (exit 1 on any breakage)  - every scanned file not listed below.
#   WARN-ONLY (reported, exit 0)     - docs/archive/**, docs/plans/archive/**, docs/parked-3d/**,
#                                      docs/plans/2d-stability-2026-09-16/evidence/**
#                                      (archived and parked material; relative links inside it
#                                      were written before the moves).
#
# This file stays pure ASCII: PowerShell 5.1 decodes a BOM-less .ps1 as ANSI, which would
# corrupt any non-ASCII literal.
#
# Run locally:   powershell -ExecutionPolicy Bypass -File tests\check_docs_links.ps1
#                (add -Verbose for every link resolved; -ShowWarn to list warn-tier breakages)
# Run in CI:     .github/workflows/ci.yml "Markdown link audit" step (pwsh).

param(
    [switch]$ShowWarn
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot   # tests/ -> repo root
$repoRootFull = (Resolve-Path -LiteralPath $repoRoot).Path.TrimEnd('\')

$warnOnlyPrefixes = @(
    'docs/archive/',
    'docs/plans/archive/',
    'docs/parked-3d/',
    'docs/plans/2d-stability-2026-09-16/evidence/'
)

# ---------------------------------------------------------------------------
# 1. Collect the files to scan.
# ---------------------------------------------------------------------------
function Add-Files([System.Collections.ArrayList]$list, [string]$dir, [string]$filter, [switch]$recurse)
{
    if (-not (Test-Path -LiteralPath $dir -PathType Container)) { return }
    $items = if ($recurse) { Get-ChildItem -LiteralPath $dir -Filter $filter -File -Recurse }
             else { Get-ChildItem -LiteralPath $dir -Filter $filter -File }
    foreach ($f in $items) { [void]$list.Add($f.FullName) }
}

$files = New-Object System.Collections.ArrayList
$rootReadme = Join-Path $repoRoot 'README.md'
if (Test-Path -LiteralPath $rootReadme) { [void]$files.Add((Resolve-Path -LiteralPath $rootReadme).Path) }
Add-Files $files (Join-Path $repoRoot 'docs') '*.md' -recurse
Add-Files $files (Join-Path $repoRoot 'tests') '*.md' -recurse
$projectsDir = Join-Path $repoRoot 'Projects'
if (Test-Path -LiteralPath $projectsDir -PathType Container)
{
    foreach ($p in (Get-ChildItem -LiteralPath $projectsDir -Directory))
    {
        Add-Files $files $p.FullName 'README.md'
        Add-Files $files (Join-Path $p.FullName 'docs') '*.md'
    }
}

function Get-RelPath([string]$full)
{
    $rel = $full
    if ($full.StartsWith($repoRootFull, [System.StringComparison]::OrdinalIgnoreCase))
    {
        $rel = $full.Substring($repoRootFull.Length).TrimStart('\', '/')
    }
    return ($rel -replace '\\', '/')
}

function Test-WarnOnly([string]$rel)
{
    foreach ($p in $warnOnlyPrefixes)
    {
        if ($rel.StartsWith($p, [System.StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
    return $false
}

# ---------------------------------------------------------------------------
# 2. Heading slugs (GitHub rules) per document, computed lazily and cached.
# ---------------------------------------------------------------------------
$slugCache = @{}

function Get-Slug([string]$heading)
{
    $t = $heading.Trim()
    # strip trailing "#"s of ATX headings and surrounding whitespace
    $t = $t -replace '\s+#+\s*$', ''
    # drop inline markdown emphasis/code markers and link syntax: [text](url) -> text
    $t = [regex]::Replace($t, '\[([^\]]*)\]\([^)]*\)', '$1')
    $t = $t -replace '[`*~]', ''
    # strip real HTML tags only (a "<T>" inside a code span is text to GitHub and stays)
    $t = [regex]::Replace($t, '</?(a|br|sup|sub|kbd|em|strong|code|span|img|b|i|u|small|del|ins)[^>]*>', '')
    $t = $t.ToLowerInvariant()
    # GitHub keeps letters (any script), digits, spaces, hyphens and underscores; everything
    # else is removed. \p{L}\p{N}\p{M} covers unicode letters/digits/marks.
    $t = [regex]::Replace($t, '[^\p{L}\p{N}\p{M}\s\-_]', '')
    $t = $t -replace '\s', '-'
    return $t
}

function Get-DocAnchors([string]$fullPath)
{
    if ($slugCache.ContainsKey($fullPath)) { return $slugCache[$fullPath] }
    $set = @{}
    $text = Get-Content -LiteralPath $fullPath -Raw -Encoding UTF8
    if ($null -eq $text) { $text = '' }
    $lines = $text -split "`r?`n"
    $inFence = $false
    $counts = @{}
    for ($i = 0; $i -lt $lines.Count; $i++)
    {
        $line = $lines[$i]
        if ($line -match '^\s{0,3}(```|~~~)') { $inFence = -not $inFence; continue }
        if ($inFence) { continue }
        $heading = $null
        if ($line -match '^\s{0,3}#{1,6}\s+(.*)$') { $heading = $matches[1] }
        elseif ($i + 1 -lt $lines.Count -and $line.Trim().Length -gt 0 -and $lines[$i + 1] -match '^\s{0,3}(=+|-+)\s*$' -and $line -notmatch '^\s*\|')
        {
            # setext heading (underlined with === or ---); skip table separators
            $heading = $line
        }
        if ($null -ne $heading)
        {
            $slug = Get-Slug $heading
            if ($counts.ContainsKey($slug))
            {
                $counts[$slug] = $counts[$slug] + 1
                $slug = '{0}-{1}' -f $slug, $counts[$slug]
            }
            else { $counts[$slug] = 0 }
            $set[$slug] = $true
        }
        # explicit anchors: <a name="x">, <a id="x">, {#x}
        foreach ($m in [regex]::Matches($line, '<a\s+(?:name|id)\s*=\s*"([^"]+)"'))
        {
            $set[$m.Groups[1].Value.ToLowerInvariant()] = $true
        }
        foreach ($m in [regex]::Matches($line, '\{#([^}\s]+)\}'))
        {
            $set[$m.Groups[1].Value.ToLowerInvariant()] = $true
        }
    }
    $slugCache[$fullPath] = $set
    return $set
}

# ---------------------------------------------------------------------------
# 3. Link extraction: [text](target), [text]: target reference definitions.
#    Fenced code blocks and inline code spans are removed first.
# ---------------------------------------------------------------------------
function Get-Links([string]$fullPath)
{
    $text = Get-Content -LiteralPath $fullPath -Raw -Encoding UTF8
    if ($null -eq $text) { return @() }
    $lines = $text -split "`r?`n"
    $out = New-Object System.Collections.ArrayList
    $inFence = $false
    for ($i = 0; $i -lt $lines.Count; $i++)
    {
        $line = $lines[$i]
        if ($line -match '^\s{0,3}(```|~~~)') { $inFence = -not $inFence; continue }
        if ($inFence) { continue }
        # blank out inline code spans so links quoted as examples are not checked
        $clean = [regex]::Replace($line, '`[^`]*`', '')
        # inline links: ](target) - target ends at the first ")" not opened inside; allow one
        # level of balanced parentheses (Wikipedia-style) and an optional "title".
        foreach ($m in [regex]::Matches($clean, '\]\(\s*<?([^\s>()]+(?:\([^\s()]*\)[^\s>()]*)*)>?(?:\s+"[^"]*")?\s*\)'))
        {
            [void]$out.Add([pscustomobject]@{ Target = $m.Groups[1].Value; Line = $i + 1 })
        }
        # reference definitions: [id]: target
        if ($clean -match '^\s{0,3}\[[^\]]+\]:\s*<?(\S+)>?')
        {
            [void]$out.Add([pscustomobject]@{ Target = $matches[1]; Line = $i + 1 })
        }
    }
    return $out
}

# ---------------------------------------------------------------------------
# 4. Resolve every link.
# ---------------------------------------------------------------------------
$strictIssues = New-Object System.Collections.ArrayList
$warnIssues = New-Object System.Collections.ArrayList
$fileCount = 0
$linkCount = 0
$anchorCount = 0

foreach ($file in ($files | Sort-Object -Unique))
{
    $fileCount++
    $rel = Get-RelPath $file
    $warnOnly = Test-WarnOnly $rel
    $dir = Split-Path -Parent $file
    foreach ($link in (Get-Links $file))
    {
        $target = $link.Target
        if ($target -match '^(https?:|mailto:|ftp:|file:)') { continue }
        if ($target -match '^[a-zA-Z][a-zA-Z0-9+.\-]*:') { continue }   # any other URI scheme
        $linkCount++
        $pathPart = $target
        $anchor = $null
        $hash = $target.IndexOf('#')
        if ($hash -ge 0)
        {
            $pathPart = $target.Substring(0, $hash)
            $anchor = $target.Substring($hash + 1)
        }
        # strip a query string (rare in local links)
        $q = $pathPart.IndexOf('?')
        if ($q -ge 0) { $pathPart = $pathPart.Substring(0, $q) }
        # percent-decoding for spaces etc.
        try { $pathPart = [System.Uri]::UnescapeDataString($pathPart) } catch { }

        $targetFull = $null
        if ($pathPart.Length -eq 0)
        {
            $targetFull = $file
        }
        else
        {
            if ($pathPart.StartsWith('/'))
            {
                # site-absolute link: resolve against the repo root (GitHub renders these
                # relative to the repository root only for the web UI; keep them checkable).
                $targetFull = Join-Path $repoRoot ($pathPart.TrimStart('/') -replace '/', '\')
            }
            else
            {
                $targetFull = Join-Path $dir ($pathPart -replace '/', '\')
            }
            try { $targetFull = [System.IO.Path]::GetFullPath($targetFull) } catch { $targetFull = $null }
            if ($null -eq $targetFull -or -not (Test-Path -LiteralPath $targetFull))
            {
                $msg = '{0}:{1}: broken link "{2}" (file not found)' -f $rel, $link.Line, $target
                if ($warnOnly) { [void]$warnIssues.Add($msg) } else { [void]$strictIssues.Add($msg) }
                continue
            }
        }

        if ($null -ne $anchor -and $anchor.Length -gt 0)
        {
            if (Test-Path -LiteralPath $targetFull -PathType Container)
            {
                # directory + anchor: GitHub shows the directory README
                $readme = Join-Path $targetFull 'README.md'
                if (Test-Path -LiteralPath $readme -PathType Leaf) { $targetFull = $readme } else { continue }
            }
            if ($targetFull -notmatch '\.(md|markdown)$') { continue }   # anchors into non-markdown are not checked
            $anchorCount++
            $anchors = Get-DocAnchors $targetFull
            $want = $anchor.ToLowerInvariant()
            try { $want = [System.Uri]::UnescapeDataString($want) } catch { }
            if (-not $anchors.ContainsKey($want))
            {
                $msg = '{0}:{1}: broken anchor "{2}" (no heading with slug "#{3}" in {4})' -f $rel, $link.Line, $target, $want, (Get-RelPath $targetFull)
                if ($warnOnly) { [void]$warnIssues.Add($msg) } else { [void]$strictIssues.Add($msg) }
            }
        }
    }
}

# ---------------------------------------------------------------------------
# 5. Report.
# ---------------------------------------------------------------------------
Write-Host ('Markdown link audit: {0} files, {1} local links ({2} with anchors) checked.' -f $fileCount, $linkCount, $anchorCount)
Write-Host ('  strict-tier breakages: {0}' -f $strictIssues.Count)
Write-Host ('  warn-only breakages (archive / parked-3d / stability evidence): {0}' -f $warnIssues.Count)

if ($warnIssues.Count -gt 0 -and ($ShowWarn -or $VerbosePreference -ne 'SilentlyContinue'))
{
    Write-Host ''
    Write-Host 'WARNING - breakages in warn-only tiers (not a failure):'
    $warnIssues | ForEach-Object { Write-Host "  $_" }
}

if ($strictIssues.Count -gt 0)
{
    Write-Host ''
    Write-Host 'DOCS LINK FAILURE - broken links or anchors in live documentation:'
    $strictIssues | ForEach-Object { Write-Host "  $_" }
    exit 1
}

Write-Host 'Markdown link audit passed.'
exit 0
