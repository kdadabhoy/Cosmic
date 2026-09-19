# check_docs_coverage.ps1 - API reference coverage audit (doc 12 work order D5).
#
# FAILS (exit 1) when the public C++ surface and docs/reference/README.md's
# coverage manifest disagree:
#   1. a public header with NO manifest row
#   2. a manifest row whose header no longer exists on disk (stale row)
#   3. a manifest row with no chapter cell (malformed - would be skipped silently)
#   4. a manifest row pointing at a chapter file that does not exist
#   5. STRICT MODE (per chapter, automatic): a chapter file with no
#      "STATUS: SKELETON" banner that never mentions a COSMIC_API class/struct
#      declared by one of its own headers.
# Chapters that still carry the skeleton banner WARN only - they have not been
# written yet, so a missing class name there is expected, not a defect.
#
# Chapter links are resolved relative to docs/reference/, so a row MAY point
# outside that directory - "../guide/scripting.md" is how a header whose reference
# chapter does not exist yet gets parked on its client-facing guide chapter.
# Strict mode is a reference-tier contract and does not apply to those.
#
# WHAT COUNTS AS "PUBLIC" (three tiers - doc 12 section 5, flavours 1-5):
#   A. DIRECT      - #include "..." in Cosmic/src/Cosmic.h.
#   B. TRANSITIVE  - anything those headers pull in, recursively, that lives
#                    under Cosmic/src/. A one-level scan of Cosmic.h misses the
#                    headers that only ride in through another public header
#                    yet are named in public signatures.
#   C. CLIENT-ONLY - an engine header under Cosmic/src/ that is not reachable
#                    from Cosmic.h at all but IS explicitly #included by shipped
#                    client code (Projects/*/src, tests/). utils/Branding.h is
#                    the type case: COSMIC_API-exported, unit-tested, and called
#                    from Projects/Starforge/src/StarforgeApp.cpp.
# Tier C is evidence-based rather than an allowlist on purpose: an allowlist
# only ever contains the gaps somebody already noticed by hand, which is exactly
# the failure mode this script exists to end. The including file is printed with
# every tier-C finding so a reviewer can judge the claim.
#
# Cosmic.h and every header it reaches are parsed as flat text: an #include is an
# #include whatever preprocessor block it sits in (there are no COSMIC_2D_ONLY
# fences left since AP-05 part B). A quoted include whose file is absent under
# Cosmic/src/ is simply not a public header (it is skipped, not reported).
#
# History: until AP-05 part A (2026-09-18) this script also parsed Cosmic.h
# fence-aware and read the list(FILTER) block in Cosmic/CMakeLists.txt to classify
# headers as 3D-only, and checked the manifest's 3D marker glyphs against that
# classification. The 3D source, the filter block and the marked rows are gone
# from this trunk, so that machinery (which exited 1 whenever no filter rules
# existed) went with them. Rows may still carry the old marker glyphs after the
# header cell; they are ignored.
#
# This file stays pure ASCII: PowerShell 5.1 decodes a BOM-less .ps1 as ANSI,
# which would corrupt any non-ASCII literal.
#
# Run locally:   powershell -ExecutionPolicy Bypass -File tests\check_docs_coverage.ps1
# Run in CI:     .github/workflows/ci.yml "API reference coverage audit" step (pwsh).

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot   # tests/ -> repo root
$srcRoot = Join-Path $repoRoot 'Cosmic\src'
$manifestPath = Join-Path $repoRoot 'docs\reference\README.md'
$referenceDir = Join-Path $repoRoot 'docs\reference'
$clientRoots = @((Join-Path $repoRoot 'Projects'), (Join-Path $repoRoot 'tests'))

# Headers reachable from Cosmic.h that are engine plumbing, not client surface.
# Each one is reached only because a public header needs its type internally;
# none is exported for a project DLL to call. Keep this list SHORT and justified
# - it is the one place the script can be wrong on purpose.
$internalHeaders = @{
    'core/LayerStack.h'        = 'Application owns it; PushLayer/PushOverlay are the client verbs'
    'graphics/GraphicsContext.h' = 'platform seam created by Window; no client ever constructs one'
    'renderer/RendererAPI.h'   = 'the S13.1 backend seam that RenderCommand fronts (doc 05 rule 0.1)'
}

# The manifest's "Not in Cosmic.h but client-reachable, documented anyway"
# footnote (docs/reference/README.md, directly under the table). It is prose, not
# a table row, so it is encoded here rather than force-fit into the table format
# (doc 12 section 5 gotcha). Keep in sync with that sentence.
$footnoteRows = @{
    'core/Window.h'           = 'core.md'
    'layers/WorkspaceLayer.h' = 'ui.md'
}

# ---------------------------------------------------------------------------
# 1. Flat include parser.
# ---------------------------------------------------------------------------
# Returns one object per quoted #include: the raw path and its line number.
function Get-QuotedIncludes([string]$fullPath)
{
    $text = Get-Content -LiteralPath $fullPath -Raw -Encoding UTF8
    $lines = $text -split "`r?`n"
    $results = @()
    $lineNo = 0
    foreach ($line in $lines)
    {
        $lineNo++
        if ($line -match '^\s*#\s*include\s+"([^"]+)"')
        {
            $results += [pscustomobject]@{
                Path = $matches[1] -replace '\\', '/'
                Line = $lineNo
            }
        }
    }
    return $results
}

# Resolve an include spelling to a path relative to Cosmic/src, or $null.
function Resolve-EngineHeader([string]$includePath, [string]$includingFile)
{
    $cand = Join-Path $srcRoot $includePath
    if (Test-Path -LiteralPath $cand -PathType Leaf) { return ($includePath -replace '\\', '/') }
    $sibling = Join-Path (Split-Path -Parent $includingFile) $includePath
    if (Test-Path -LiteralPath $sibling -PathType Leaf)
    {
        $full = (Resolve-Path -LiteralPath $sibling).Path
        if ($full.StartsWith($srcRoot, [System.StringComparison]::OrdinalIgnoreCase))
        {
            return ($full.Substring($srcRoot.Length + 1) -replace '\\', '/')
        }
    }
    return $null
}

# ---------------------------------------------------------------------------
# 2. Walk Cosmic.h: tier A (direct) + tier B (transitive).
# ---------------------------------------------------------------------------
$public = @{}   # rel path -> record
$entryHeader = Join-Path $srcRoot 'Cosmic.h'

$public['Cosmic.h'] = [pscustomobject]@{
    Path = 'Cosmic.h'; Tier = 'A'; Via = ''; Line = 0; Evidence = ''
}

$queue = New-Object System.Collections.Queue
foreach ($inc in (Get-QuotedIncludes $entryHeader))
{
    $rel = Resolve-EngineHeader $inc.Path $entryHeader
    if (-not $rel) { continue }
    $queue.Enqueue([pscustomobject]@{
        Path = $rel; Tier = 'A'; Via = 'Cosmic.h'; Line = $inc.Line
    })
}

while ($queue.Count -gt 0)
{
    $item = $queue.Dequeue()
    if ($public.ContainsKey($item.Path))
    {
        if ($item.Tier -eq 'A' -and $public[$item.Path].Tier -ne 'A')
        {
            $public[$item.Path].Tier = 'A'
            $public[$item.Path].Via = $item.Via
            $public[$item.Path].Line = $item.Line
        }
        continue
    }
    $public[$item.Path] = [pscustomobject]@{
        Path = $item.Path; Tier = $item.Tier
        Via = $item.Via; Line = $item.Line; Evidence = ''
    }

    $full = Join-Path $srcRoot ($item.Path -replace '/', '\')
    foreach ($inc in (Get-QuotedIncludes $full))
    {
        $rel = Resolve-EngineHeader $inc.Path $full
        if (-not $rel) { continue }
        $queue.Enqueue([pscustomobject]@{
            Path = $rel; Tier = 'B'; Via = $item.Path; Line = $inc.Line
        })
    }
}

# ---------------------------------------------------------------------------
# 3. Tier C - engine headers only shipped client code includes explicitly.
# ---------------------------------------------------------------------------
foreach ($root in $clientRoots)
{
    if (-not (Test-Path $root)) { continue }
    $clientFiles = Get-ChildItem -Path $root -Recurse -Include *.cpp, *.h -File
    foreach ($file in $clientFiles)
    {
        $text = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8
        foreach ($m in [regex]::Matches($text, '(?m)^\s*#\s*include\s+"([^"]+)"'))
        {
            $inc = $m.Groups[1].Value -replace '\\', '/'
            if ($inc.StartsWith('../')) { continue }
            $engineCand = Join-Path $srcRoot $inc
            if (-not (Test-Path -LiteralPath $engineCand -PathType Leaf)) { continue }
            # A project-local file of the same relative name wins - that include
            # resolves to the project's own header, not the engine's.
            $localCand = Join-Path $file.DirectoryName $inc
            if (Test-Path -LiteralPath $localCand -PathType Leaf) { continue }
            if ($public.ContainsKey($inc)) { continue }
            $public[$inc] = [pscustomobject]@{
                Path = $inc; Tier = 'C'; Via = ''; Line = 0
                Evidence = $file.FullName.Substring($repoRoot.Length + 1) -replace '\\', '/'
            }
        }
    }
}

# ---------------------------------------------------------------------------
# 4. Parse the coverage manifest table (+ the encoded footnote rows).
# ---------------------------------------------------------------------------
$manifestText = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8
$manifestLines = $manifestText -split "`r?`n"

$rows = @{}
$malformedRows = @()
foreach ($line in $manifestLines)
{
    if ($line -notmatch '^\s*\|') { continue }
    $cells = $line.Split('|')
    if ($cells.Count -lt 3) { continue }
    $left = $cells[1].Trim()
    if ($left -notmatch '`([^`]+)`') { continue }
    $header = $matches[1] -replace '\\', '/'
    if ($header -notmatch '\.h$') { continue }   # skips the "Header | Chapter" head row
    if ($cells.Count -lt 4)
    {
        # A row missing its chapter cell would otherwise be skipped silently and
        # read as "no row at all" - say so instead of guessing.
        $malformedRows += ('{0}: manifest row has no chapter cell (expected "| `header` | [chapter](chapter) |")' -f $header)
        continue
    }
    $right = $cells[2].Trim()

    $chapters = @()
    foreach ($lm in [regex]::Matches($right, '\[[^\]]*\]\(([^)#]+)')) { $chapters += $lm.Groups[1].Value }

    $rows[$header] = [pscustomobject]@{
        Header = $header; Chapters = $chapters; Source = 'table'
    }
}
foreach ($k in $footnoteRows.Keys)
{
    if ($rows.ContainsKey($k)) { continue }
    $rows[$k] = [pscustomobject]@{
        Header = $k; Chapters = @($footnoteRows[$k]); Source = 'footnote'
    }
}

# ---------------------------------------------------------------------------
# 5. Compare.
# ---------------------------------------------------------------------------
$failures = @()
$failures += $malformedRows
$suggestedRows = @()

# 5a. Public headers with no manifest row.
$missing = @()
foreach ($key in ($public.Keys | Sort-Object))
{
    $rec = $public[$key]
    if ($internalHeaders.ContainsKey($key)) { continue }
    if ($rows.ContainsKey($key)) { continue }

    if ($rec.Tier -eq 'A') { $why = ('included directly by Cosmic.h:{0}' -f $rec.Line) }
    elseif ($rec.Tier -eq 'B') { $why = ('reachable from Cosmic.h via {0}' -f $rec.Via) }
    else { $why = ('not reachable from Cosmic.h; explicitly included by {0}' -f $rec.Evidence) }

    $missing += ('{0}: {1} - no manifest row' -f $key, $why)
    $suggestedRows += ('| `{0}` | [CHAPTER](CHAPTER) |' -f $key)
}
$failures += $missing

# 5b. Stale rows - a listed header that no longer exists.
$stale = @()
foreach ($header in ($rows.Keys | Sort-Object))
{
    $full = Join-Path $srcRoot ($header -replace '/', '\')
    if (Test-Path -LiteralPath $full -PathType Leaf) { continue }
    $stale += ('{0}: listed in the coverage manifest but no such file under Cosmic/src/' -f $header)
}
$failures += $stale

# ---------------------------------------------------------------------------
# 6. Strict mode - per chapter, automatic when the skeleton banner is gone.
# ---------------------------------------------------------------------------
$chapterHeaders = @{}
foreach ($header in $rows.Keys)
{
    foreach ($chapter in $rows[$header].Chapters)
    {
        $link = $chapter.Trim()
        if (-not $chapterHeaders.ContainsKey($link)) { $chapterHeaders[$link] = @() }
        $chapterHeaders[$link] += $header
    }
}

$strictIssues = @()
$skeletonReport = @()
$missingChapters = @()
$skeletonChapterCount = 0
$offTierChapters = 0
foreach ($chapterLink in ($chapterHeaders.Keys | Sort-Object))
{
    # Links are relative to docs/reference/. A row may legitimately point OUTSIDE
    # that directory - "../guide/scripting.md" is how a header whose reference
    # chapter does not exist yet gets parked on its client-facing guide chapter.
    $chapterPath = [System.IO.Path]::GetFullPath((Join-Path $referenceDir $chapterLink))
    $chapterName = $chapterPath.Substring($repoRoot.Length + 1) -replace '\\', '/'
    if (-not (Test-Path -LiteralPath $chapterPath -PathType Leaf))
    {
        $missingChapters += ('{0}: chapter file referenced by the manifest does not exist (linked as "{1}")' -f $chapterName, $chapterLink)
        continue
    }
    # Strict mode is a REFERENCE-tier contract (per-call entries). A guide chapter
    # is prose and is not held to naming every COSMIC_API class.
    if (-not $chapterPath.StartsWith($referenceDir, [System.StringComparison]::OrdinalIgnoreCase))
    {
        $offTierChapters++
        continue
    }
    $chapterText = Get-Content -LiteralPath $chapterPath -Raw -Encoding UTF8
    $isSkeleton = $chapterText -match 'STATUS:\s*SKELETON'

    $names = @()
    foreach ($header in ($chapterHeaders[$chapterLink] | Sort-Object -Unique))
    {
        $full = Join-Path $srcRoot ($header -replace '/', '\')
        if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { continue }
        $headerText = Get-Content -LiteralPath $full -Raw -Encoding UTF8
        foreach ($m in [regex]::Matches($headerText, '(?m)^\s*(?:class|struct)\s+COSMIC_API\s+(\w+)'))
        {
            $names += [pscustomobject]@{ Name = $m.Groups[1].Value; Header = $header }
        }
    }

    $undocumented = @()
    $seenName = @{}
    foreach ($n in $names)
    {
        if ($seenName.ContainsKey($n.Name)) { continue }
        $seenName[$n.Name] = $true
        if ($chapterText.Contains($n.Name)) { continue }
        $undocumented += ('{0}: never mentions COSMIC_API {1} (declared in {2})' -f $chapterName, $n.Name, $n.Header)
    }

    if ($isSkeleton)
    {
        $skeletonChapterCount++
        if ($undocumented.Count -gt 0)
        {
            $skeletonReport += ('{0}: STATUS: SKELETON - {1} COSMIC_API name(s) not covered yet' -f $chapterName, $undocumented.Count)
            foreach ($u in $undocumented) { $skeletonReport += ('    ' + ($u -replace ('^' + [regex]::Escape($chapterName) + ': '), '')) }
        }
    }
    else
    {
        $strictIssues += $undocumented
    }
}
$failures += $missingChapters
$failures += $strictIssues

# ---------------------------------------------------------------------------
# 7. Report.
# ---------------------------------------------------------------------------
if ($missing.Count -gt 0)
{
    Write-Host 'DOCS COVERAGE FAILURE - public headers with no row in docs/reference/README.md:'
    $missing | ForEach-Object { Write-Host "  $_" }
    Write-Host ''
    Write-Host '  Suggested manifest rows (fill in CHAPTER):'
    $suggestedRows | ForEach-Object { Write-Host "    $_" }
    Write-Host ''
}
if ($stale.Count -gt 0)
{
    Write-Host 'DOCS COVERAGE FAILURE - stale manifest rows:'
    $stale | ForEach-Object { Write-Host "  $_" }
    Write-Host ''
}
if ($malformedRows.Count -gt 0)
{
    Write-Host 'DOCS COVERAGE FAILURE - malformed manifest rows:'
    $malformedRows | ForEach-Object { Write-Host "  $_" }
    Write-Host ''
}
if ($missingChapters.Count -gt 0)
{
    Write-Host 'DOCS COVERAGE FAILURE - manifest rows pointing at a chapter file that does not exist:'
    $missingChapters | ForEach-Object { Write-Host "  $_" }
    Write-Host ''
}
if ($strictIssues.Count -gt 0)
{
    Write-Host 'DOCS COVERAGE FAILURE - written chapter missing a COSMIC_API symbol (strict mode):'
    $strictIssues | ForEach-Object { Write-Host "  $_" }
    Write-Host ''
}
if ($skeletonReport.Count -gt 0)
{
    Write-Host 'WARNING - skeleton chapters with uncovered COSMIC_API symbols (strict mode turns on when the STATUS: SKELETON banner is deleted):'
    $skeletonReport | ForEach-Object { Write-Host "  $_" }
    Write-Host ''
}

if ($failures.Count -gt 0)
{
    Write-Host ('{0} coverage violation(s): {1} unlisted header(s), {2} stale row(s), {3} malformed row(s), {4} missing chapter file(s), {5} strict-mode gap(s).' -f `
        $failures.Count, $missing.Count, $stale.Count, $malformedRows.Count, $missingChapters.Count, $strictIssues.Count)
    Write-Host 'Add the row to the manifest in docs/reference/README.md (and an entry in its chapter), or justify the header in $internalHeaders.'
    exit 1
}

Write-Host ('Docs coverage: clean ({0} public headers, {1} manifest rows, {2} reference chapter(s) still skeletons, {3} chapter(s) outside docs/reference/ so exempt from strict mode).' -f `
    $public.Count, $rows.Count, $skeletonChapterCount, $offTierChapters)
exit 0
