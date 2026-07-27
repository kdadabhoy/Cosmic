# check_docs_coverage.ps1 - API reference coverage audit (doc 12 work order D5).
#
# FAILS (exit 1) when the public C++ surface and docs/reference/README.md's
# coverage manifest disagree:
#   1. a public header with NO manifest row
#   2. a manifest row whose header no longer exists on disk (stale row)
#   3. a manifest row with no chapter cell (malformed - would be skipped silently)
#   4. a manifest row whose 3D marker contradicts the build (see MARKERS below)
#   5. a manifest row pointing at a chapter file that does not exist
#   6. STRICT MODE (per chapter, automatic): a chapter file with no
#      "STATUS: SKELETON" banner that never mentions a COSMIC_API class/struct
#      declared by one of its own headers.
# Chapters that still carry the skeleton banner WARN only - they have not been
# written yet, so a missing class name there is expected, not a defect.
#
# Chapter links are resolved relative to docs/reference/, so a row MAY point
# outside that directory - "../guide/voxels.md" is how a header whose reference
# chapter does not exist yet gets parked on its client-facing guide chapter.
# Strict mode is a reference-tier contract and does not apply to those.
#
# WHAT COUNTS AS "PUBLIC" (three tiers - doc 12 section 5, flavours 1-5):
#   A. DIRECT      - #include "..." in Cosmic/src/Cosmic.h.
#   B. TRANSITIVE  - anything those headers pull in, recursively, that lives
#                    under Cosmic/src/. A one-level scan of Cosmic.h misses
#                    graphics/Skeleton.h (via scene/Components3D.h) and
#                    voxel/VoxelVolume.h (via scripting/ScriptableEntity.h),
#                    both of which are named in public signatures.
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
# THE 2D/3D SPLIT (README section 1.6, systems/build-2d-3d-split.md):
#   Cosmic.h is parsed WITH its #ifndef COSMIC_2D_ONLY fences, not as flat text.
#   Fence state propagates through the transitive walk, so a header reachable
#   only through a fenced include is itself 3D-only.
#   "Inside a fence" is NOT the test for 3D-only, though. camera/NavigationCube.h
#   is included UNFENCED yet NavigationCube.cpp is dropped from the 2D build by
#   the list(FILTER) block in Cosmic/CMakeLists.txt - it compiles in a 2D tree
#   and fails at LINK time. So the script parses that CMake block too and
#   classifies by the union of the two.
#
# MARKERS in the manifest's first column:
#   U+00B3 U+1D30        ("3D")   header Cosmic.h includes only inside a fence
#   U+00B3 U+1D30 U+207A ("3D+")  header compiles in 2D but whose .cpp the
#                                 CMake 2D filter removes -> link-time failure
# Both are written with [char] casts below so this file stays pure ASCII:
# PowerShell 5.1 decodes a BOM-less .ps1 as ANSI, which would corrupt literals.
#
# Run locally:   powershell -ExecutionPolicy Bypass -File tests\check_docs_coverage.ps1
# Run in CI:     .github/workflows/ci.yml "API reference coverage audit" step (pwsh).

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot   # tests/ -> repo root
$srcRoot = Join-Path $repoRoot 'Cosmic\src'
$manifestPath = Join-Path $repoRoot 'docs\reference\README.md'
$referenceDir = Join-Path $repoRoot 'docs\reference'
$cmakePath = Join-Path $repoRoot 'Cosmic\CMakeLists.txt'
$clientRoots = @((Join-Path $repoRoot 'Projects'), (Join-Path $repoRoot 'tests'))

$MARK_3D = [string]([char]0x00B3) + [string]([char]0x1D30)
$MARK_3D_LINK = $MARK_3D + [string]([char]0x207A)

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
# 1. The CMake 2D filter - which sources vanish when COSMIC_2D_ONLY is ON.
# ---------------------------------------------------------------------------
$cmakeText = Get-Content -LiteralPath $cmakePath -Raw -Encoding UTF8
$cmakeLines = $cmakeText -split "`r?`n"
$filterPatterns = @()
$inBlock = $false
foreach ($line in $cmakeLines)
{
    if ($line -match '^\s*if\s*\(\s*COSMIC_2D_ONLY\s*\)') { $inBlock = $true; continue }
    if ($inBlock -and $line -match '^\s*endif\s*\(') { $inBlock = $false; continue }
    if (-not $inBlock) { continue }
    if ($line -match 'list\s*\(\s*FILTER\s+COSMIC_SOURCES\s+EXCLUDE\s+REGEX\s+"([^"]+)"')
    {
        # CMake string escaping: "\\." in the file is the regex \. once parsed.
        $filterPatterns += ($matches[1] -replace '\\\\', '\')
    }
}
if ($filterPatterns.Count -eq 0)
{
    Write-Host 'DOCS COVERAGE ERROR - no list(FILTER COSMIC_SOURCES ...) rules found inside if(COSMIC_2D_ONLY)'
    Write-Host ('  {0}: the 2D partition block moved or was renamed; the 3D classification cannot be computed.' -f 'Cosmic/CMakeLists.txt')
    exit 1
}

function Test-FilteredFrom2D([string]$relPath)
{
    # The CMake patterns are written against absolute paths, all anchored on /src/.
    $probe = '/src/' + $relPath
    foreach ($p in $filterPatterns) { if ($probe -match $p) { return $true } }
    return $false
}

# ---------------------------------------------------------------------------
# 2. Fence-aware include parser.
# ---------------------------------------------------------------------------
# Returns one object per quoted #include: the raw path and whether it sits
# inside an #ifndef COSMIC_2D_ONLY region. Ordinary include guards and unrelated
# #if blocks are tracked too so the nesting stays balanced.
function Get-QuotedIncludes([string]$fullPath)
{
    $text = Get-Content -LiteralPath $fullPath -Raw -Encoding UTF8
    $lines = $text -split "`r?`n"
    $stack = New-Object System.Collections.Generic.Stack[bool]
    $results = @()
    $lineNo = 0
    foreach ($line in $lines)
    {
        $lineNo++
        if ($line -match '^\s*#\s*ifndef\s+COSMIC_2D_ONLY\b') { $stack.Push($true); continue }
        if ($line -match '^\s*#\s*(if|ifdef|ifndef)\b') { $stack.Push($false); continue }
        if ($line -match '^\s*#\s*endif\b') { if ($stack.Count -gt 0) { $null = $stack.Pop() }; continue }
        if ($line -match '^\s*#\s*include\s+"([^"]+)"')
        {
            $fenced = $false
            foreach ($f in $stack) { if ($f) { $fenced = $true } }
            $results += [pscustomobject]@{
                Path   = $matches[1] -replace '\\', '/'
                Fenced = $fenced
                Line   = $lineNo
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
# 3. Walk Cosmic.h: tier A (direct) + tier B (transitive), fence state carried.
# ---------------------------------------------------------------------------
$public = @{}   # rel path -> record
$entryHeader = Join-Path $srcRoot 'Cosmic.h'

$public['Cosmic.h'] = [pscustomobject]@{
    Path = 'Cosmic.h'; Tier = 'A'; Fenced = $false; Via = ''; Line = 0; Evidence = ''
}

$queue = New-Object System.Collections.Queue
foreach ($inc in (Get-QuotedIncludes $entryHeader))
{
    $rel = Resolve-EngineHeader $inc.Path $entryHeader
    if (-not $rel) { continue }
    $queue.Enqueue([pscustomobject]@{
        Path = $rel; Fenced = $inc.Fenced; Tier = 'A'; Via = 'Cosmic.h'; Line = $inc.Line
    })
}

while ($queue.Count -gt 0)
{
    $item = $queue.Dequeue()
    if ($public.ContainsKey($item.Path))
    {
        # An unfenced route to a header beats a fenced one: if ANY reachable path
        # is unfenced the header exists in a 2D compile.
        if (-not $item.Fenced) { $public[$item.Path].Fenced = $false }
        if ($item.Tier -eq 'A' -and $public[$item.Path].Tier -ne 'A')
        {
            $public[$item.Path].Tier = 'A'
            $public[$item.Path].Via = $item.Via
            $public[$item.Path].Line = $item.Line
        }
        continue
    }
    $public[$item.Path] = [pscustomobject]@{
        Path = $item.Path; Tier = $item.Tier; Fenced = $item.Fenced
        Via = $item.Via; Line = $item.Line; Evidence = ''
    }

    $full = Join-Path $srcRoot ($item.Path -replace '/', '\')
    foreach ($inc in (Get-QuotedIncludes $full))
    {
        $rel = Resolve-EngineHeader $inc.Path $full
        if (-not $rel) { continue }
        $queue.Enqueue([pscustomobject]@{
            Path = $rel; Fenced = ($item.Fenced -or $inc.Fenced); Tier = 'B'
            Via = $item.Path; Line = $inc.Line
        })
    }
}

# ---------------------------------------------------------------------------
# 4. Tier C - engine headers only shipped client code includes explicitly.
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
                Path = $inc; Tier = 'C'; Fenced = $false; Via = ''; Line = 0
                Evidence = $file.FullName.Substring($repoRoot.Length + 1) -replace '\\', '/'
            }
        }
    }
}

# ---------------------------------------------------------------------------
# 5. Parse the coverage manifest table (+ the encoded footnote rows).
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

    # 3D markers, matched by codepoint so this file stays ASCII-only:
    # U+207A superscript plus, U+1D30 modifier letter capital D.
    $marker = ''
    if ($left.IndexOf([char]0x207A) -ge 0) { $marker = '3D+' }
    elseif ($left.IndexOf([char]0x1D30) -ge 0) { $marker = '3D' }

    $chapters = @()
    foreach ($lm in [regex]::Matches($right, '\[[^\]]*\]\(([^)#]+)')) { $chapters += $lm.Groups[1].Value }

    $rows[$header] = [pscustomobject]@{
        Header = $header; Chapters = $chapters; Marker = $marker; Source = 'table'
    }
}
foreach ($k in $footnoteRows.Keys)
{
    if ($rows.ContainsKey($k)) { continue }
    $rows[$k] = [pscustomobject]@{
        Header = $k; Chapters = @($footnoteRows[$k]); Marker = ''; Source = 'footnote'
    }
}

# ---------------------------------------------------------------------------
# 6. Compare.
# ---------------------------------------------------------------------------
$failures = @()
$failures += $malformedRows
$suggestedRows = @()

# 6a. Public headers with no manifest row.
$missing = @()
foreach ($key in ($public.Keys | Sort-Object))
{
    $rec = $public[$key]
    if ($internalHeaders.ContainsKey($key)) { continue }
    if ($rows.ContainsKey($key)) { continue }

    $full = Join-Path $srcRoot ($key -replace '/', '\')
    $is3DFence = $rec.Fenced
    $is3DLink = Test-FilteredFrom2D $key
    # A 2D-only distribution that physically drops the 3D sources must not be
    # reported as a wall of missing headers - that is the naive-parse trap.
    if (-not (Test-Path -LiteralPath $full -PathType Leaf))
    {
        if ($is3DFence -or $is3DLink) { continue }
    }

    $mark = ''
    if ($is3DFence) { $mark = $MARK_3D }
    elseif ($is3DLink) { $mark = $MARK_3D_LINK }

    if ($rec.Tier -eq 'A') { $why = ('included directly by Cosmic.h:{0}' -f $rec.Line) }
    elseif ($rec.Tier -eq 'B') { $why = ('reachable from Cosmic.h via {0}' -f $rec.Via) }
    else { $why = ('not reachable from Cosmic.h; explicitly included by {0}' -f $rec.Evidence) }
    if ($is3DFence) { $why = $why + ' [3D-only: fenced]' }
    elseif ($is3DLink) { $why = $why + ' [3D-only: CMake 2D filter drops its .cpp]' }

    $missing += ('{0}: {1} - no manifest row' -f $key, $why)
    if ($mark -eq '') { $suggestedRows += ('| `{0}` | [CHAPTER](CHAPTER) |' -f $key) }
    else { $suggestedRows += ('| `{0}` {1} | [CHAPTER](CHAPTER) |' -f $key, $mark) }
}
$failures += $missing

# 6b. Stale rows - a listed header that no longer exists.
$stale = @()
foreach ($header in ($rows.Keys | Sort-Object))
{
    $row = $rows[$header]
    $full = Join-Path $srcRoot ($header -replace '/', '\')
    if (Test-Path -LiteralPath $full -PathType Leaf) { continue }
    # A 3D-marked row in a 2D-only distribution is not stale, just absent.
    if ($row.Marker -ne '') { continue }
    $stale += ('{0}: listed in the coverage manifest but no such file under Cosmic/src/' -f $header)
}
$failures += $stale

# 6c. Marker vs. build reality.
$markerIssues = @()
foreach ($header in ($rows.Keys | Sort-Object))
{
    $row = $rows[$header]
    if ($row.Source -eq 'footnote') { continue }
    if (-not $public.ContainsKey($header)) { continue }
    $full = Join-Path $srcRoot ($header -replace '/', '\')
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { continue }

    $is3DFence = $public[$header].Fenced
    $is3DLink = Test-FilteredFrom2D $header
    $expected = ''
    if ($is3DFence) { $expected = '3D' }
    elseif ($is3DLink) { $expected = '3D+' }

    if ($row.Marker -eq $expected) { continue }
    if ($expected -eq '')
    {
        $markerIssues += ('{0}: row carries a 3D marker but the header compiles AND links in the 2D build - drop the marker' -f $header)
    }
    elseif ($expected -eq '3D')
    {
        $markerIssues += ('{0}: Cosmic.h reaches it only inside an #ifndef COSMIC_2D_ONLY fence - row needs the 3D marker' -f $header)
    }
    else
    {
        $markerIssues += ('{0}: unfenced but Cosmic/CMakeLists.txt drops its .cpp from the 2D build (link-time failure) - row needs the 3D-plus marker' -f $header)
    }
}
$failures += $markerIssues

# ---------------------------------------------------------------------------
# 7. Strict mode - per chapter, automatic when the skeleton banner is gone.
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
    # that directory - "../guide/voxels.md" is how a header whose reference
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
# 8. Report.
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
if ($markerIssues.Count -gt 0)
{
    Write-Host 'DOCS COVERAGE FAILURE - 2D/3D marker does not match the build:'
    $markerIssues | ForEach-Object { Write-Host "  $_" }
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
    Write-Host ('{0} coverage violation(s): {1} unlisted header(s), {2} stale row(s), {3} malformed row(s), {4} marker mismatch(es), {5} missing chapter file(s), {6} strict-mode gap(s).' -f `
        $failures.Count, $missing.Count, $stale.Count, $malformedRows.Count, $markerIssues.Count, $missingChapters.Count, $strictIssues.Count)
    Write-Host 'Add the row to the manifest in docs/reference/README.md (and an entry in its chapter), or justify the header in $internalHeaders.'
    exit 1
}

Write-Host ('Docs coverage: clean ({0} public headers, {1} manifest rows, {2} reference chapter(s) still skeletons, {3} chapter(s) outside docs/reference/ so exempt from strict mode).' -f `
    $public.Count, $rows.Count, $skeletonChapterCount, $offTierChapters)
exit 0
