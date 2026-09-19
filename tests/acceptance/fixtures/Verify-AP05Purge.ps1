<#
.SYNOPSIS
    AP-05 purge oracle - the grep / file half of acceptance case B06 (App Platform packet,
    01-Design-Contracts.md section 9).

.DESCRIPTION
    Five checks over the source tree, each PASS / FAIL (or INFO where -AllowFences says so):

      1. paths-absent        none of the Part-A paths (trees, engine files, vendored deps, tests,
                             goldens, editor TUs, template scripts, build script, assets) exists.
      2. build-files-clean   no non-comment line of the root, engine, tests, render-tests or
                             Starforge CMakeLists, or of CMakePresets.json, still names a purged
                             dependency dir, an if(NOT COSMIC_2D_ONLY) block, a list(FILTER)
                             partition rule for a purged tree/TU, a purged test TU, render_3d.cpp,
                             a purged editor TU, or a 3D (COSMIC_2D_ONLY=OFF / "default") preset.
      3. fence-uses          "#if / #ifdef / #ifndef / #elif ... COSMIC_2D_ONLY" and
                             "defined(COSMIC_2D_ONLY)" under Cosmic/src, Projects, tests.
                             Zero is the B06 bar; with -AllowFences (Part A) the count is INFO.
      4. includes-of-deleted an #include of a purged header or tree in 2D-compiled code
                             (Cosmic/src, Projects, tests, Runtime, Cosmic/templates). An include
                             inside an "#ifndef COSMIC_2D_ONLY" region is dead text the compiler
                             never sees; with -AllowFences those are INFO, any other one is FAIL.
                             Without -AllowFences every such include is FAIL.
      5. identifiers         grep -rniE "Renderer3D|Terrain|Voxel|NavMesh|assimp|Recast|
                             EnvironmentMap|ShadowMap" Cosmic/src -> the B06 bar is zero hits
                             outside a "History:" comment note. With -AllowFences hits inside
                             excluded fence regions are ignored and the remaining hits are
                             INFO (counted: comment vs code); without it any non-History hit
                             is FAIL. The physics verb "SphereCast" contains the substring
                             "recast" and is masked before matching (documented false positive).

    The Part-A path list is built in (see $script:DefaultDeletedPaths) and can be replaced or
    extended with -DeletedPaths / -DeletedPathsFile so Part B and AP-Q1 reuse the oracle.
    Prints a doctest-style "test cases: N | P passed | F failed" line for the acceptance runner's
    minTests parse. Exit 0 when nothing FAILED, 1 otherwise, 3 on a usage error.

.NOTES
    Windows PowerShell 5.1, pure ASCII, no absolute user paths.
#>
[CmdletBinding()]
param(
    [string]$Repo,                      # repo root; default resolved from this script's location
    [string[]]$DeletedPaths = @(),      # extra / replacement Part-A paths (repo-relative, / or \)
    [string]$DeletedPathsFile,          # one repo-relative path per line; '#' comments allowed
    [switch]$NoDefaultPaths,            # use only -DeletedPaths / -DeletedPathsFile
    [switch]$AllowFences,               # Part A: fences tolerated (see DESCRIPTION)
    [int]$MaxListed = 25                # how many offending lines each check prints
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $Repo) {
    $Repo = (Resolve-Path (Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) '..\..\..')).Path
}
$Repo = $Repo -replace '/', '\'
if (-not (Test-Path -LiteralPath (Join-Path $Repo 'Cosmic\src\Cosmic.h'))) {
    Write-Host "[FATAL] $Repo does not look like the Cosmic repository (no Cosmic/src/Cosmic.h)"
    exit 3
}
$Repo = (Resolve-Path -LiteralPath $Repo).Path.TrimEnd('\')

# --- The Part-A path list (01-Design-Contracts.md section 9, Part A) ----------------------
$script:DefaultDeletedPaths = @(
    # engine trees
    'Cosmic/src/terrain', 'Cosmic/src/voxel', 'Cosmic/src/water', 'Cosmic/src/nav', 'Cosmic/src/particles',
    # engine files (exactly the former list(FILTER) block)
    'Cosmic/src/renderer/Renderer3D.h', 'Cosmic/src/renderer/Renderer3D.cpp',
    'Cosmic/src/renderer/EnvironmentMap.h', 'Cosmic/src/renderer/EnvironmentMap.cpp',
    'Cosmic/src/renderer/ShadowMap.h', 'Cosmic/src/renderer/ShadowMap.cpp',
    'Cosmic/src/renderer/CoverageCapture.h', 'Cosmic/src/renderer/CoverageCapture.cpp',
    'Cosmic/src/renderer/InstanceSet.h', 'Cosmic/src/renderer/InstanceSet.cpp',
    'Cosmic/src/graphics/Model.h', 'Cosmic/src/graphics/Model.cpp',
    'Cosmic/src/graphics/Skeleton.h', 'Cosmic/src/graphics/Skeleton.cpp',
    'Cosmic/src/graphics/AnimationClip.h', 'Cosmic/src/graphics/AnimationClip.cpp',
    'Cosmic/src/graphics/CgltfImpl.cpp',
    'Cosmic/src/camera/NavigationCube.h', 'Cosmic/src/camera/NavigationCube.cpp',
    'Cosmic/src/scene/Scene3D.cpp', 'Cosmic/src/scene/Components3D.h',
    'Cosmic/src/scene/SceneNav.h', 'Cosmic/src/scene/SceneNav.cpp',
    'Cosmic/src/scene/ScenePicker.h', 'Cosmic/src/scene/ScenePicker.cpp',
    'Cosmic/src/scene/WorldSystemRecipes.h', 'Cosmic/src/scene/WorldSystemRecipes.cpp',
    'Cosmic/src/reflect/TypeRegistry3D.cpp',
    'Cosmic/src/assets/MeshImport.h', 'Cosmic/src/assets/MeshImport.cpp',
    # vendored deps
    'Cosmic/dependencies/recastnavigation', 'Cosmic/dependencies/assimp', 'Cosmic/dependencies/cgltf',
    # engine assets referenced only by the deleted code
    'Cosmic/assets/models/Duck.glb',
    'Cosmic/assets/shaders/BrdfLut.glsl', 'Cosmic/assets/shaders/DemoChecker3D.glsl',
    'Cosmic/assets/shaders/EnvSky.glsl', 'Cosmic/assets/shaders/EquirectToCube.glsl',
    'Cosmic/assets/shaders/InfiniteGrid.glsl', 'Cosmic/assets/shaders/IrradianceConvolve.glsl',
    'Cosmic/assets/shaders/Line3D.glsl', 'Cosmic/assets/shaders/Mesh3D.glsl',
    'Cosmic/assets/shaders/PBRInstanced.glsl', 'Cosmic/assets/shaders/ParticleBillboards.glsl',
    'Cosmic/assets/shaders/ParticleUpdate.glsl', 'Cosmic/assets/shaders/PrefilterEnv.glsl',
    'Cosmic/assets/shaders/Ribbon.glsl', 'Cosmic/assets/shaders/ShadowDepth.glsl',
    'Cosmic/assets/shaders/ShadowDepthInstanced.glsl', 'Cosmic/assets/shaders/ShadowDepthSkinned.glsl',
    'Cosmic/assets/shaders/SkyDetail.glsl', 'Cosmic/assets/shaders/Skybox.glsl',
    'Cosmic/assets/shaders/SnowAccum.glsl', 'Cosmic/assets/shaders/Terrain.glsl',
    'Cosmic/assets/shaders/TerrainDepth.glsl', 'Cosmic/assets/shaders/Water.glsl',
    # tests
    'tests/test_phase10_world.cpp', 'tests/test_flycamera.cpp', 'tests/test_frustum.cpp',
    'tests/test_presets.cpp', 'tests/test_particle_noise.cpp', 'tests/test_render_queue.cpp',
    'tests/test_primitives.cpp', 'tests/test_meshimport.cpp', 'tests/test_animation.cpp',
    'tests/test_sockets.cpp', 'tests/test_crossfade.cpp', 'tests/test_material_slots.cpp',
    'tests/test_worldsystems.cpp', 'tests/test_physics_terrain.cpp', 'tests/test_nav_world.cpp',
    'tests/test_nav_bake.cpp', 'tests/test_nav_agents.cpp', 'tests/test_voxel.cpp',
    'tests/test_voxel_collision.cpp', 'tests/test_forgeisle_content.cpp', 'tests/test_render_desc.cpp',
    'tests/test_components3d_registry.cpp', 'tests/render/render_3d.cpp',
    # goldens referenced only by render_3d.cpp
    'tests/render/goldens/instancing.png', 'tests/render/goldens/mesh_pbr.png',
    'tests/render/goldens/outline.png', 'tests/render/goldens/particles.png',
    'tests/render/goldens/postchain_off.png', 'tests/render/goldens/postchain_on.png',
    'tests/render/goldens/sky_ibl.png', 'tests/render/goldens/terrain.png', 'tests/render/goldens/water.png',
    # editor TUs
    'Projects/Starforge/src/panels/WorldSystemsPanel.h', 'Projects/Starforge/src/panels/WorldSystemsPanel.cpp',
    'Projects/Starforge/src/panels/VoxelPanel.h', 'Projects/Starforge/src/panels/VoxelPanel.cpp',
    'Projects/Starforge/src/editors/AnimationEditor.h', 'Projects/Starforge/src/editors/AnimationEditor.cpp',
    # template scripts
    'Projects/Starforge/assets/templates/src/scripts/VoxelDigger.h',
    'Projects/Starforge/assets/templates/src/scripts/NavCritter.h',
    # build / scripts
    'build_3d.bat'
)

$paths = @()
if (-not $NoDefaultPaths) { $paths += $script:DefaultDeletedPaths }
if ($DeletedPathsFile) {
    if (-not (Test-Path -LiteralPath $DeletedPathsFile)) { Write-Host "[FATAL] -DeletedPathsFile not found: $DeletedPathsFile"; exit 3 }
    foreach ($l in @(Get-Content -LiteralPath $DeletedPathsFile)) {
        if ($null -eq $l) { continue }
        $t = $l.Trim()
        if ($t -and -not $t.StartsWith('#')) { $paths += $t }
    }
}
$paths += $DeletedPaths
$paths = @($paths | ForEach-Object { ($_ -replace '\\', '/').Trim('/') } | Select-Object -Unique)
if ($paths.Count -eq 0) { Write-Host '[FATAL] the deleted-path list is empty'; exit 3 }

# --- helpers ---------------------------------------------------------------------------------
$script:results = @()
function Add-Result([string]$name, [string]$verdict, [string]$detail) {
    $script:results += [pscustomobject]@{ Name = $name; Verdict = $verdict; Detail = $detail }
    $tag = '[{0}]' -f $verdict.PadRight(4)
    Write-Host ('{0} {1}: {2}' -f $tag, $name, $detail)
}
function Show-Lines($lines, [string]$label) {
    $arr = @($lines)
    if ($arr.Count -eq 0) { return }
    Write-Host ('       {0} ({1}):' -f $label, $arr.Count)
    $shown = 0
    foreach ($l in $arr) {
        if ($shown -ge $MaxListed) { Write-Host ('         ... {0} more' -f ($arr.Count - $shown)); break }
        Write-Host ('         ' + $l)
        $shown++
    }
}
function Get-CodeFiles([string[]]$roots, [string[]]$exts) {
    $out = @()
    foreach ($r in $roots) {
        $full = Join-Path $Repo $r
        if (-not (Test-Path -LiteralPath $full)) { continue }
        $out += @(Get-ChildItem -LiteralPath $full -Recurse -File -Include $exts |
            Where-Object { $_.FullName -notmatch '\\(build|_results|_temp)\\' })
    }
    # Plain return on purpose: the pipeline unrolls the array into one FileInfo per
    # iteration for the caller's @(...) foreach (a ",$out" would hand it ONE element).
    return $out
}
function Get-Rel([string]$full) { return ($full.Substring($Repo.Length + 1) -replace '\\', '/') }
function Read-Lines([string]$full) {
    # Always an array (a one-line file would otherwise come back as a bare string).
    $raw = Get-Content -LiteralPath $full
    if ($null -eq $raw) { return ,@() }
    return ,@($raw)
}

# Fence state per line: EXCLUDED = inside an "#ifndef COSMIC_2D_ONLY" / "#if !defined(COSMIC_2D_ONLY)"
# (or "... && !defined(COSMIC_2D_ONLY)") true branch -> never compiled on this trunk.
# INCLUDED = inside an "#ifdef COSMIC_2D_ONLY" / "#if defined(COSMIC_2D_ONLY)" true branch or the
# #else of an excluded block. UNFENCED = everything else. Returns one state string per line;
# preprocessor directive lines themselves are 'DIRECTIVE'.
function Get-FenceStates($lines) {
    $lines = @($lines)
    $states = New-Object string[] $lines.Count
    $stack = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = [string]$lines[$i]
        $st = 'UNFENCED'
        foreach ($s in $stack) { if ($s -eq 'EXCLUDED') { $st = 'EXCLUDED'; break } }
        if ($st -ne 'EXCLUDED') { foreach ($s in $stack) { if ($s -eq 'INCLUDED') { $st = 'INCLUDED'; break } } }
        $states[$i] = $st
        if ($line -match '^\s*#\s*ifndef\s+COSMIC_2D_ONLY\b' -or
            $line -match '^\s*#\s*if\s+!\s*defined\s*\(\s*COSMIC_2D_ONLY\s*\)\s*$' -or
            $line -match '^\s*#\s*if\b.*&&\s*!\s*defined\s*\(\s*COSMIC_2D_ONLY\s*\)') {
            $stack.Add('EXCLUDED'); $states[$i] = 'DIRECTIVE'; continue
        }
        if ($line -match '^\s*#\s*ifdef\s+COSMIC_2D_ONLY\b' -or
            $line -match '^\s*#\s*if\s+defined\s*\(\s*COSMIC_2D_ONLY\s*\)\s*$') {
            $stack.Add('INCLUDED'); $states[$i] = 'DIRECTIVE'; continue
        }
        if ($line -match '^\s*#\s*(if|ifdef|ifndef)\b') { $stack.Add('NEUTRAL'); $states[$i] = 'DIRECTIVE'; continue }
        if ($line -match '^\s*#\s*else\b') {
            if ($stack.Count -gt 0) {
                $top = $stack[$stack.Count - 1]
                if ($top -eq 'EXCLUDED') { $stack[$stack.Count - 1] = 'INCLUDED' }
                elseif ($top -eq 'INCLUDED') { $stack[$stack.Count - 1] = 'EXCLUDED' }
            }
            $states[$i] = 'DIRECTIVE'; continue
        }
        if ($line -match '^\s*#\s*elif\b') { $states[$i] = 'DIRECTIVE'; continue }
        if ($line -match '^\s*#\s*endif\b') {
            if ($stack.Count -gt 0) { $stack.RemoveAt($stack.Count - 1) }
            $states[$i] = 'DIRECTIVE'; continue
        }
    }
    return ,$states
}

$srcExts = @('*.cpp', '*.h', '*.hpp', '*.inl', '*.in')

# --- 1. paths-absent -------------------------------------------------------------------------
$present = @()
foreach ($p in $paths) {
    if (Test-Path -LiteralPath (Join-Path $Repo ($p -replace '/', '\'))) { $present += $p }
}
if ($present.Count -eq 0) {
    Add-Result 'paths-absent' 'PASS' ('none of the {0} Part-A paths exists' -f $paths.Count)
} else {
    Add-Result 'paths-absent' 'FAIL' ('{0} of {1} Part-A paths still exist' -f $present.Count, $paths.Count)
    Show-Lines $present 'still present'
}

# --- 2. build-files-clean --------------------------------------------------------------------
# Tokens derived from the path list: purged dependency dirs, engine tree names, engine / editor
# TU stems and test TU file names. Matched case-sensitively on non-comment lines only.
$depDirs = @(); $engineTrees = @(); $engineStems = @(); $testTus = @(); $editorStems = @()
foreach ($p in $paths) {
    $leaf = Split-Path -Leaf $p
    $isTree = ($p -notmatch '\.[A-Za-z0-9]+$')
    if ($p.StartsWith('Cosmic/dependencies/') -and $isTree) { $depDirs += [regex]::Escape($p.Substring('Cosmic/'.Length)) }
    elseif ($p.StartsWith('Cosmic/src/') -and $isTree) { $engineTrees += [regex]::Escape($leaf) }
    elseif ($p.StartsWith('Cosmic/src/') -and $leaf -match '\.(cpp|h)$') { $engineStems += [regex]::Escape(($leaf -replace '\.(cpp|h)$', '')) }
    elseif ($p.StartsWith('tests/') -and $leaf -match '\.cpp$') { $testTus += [regex]::Escape($leaf) }
    elseif ($p.StartsWith('Projects/Starforge/src/') -and $leaf -match '\.(cpp|h)$') { $editorStems += [regex]::Escape(($leaf -replace '\.(cpp|h)$', '')) }
}
$engineStems = @($engineStems | Select-Object -Unique)
$editorStems = @($editorStems | Select-Object -Unique)
$enginePatterns = @('if\s*\(\s*NOT\s+COSMIC_2D_ONLY\s*\)', '\bRecastNavigation\b', 'COSMIC_WITH_ASSIMP', '\bassimp\b') + @($depDirs)
if ($engineTrees.Count -gt 0) { $enginePatterns += ('list\s*\(\s*FILTER\b.*(' + ($engineTrees -join '|') + ')') }
if ($engineStems.Count -gt 0) { $enginePatterns += ('list\s*\(\s*FILTER\b.*\b(' + ($engineStems -join '|') + ')\b') }
$testPatterns = @('if\s*\(\s*NOT\s+COSMIC_2D_ONLY\s*\)') + @($testTus)
$editorPatterns = @('list\s*\(\s*FILTER\s+STARFORGE_SOURCES')
if ($editorStems.Count -gt 0) { $editorPatterns += ('\b(' + ($editorStems -join '|') + ')\b') }
$buildChecks = @(
    @{ File = 'CMakeLists.txt';                    Patterns = @($depDirs) },
    @{ File = 'Cosmic/CMakeLists.txt';             Patterns = $enginePatterns },
    @{ File = 'tests/CMakeLists.txt';              Patterns = $testPatterns },
    @{ File = 'tests/render/CMakeLists.txt';       Patterns = @('render_3d\.cpp') },
    @{ File = 'Projects/Starforge/CMakeLists.txt'; Patterns = $editorPatterns },
    @{ File = 'CMakePresets.json';                 Patterns = @('"COSMIC_2D_ONLY"\s*:\s*"OFF"', '"name"\s*:\s*"default"') }
)
$buildHits = @()
foreach ($bc in $buildChecks) {
    $full = Join-Path $Repo ($bc.File -replace '/', '\')
    if (-not (Test-Path -LiteralPath $full)) { $buildHits += ('{0}: file missing' -f $bc.File); continue }
    $lines = Read-Lines $full
    $isCMake = ($bc.File -like '*CMakeLists.txt')
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $l = [string]$lines[$i]
        # CMake comment lines are prose, not build surface; JSON has no comments.
        if ($isCMake -and $l -match '^\s*#') { continue }
        foreach ($pat in @($bc.Patterns)) {
            if ($l -cmatch $pat) { $buildHits += ('{0}:{1}: {2}' -f $bc.File, ($i + 1), $l.Trim()); break }
        }
    }
}
$presetText = Get-Content -LiteralPath (Join-Path $Repo 'CMakePresets.json') -Raw
if ($presetText -notmatch '"name"\s*:\s*"2d"') { $buildHits += 'CMakePresets.json: the "2d" configure preset is missing' }
if ($buildHits.Count -eq 0) {
    Add-Result 'build-files-clean' 'PASS' 'no CMake/preset line names a purged dep dir, partition rule, 3D test block, render_3d.cpp, purged editor TU or 3D preset'
} else {
    Add-Result 'build-files-clean' 'FAIL' ('{0} build-file reference(s) to purged surface' -f $buildHits.Count)
    Show-Lines $buildHits 'references'
}

# --- 3. fence-uses ---------------------------------------------------------------------------
$fenceHits = @()
foreach ($f in @(Get-CodeFiles @('Cosmic/src', 'Projects', 'tests') $srcExts)) {
    $lines = Read-Lines $f.FullName
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $l = [string]$lines[$i]
        if ($l -match '^\s*#\s*(if|ifdef|ifndef|elif)\b.*\bCOSMIC_2D_ONLY\b' -or $l -match 'defined\s*\(\s*COSMIC_2D_ONLY\s*\)') {
            $fenceHits += ('{0}:{1}: {2}' -f (Get-Rel $f.FullName), ($i + 1), $l.Trim())
        }
    }
}
if ($fenceHits.Count -eq 0) {
    Add-Result 'fence-uses' 'PASS' 'zero COSMIC_2D_ONLY preprocessor uses under Cosmic/src, Projects, tests'
} elseif ($AllowFences) {
    Add-Result 'fence-uses' 'INFO' ('{0} COSMIC_2D_ONLY preprocessor use(s) remain (tolerated by -AllowFences; part B removes them)' -f $fenceHits.Count)
} else {
    Add-Result 'fence-uses' 'FAIL' ('{0} COSMIC_2D_ONLY preprocessor use(s) remain' -f $fenceHits.Count)
    Show-Lines $fenceHits 'uses'
}

# --- 4. includes-of-deleted ------------------------------------------------------------------
# An include matches a purged path when its spelling ends with the path relative to one of the
# roots an engine / editor / template include resolves against, or names a file inside a purged tree.
$deletedFileKeys = @()
$deletedTreeKeys = @()
foreach ($p in $paths) {
    $isTree = ($p -notmatch '\.[A-Za-z0-9]+$')
    $rel = $p
    foreach ($prefix in @('Cosmic/src/', 'Projects/Starforge/src/', 'Projects/Starforge/assets/templates/src/', 'tests/render/', 'tests/')) {
        if ($rel.StartsWith($prefix)) { $rel = $rel.Substring($prefix.Length); break }
    }
    if ($isTree) { $deletedTreeKeys += ($rel.TrimEnd('/') + '/') } else { $deletedFileKeys += $rel }
}
$incUnfenced = @()
$incFenced = @()
foreach ($f in @(Get-CodeFiles @('Cosmic/src', 'Projects', 'tests', 'Runtime', 'Cosmic/templates') $srcExts)) {
    $lines = Read-Lines $f.FullName
    if ($lines.Count -eq 0) { continue }
    $states = Get-FenceStates $lines
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $l = [string]$lines[$i]
        if ($l -notmatch '^\s*#\s*include\s+["<]([^">]+)[">]') { continue }
        $inc = ($matches[1] -replace '\\', '/')
        $hit = $false
        foreach ($k in $deletedFileKeys) { if ($inc -eq $k -or $inc.EndsWith('/' + $k)) { $hit = $true; break } }
        if (-not $hit) { foreach ($k in $deletedTreeKeys) { if ($inc.StartsWith($k) -or $inc.Contains('/' + $k)) { $hit = $true; break } } }
        if (-not $hit) { continue }
        $entry = ('{0}:{1}: {2}' -f (Get-Rel $f.FullName), ($i + 1), $l.Trim())
        if ($states[$i] -eq 'EXCLUDED') { $incFenced += $entry } else { $incUnfenced += $entry }
    }
}
if ($incUnfenced.Count -eq 0 -and $incFenced.Count -eq 0) {
    Add-Result 'includes-of-deleted' 'PASS' 'no #include of a purged header or tree anywhere'
} elseif ($incUnfenced.Count -eq 0 -and $AllowFences) {
    Add-Result 'includes-of-deleted' 'PASS' ('no 2D-compiled #include of a purged header; {0} dead include(s) inside excluded fences remain (tolerated by -AllowFences)' -f $incFenced.Count)
} else {
    $n = $incUnfenced.Count
    if (-not $AllowFences) { $n += $incFenced.Count }
    Add-Result 'includes-of-deleted' 'FAIL' ('{0} #include(s) of a purged header or tree' -f $n)
    Show-Lines $incUnfenced 'compiled (unfenced) includes'
    if (-not $AllowFences) { Show-Lines $incFenced 'fenced includes' }
}

# --- 5. identifiers --------------------------------------------------------------------------
$identPattern = 'Renderer3D|Terrain|Voxel|NavMesh|assimp|Recast|EnvironmentMap|ShadowMap'
$idFenced = @(); $idHistory = @(); $idComment = @(); $idCode = @()
foreach ($f in @(Get-CodeFiles @('Cosmic/src') $srcExts)) {
    $lines = Read-Lines $f.FullName
    if ($lines.Count -eq 0) { continue }
    $states = Get-FenceStates $lines
    $inHistory = $false
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $l = [string]$lines[$i]
        $isComment = ($l -match '^\s*(//|\*|/\*)')
        if ($isComment -and $l -match 'History:') { $inHistory = $true }
        elseif (-not $isComment) { $inHistory = $false }
        # "SphereCast" (physics) contains "recast": mask it so the verbatim B06 regex does not
        # count it. Nothing else in the pattern has a known benign superstring.
        $probe = $l -replace 'SphereCast', 'SphereXast'
        if ($probe -notmatch $identPattern) { continue }
        $entry = ('{0}:{1}: {2}' -f (Get-Rel $f.FullName), ($i + 1), $l.Trim())
        if ($states[$i] -eq 'EXCLUDED') { $idFenced += $entry }
        elseif ($inHistory) { $idHistory += $entry }
        elseif ($isComment) { $idComment += $entry }
        else { $idCode += $entry }
    }
}
$idViolations = @($idComment) + @($idCode)
if (-not $AllowFences) { $idViolations = @($idFenced) + $idViolations }
if ($idViolations.Count -eq 0) {
    Add-Result 'identifiers' 'PASS' ('zero 3D identifiers in Cosmic/src outside History notes ({0} under a History: note)' -f $idHistory.Count)
} elseif ($AllowFences) {
    Add-Result 'identifiers' 'INFO' ('3D identifier mentions remain in Cosmic/src: {0} inside excluded fences (ignored), {1} in unfenced comments, {2} on unfenced code lines, {3} under History: notes (tolerated by -AllowFences; part B / AP-Q1 clean them up)' -f $idFenced.Count, $idComment.Count, $idCode.Count, $idHistory.Count)
    Show-Lines $idCode 'unfenced code lines'
} else {
    Add-Result 'identifiers' 'FAIL' ('{0} 3D identifier mention(s) in Cosmic/src outside History: notes ({1} fenced, {2} comment, {3} code)' -f $idViolations.Count, $idFenced.Count, $idComment.Count, $idCode.Count)
    Show-Lines $idCode 'code lines'
    Show-Lines $idComment 'comment lines'
    Show-Lines $idFenced 'fenced lines'
}

# --- summary ---------------------------------------------------------------------------------
$failed = @($script:results | Where-Object { $_.Verdict -eq 'FAIL' }).Count
$total = @($script:results).Count
$passed = $total - $failed
Write-Host ''
$modeText = 'B06 strict'
if ($AllowFences) { $modeText = 'part-A (-AllowFences)' }
Write-Host ('[AP05] mode={0}  repo={1}  paths={2}' -f $modeText, $Repo, $paths.Count)
Write-Host ('[doctest] test cases: {0} | {1} passed | {2} failed' -f $total, $passed, $failed)
if ($failed -gt 0) { exit 1 }
exit 0
