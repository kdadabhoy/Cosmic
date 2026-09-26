# check_gl_conformance.ps1 — S13.1 conformance audit (doc 05 §0 rule 1 / §12).
#
# FAILS (exit 1) when a raw OpenGL token — a gl* call like glDrawArrays( or a
# GL_* enum — appears in engine or app CODE outside the platform layer. Every
# GPU operation must go through RendererAPI/RenderCommand verbs so a second
# backend swaps in at one seam.
#
# Scanned:  Cosmic/src (minus Cosmic/src/platform/OpenGL), Projects/*/src, tests
# Exempt:   Cosmic/src/platform/OpenGL/**   — the OpenGL backend itself
#           Cosmic/dependencies/**          — vendored (GLAD, ImGui GL backend);
#                                             a second backend replaces these
#                                             wholesale, they are not engine code
#           Comment lines                   — docs may cite GL behavior by name
#
# Pass 2 (UX-V0, KI-82; see its block below): every *.glsl in the repo — exit 1
# when a uniform sampler array is indexed by anything but an integer literal.
#
# Run locally:   powershell -ExecutionPolicy Bypass -File tests\check_gl_conformance.ps1
# Run in CI:     .github/workflows/ci.yml "GL conformance audit" step (pwsh).

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot   # tests/ -> repo root
$scanRoots = @(
    (Join-Path $repoRoot 'Cosmic\src'),
    (Join-Path $repoRoot 'Projects'),
    (Join-Path $repoRoot 'tests')
)

# gl call (glFoo( ) or GL enum/typeish token (GL_FOO). GLfloat-style types do
# not appear engine-side; enums + calls are the leak vectors.
$pattern = '\bgl[A-Z][A-Za-z0-9]*\s*\(|\bGL_[A-Z0-9_]+\b'

# Line-level comment filter: full-line //, *, or /* comments are documentation
# citing GL by name, which is allowed. Inline trailing comments after code are
# NOT filtered — a violation hiding there is still code on that line.
$commentLine = '^\s*(//|\*|/\*)'

$violations = @()
foreach ($root in $scanRoots)
{
    if (-not (Test-Path $root)) { continue }

    $files = Get-ChildItem -Path $root -Recurse -Include *.cpp, *.h |
        Where-Object { $_.FullName -notmatch '\\platform\\OpenGL\\' }

    foreach ($file in $files)
    {
        # -CaseSensitive is load-bearing: without it glfw* (the windowing layer's
        # own API), ".glsl (", and prose like "glow (" all false-positive.
        $hits = Select-String -Path $file.FullName -Pattern $pattern -CaseSensitive
        foreach ($hit in $hits)
        {
            if ($hit.Line -match $commentLine) { continue }
            $rel = $hit.Path.Substring($repoRoot.Length + 1)
            $violations += ('{0}:{1}: {2}' -f $rel, $hit.LineNumber, $hit.Line.Trim())
        }
    }
}

# ---------------------------------------------------------------------------
# Pass 2 (UX-V0 / KI-82): GLSL sampler-array indexing.
#
# FAILS (exit 1) when a `uniform sampler*` array declared in a *.glsl file is
# indexed by anything but an integer literal (u_Textures[int(v_TexIndex)],
# u_Textures[i], u_Textures[N - 1], ...). GLSL 1.30-3.30 allow only constant
# expressions there, and Mesa (VMware/VirtualBox VMs, Intel and AMD on Linux)
# rejects such a shader outright; GLSL 4.00+ also require the index to be
# dynamically uniform, which a per-quad texture slot never is, so the result
# is undefined even where a driver accepts it. The conformant pattern is a
# flat integer slot plus a switch over literal indices (see
# Cosmic/assets/shaders/Texture.glsl). A const variable would be legal GLSL but
# is still reported: literals only keeps the rule trivially checkable.
#
# Scanned:  every *.glsl under the repo root
# Exempt:   directories named .git, build, out, dependencies, vendor, extern,
#           third_party, node_modules (build-output copies and vendored code)
#           Comment text (// to end of line, /* ... */ blocks)
# ---------------------------------------------------------------------------
$glslSkipDirs = @('.git', 'build', 'out', 'dependencies', 'vendor', 'extern', 'third_party', 'node_modules')

function Get-GlslFiles([string]$dir)
{
    foreach ($item in (Get-ChildItem -LiteralPath $dir -Force -ErrorAction SilentlyContinue))
    {
        if ($item.PSIsContainer)
        {
            if ($glslSkipDirs -contains $item.Name) { continue }
            Get-GlslFiles $item.FullName
        }
        elseif ($item.Extension -ieq '.glsl')
        {
            $item
        }
    }
}

# uniform [layout/qualifiers] [iu]sampler<kind> <name> [   -> group 1 = name
$samplerDecl = '\buniform\b[^;]*?\b[iu]?sampler[A-Za-z0-9_]*\s+([A-Za-z_][A-Za-z0-9_]*)\s*\['
$literalIndex = '^\s*(0[xX][0-9A-Fa-f]+|[0-9]+)[uU]?\s*$'

$glslFiles = @(Get-GlslFiles $repoRoot)
$samplerViolations = @()
foreach ($file in $glslFiles)
{
    $text = [System.IO.File]::ReadAllText($file.FullName)
    # Blank out /* ... */ (keeping newlines so line numbers hold), then // tails.
    $text = [regex]::Replace($text, '/\*.*?\*/',
        [System.Text.RegularExpressions.MatchEvaluator]{ param($m) $m.Value -replace '[^\n]', ' ' },
        [System.Text.RegularExpressions.RegexOptions]::Singleline)
    $lines = $text -split "`n"
    $rawLines = [System.IO.File]::ReadAllLines($file.FullName)
    for ($i = 0; $i -lt $lines.Count; $i++) { $lines[$i] = ($lines[$i] -replace '//.*$', '').TrimEnd("`r") }

    $names = @()
    foreach ($m in [regex]::Matches(($lines -join "`n"), $samplerDecl)) { $names += $m.Groups[1].Value }
    $names = @($names | Select-Object -Unique)
    if ($names.Count -eq 0) { continue }

    for ($i = 0; $i -lt $lines.Count; $i++)
    {
        $line = $lines[$i]
        foreach ($name in $names)
        {
            # The declaration's own [N] is the array size, not an index.
            $decl = [regex]::Match($line, $samplerDecl)
            if ($decl.Success -and $decl.Groups[1].Value -ceq $name) { continue }

            foreach ($use in [regex]::Matches($line, '\b' + [regex]::Escape($name) + '\s*\['))
            {
                # Bracket contents up to the matching ']' (nested brackets allowed).
                $start = $use.Index + $use.Length
                $depth = 1
                $j = $start
                while ($j -lt $line.Length -and $depth -gt 0)
                {
                    if ($line[$j] -eq '[') { $depth++ }
                    elseif ($line[$j] -eq ']') { $depth-- }
                    $j++
                }
                $index = if ($depth -eq 0) { $line.Substring($start, $j - 1 - $start) } else { $line.Substring($start) }
                if ($index -notmatch $literalIndex)
                {
                    $rel = $file.FullName.Substring($repoRoot.Length + 1)
                    $shown = if ($i -lt $rawLines.Count) { $rawLines[$i].Trim() } else { $line.Trim() }
                    $samplerViolations += ('{0}:{1}: {2}[{3}] -> {4}' -f $rel, ($i + 1), $name, $index.Trim(), $shown)
                }
            }
        }
    }
}

if ($glslFiles.Count -eq 0)
{
    Write-Host 'GLSL SAMPLER-INDEX FAILURE - no *.glsl file found under the repo root; the scan itself is broken.'
    $samplerViolations += '(no *.glsl scanned)'
}
elseif ($samplerViolations.Count -gt 0)
{
    Write-Host 'GLSL SAMPLER-INDEX FAILURE - a uniform sampler array indexed by a non-literal expression (KI-82):'
    $samplerViolations | ForEach-Object { Write-Host "  $_" }
    Write-Host ('{0} violation(s). Index sampler arrays with integer literals only (a flat slot + switch, see Cosmic/assets/shaders/Texture.glsl).' -f $samplerViolations.Count)
}
else
{
    Write-Host ('GLSL sampler indexing: clean ({0} *.glsl file(s) scanned, no sampler array indexed by a non-literal).' -f $glslFiles.Count)
}

if ($violations.Count -gt 0)
{
    Write-Host 'GL CONFORMANCE FAILURE - raw GL tokens outside platform/OpenGL/ (doc 05 rule 0.1):'
    $violations | ForEach-Object { Write-Host "  $_" }
    Write-Host ('{0} violation(s). Promote the operation to a RendererAPI/RenderCommand verb instead.' -f $violations.Count)
    exit 1
}

Write-Host 'GL conformance: clean (no raw gl*/GL_* tokens outside the platform layer).'
if ($samplerViolations.Count -gt 0) { exit 1 }
exit 0
