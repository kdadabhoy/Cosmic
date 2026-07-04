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

if ($violations.Count -gt 0)
{
    Write-Host 'GL CONFORMANCE FAILURE - raw GL tokens outside platform/OpenGL/ (doc 05 rule 0.1):'
    $violations | ForEach-Object { Write-Host "  $_" }
    Write-Host ('{0} violation(s). Promote the operation to a RendererAPI/RenderCommand verb instead.' -f $violations.Count)
    exit 1
}

Write-Host 'GL conformance: clean (no raw gl*/GL_* tokens outside the platform layer).'
exit 0
