"""park_3d.py - AP-D1 step 4: park the 3D documentation under docs/parked-3d/.

  * git mv the wholly-3D chapters (guide x5, reference x2, systems x5) into docs/parked-3d/<tier>/
  * split the three MIXED skeleton chapters: the live file keeps the 2D scope, a parked twin gets the 3D scope
  * split docs/guide/lighting-and-environment.md: the 2D post-chain/RTT part stays as lighting-2d.md,
    the whole original is parked verbatim
  * move the root README's Part II 3D material (the "2D partition" subsection, the 3D lines of the
    source map, the Renderer3D nodes of DG-6, the COSMIC_2D_ONLY paragraph of section 40) verbatim into
    docs/parked-3d/README-part2-3d-systems.md, leaving one pointer paragraph in README.md
  * PARKED banner at line 3 of every parked file; docs/parked-3d/README.md index
  * write moves-parked.json for rewrite_links.py
"""
import json, os, re, subprocess

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..', '..'))
BANNER = ('> **PARKED 3D — not on the trunk.** This chapter documents code that lives only on the `engine-3d` '
          'branch (`0e8894b`, tag `cosmic-pre-2d-2026-09-16`). The 2D trunk (`main`) no longer builds or ships it '
          '(D-PURGE, 2026-09-18). Kept for when 3D resumes.')

WHOLE = {
    'docs/guide/rendering-3d.md': 'docs/parked-3d/guide/rendering-3d.md',
    'docs/guide/voxels.md': 'docs/parked-3d/guide/voxels.md',
    'docs/guide/navigation-and-ai.md': 'docs/parked-3d/guide/navigation-and-ai.md',
    'docs/guide/animation.md': 'docs/parked-3d/guide/animation.md',
    'docs/guide/world-systems.md': 'docs/parked-3d/guide/world-systems.md',
    'docs/reference/rendering-3d.md': 'docs/parked-3d/reference/rendering-3d.md',
    'docs/reference/world-systems.md': 'docs/parked-3d/reference/world-systems.md',
    'docs/systems/rendering-3d.md': 'docs/parked-3d/systems/rendering-3d.md',
    'docs/systems/terrain.md': 'docs/parked-3d/systems/terrain.md',
    'docs/systems/water.md': 'docs/parked-3d/systems/water.md',
    'docs/systems/particles.md': 'docs/parked-3d/systems/particles.md',
    'docs/systems/build-2d-3d-split.md': 'docs/parked-3d/systems/build-2d-3d-split.md',
}

def rd(rel):
    return open(os.path.join(ROOT, rel), encoding='utf-8', newline='').read()

def wr(rel, text):
    full = os.path.join(ROOT, rel)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    open(full, 'w', encoding='utf-8', newline='').write(text)

def with_banner(text):
    nl = '\r\n' if '\r\n' in text else '\n'
    L = text.split(nl)
    if any(l.startswith('> **PARKED 3D') for l in L[:6]):
        return text
    if len(L) < 2 or L[1].strip() != '':
        L.insert(1, '')
    L.insert(2, BANNER); L.insert(3, '')
    return nl.join(L)

def git_mv(src, dst):
    os.makedirs(os.path.dirname(os.path.join(ROOT, dst)), exist_ok=True)
    subprocess.check_call(['git', 'mv', src, dst], cwd=ROOT)

def main():
    moves = {}
    for src, dst in WHOLE.items():
        if os.path.exists(os.path.join(ROOT, src)):
            git_mv(src, dst)
        moves[src] = dst
        wr(dst, with_banner(rd(dst)))

    # ---- mixed skeleton chapters: live keeps 2D scope, parked twin gets the 3D scope ----------
    split_mixed()
    # ---- lighting-and-environment -> lighting-2d.md (live) + parked verbatim ------------------
    split_lighting(moves)
    # ---- root README Part II 3D material -> parked, pointer left behind -----------------------
    split_readme()
    # ---- index ---------------------------------------------------------------------------------
    write_index()
    json.dump(moves, open(os.path.join(os.path.dirname(__file__), 'moves-parked.json'), 'w'), indent=1)
    print('parked', len(moves), 'whole chapters + 3 mixed twins + lighting + README part II')

def split_mixed():
    # reference/rendering-pipeline.md
    p = 'docs/reference/rendering-pipeline.md'
    t = rd(p); nl = '\r\n' if '\r\n' in t else '\n'
    parked = with_banner(t.replace('# API Reference — Frame Pipeline (SceneRenderer, Post-Processing, Environment)',
                                    '# API Reference — Frame Pipeline, 3D half (EnvironmentMap, ShadowMap, CoverageCapture)', 1))
    parked = parked.replace('**Scope (headers are the truth):** `Cosmic/src/renderer/SceneRenderer.h`,' + nl + '`renderer/PostProcessStack.h`, ',
                            '**Scope (headers are the truth; all deleted from `main` by AP-05):** ', 1)
    wr('docs/parked-3d/reference/rendering-pipeline-3d.md', parked)
    live = t.replace('# API Reference — Frame Pipeline (SceneRenderer, Post-Processing, Environment)',
                     '# API Reference — Frame Pipeline (SceneRenderer, Post-Processing)', 1)
    live = re.sub(r'\*\*Scope \(headers are the truth\):\*\*.*?(?=' + re.escape(nl) + re.escape(nl) + r')',
                  '**Scope (headers are the truth):** `Cosmic/src/renderer/SceneRenderer.h`,' + nl +
                  '`renderer/PostProcessStack.h`. The 3D half of this chapter (`EnvironmentMap`, `ShadowMap`,' + nl +
                  '`CoverageCapture`; deleted from `main` by AP-05) is parked at' + nl +
                  '[`../parked-3d/reference/rendering-pipeline-3d.md`](../parked-3d/reference/rendering-pipeline-3d.md) (parked 3D).',
                  live, count=1, flags=re.S)
    live = strip_3d_bullets(live, ['EnvironmentMap', 'ShadowMap', 'CoverageCapture', 'Renderer3D', 'terrain', 'Terrain', 'water', 'Water', 'Emitter', 'IBL', 'sky', 'Sky', 'shadow', 'Shadow', 'reflection', 'Reflection'])
    wr(p, live)

    # systems/rendering-pipeline.md
    p = 'docs/systems/rendering-pipeline.md'
    t = rd(p); nl = '\r\n' if '\r\n' in t else '\n'
    parked = with_banner(t.replace('# Frame Pipeline & Post-Processing — How It Works',
                                    '# Frame Pipeline, 3D half (shadows, reflections, environment) — How It Works', 1))
    wr('docs/parked-3d/systems/rendering-pipeline-3d.md', parked)
    live = t.replace('**One-liner:** a frame is a *pipeline of passes* — shadows, reflections, the main HDR render',
                     '**One-liner:** a frame is a *pipeline of passes* — the main HDR render', 1)
    live = live.replace('# Frame Pipeline & Post-Processing — How It Works',
                        '# Frame Pipeline & Post-Processing — How It Works' + nl + nl +
                        '> **2D trunk (2026-09-20):** `SceneRenderer` and `PostProcessStack` are the live 2D spine (`BeginHDR → sprites + 2D lights → post → DrawOverlay2D`). The shadow, reflection, environment/IBL and sky passes this skeleton planned are parked at [`../parked-3d/systems/rendering-pipeline-3d.md`](../parked-3d/systems/rendering-pipeline-3d.md) (parked 3D).', 1)
    live = strip_3d_bullets(live, ['shadow', 'Shadow', 'reflection', 'Reflection', 'EnvironmentMap', 'IBL', 'sky', 'Sky', 'Renderer3D', 'terrain', 'water', 'lighting theory', 'PBR', 'BRDF', 'god ray', 'God ray'])
    wr(p, live)

    # systems/cameras-navigation.md
    p = 'docs/systems/cameras-navigation.md'
    t = rd(p); nl = '\r\n' if '\r\n' in t else '\n'
    parked = with_banner(t.replace('# Cameras & CAD Navigation — How It Works',
                                    '# CAD Navigation (orbit, fly, ViewCube, picking) — How It Works', 1))
    wr('docs/parked-3d/systems/cameras-navigation-3d.md', parked)
    live = t.replace('# Cameras & CAD Navigation — How It Works',
                     '# Cameras & 2D Navigation — How It Works' + nl + nl +
                     '> **2D trunk (2026-09-20):** `Camera`, `OrthographicCamera`, `Camera2DController` and the orthographic controller are the live surface (the perspective camera and the orbit/fly controllers still compile for the editor viewport). The `NavigationCube`, `ScenePicker` and 3D viewport picking parts are parked at [`../parked-3d/systems/cameras-navigation-3d.md`](../parked-3d/systems/cameras-navigation-3d.md) (parked 3D).', 1)
    live = strip_3d_bullets(live, ['NavigationCube', 'ViewCube', 'ScenePicker', 'pick', 'Pick', 'SolidWorks', 'orbit', 'Orbit', 'fly', 'Fly'])
    wr(p, live)

def strip_3d_bullets(text, keys):
    """Remove list items / table rows in the skeleton's plan sections that name a 3D-only thing."""
    nl = '\r\n' if '\r\n' in text else '\n'
    out = []
    for l in text.split(nl):
        s = l.strip()
        if (s.startswith('- ') or s.startswith('* ') or re.match(r'^\d+\.\s', s) or (s.startswith('|') and not s.startswith('| ---') and not s.startswith('| Section'))) and any(k in l for k in keys):
            # keep rows that also name a live 2D thing
            if any(k in l for k in ['SceneRenderer', 'PostProcessStack', 'Camera2D', 'Orthographic', 'Renderer2D', 'tonemap', 'Tonemap', 'FXAA', 'bloom', 'Bloom', 'vignette']):
                out.append(l); continue
            continue
        out.append(l)
    return nl.join(out)

def split_lighting(moves):
    src = 'docs/guide/lighting-and-environment.md'
    t = rd(src); nl = '\r\n' if '\r\n' in t else '\n'
    L = t.split(nl)
    def section(start_h, end_hs):
        i = next(k for k, l in enumerate(L) if l.startswith(start_h))
        j = i + 1
        while j < len(L) and not any(L[j].startswith(e) for e in end_hs):
            j += 1
        return L[i:j]
    post = section('## The post chain', ['## Rendering into a texture'])
    rtt = section('## Rendering into a texture', ['## Advanced'])
    # drop trailing '---' separators
    while rtt and rtt[-1].strip() in ('---', ''): rtt.pop()
    while post and post[-1].strip() in ('---', ''): post.pop()
    head = [
        '# Lighting & Post-Processing in 2D — Guide', '',
        '**What this covers:** what the engine-owned frame orchestrator `SceneRenderer` does for a 2D app on the',
        'trunk: the spine `BeginHDR → sprites + 2D lights via DrawTransparent → post chain → DrawOverlay2D`, the',
        'post-chain toggles on `SceneRenderDesc::Settings` (bloom, FXAA, tonemap/exposure/gamma, vignette, and the',
        'depth-based effects that still compile), `ApplyEnvironment` from an `EnvironmentComponent`, and',
        '`RenderToTexture`. The 2D *lights themselves* (`Light2DComponent`, `Ambient2D`, `Light2DRenderer`) are',
        'documented where they are used: [`sprites-and-tilemaps.md`](sprites-and-tilemaps.md) and',
        '[`rendering-2d.md`](rendering-2d.md).',
        '**Source of truth:** `Cosmic/src/renderer/SceneRenderer.{h,cpp}`, `renderer/PostProcessStack.{h,cpp}`,',
        '`renderer/Light2DRenderer.{h,cpp}`, `scene/Components.h` (`EnvironmentComponent`, `Light2DComponent`),',
        '`Cosmic/assets/shaders/Tonemap.glsl`.',
        '**API Reference:** [../reference/rendering-pipeline.md](../reference/rendering-pipeline.md) *(skeleton)* ·',
        '**How it works:** [../systems/rendering-pipeline.md](../systems/rendering-pipeline.md) *(skeleton)*', '',
        '> **History (2026-09-20, App Platform AP-D1).** This chapter was split out of `lighting-and-environment.md`,',
        '> which described the full 3D frame (sun and point lights, PBR + IBL, the four sky modes, time of day, shadows,',
        '> coverage capture). That code was deleted from `main` by AP-05 (D-PURGE) and the original chapter is kept',
        '> verbatim at [`../parked-3d/guide/lighting-and-environment.md`](../parked-3d/guide/lighting-and-environment.md)',
        '> (parked 3D). The sections below are the parts that apply to the 2D trunk, copied from it unchanged except',
        '> for this note; AP-D2 owns the rewrite. Rows and fields that name shadow maps, the sun, terrain, water or',
        '> emitters exist in the headers as 3D-era toggles kept for source compatibility and do nothing on `main`',
        '> (`SceneRenderer.h:106-108`); the god-rays pass was removed outright (AP-05 [B1]).', '',
        '## The 2D spine', '',
        '`SceneRenderer::Render(desc)` on the trunk runs, in order: clear to `Settings.ClearColor` → bind the HDR',
        'target (`PassOpaqueHDR`) → your `desc.DrawTransparent` callback, which is where `Scene::OnRenderSprites` draws',
        'sprites and tilemaps and `Scene::OnRender2DLights` multiplies the 2D light buffer over them →',
        '`PassPostAndComposite` (the table below) into the bound LDR viewport target → your `desc.DrawOverlay2D`',
        'callback for canvas UI, which post never touches (**UI is LDR**). The editor viewport and `PlayerLayer` both',
        'call `ApplyEnvironment(env, desc)` first so the scene’s single `Environment` entity drives exposure and the',
        'post toggles.', '',
        '```cpp',
        'SceneRenderDesc desc;',
        'desc.Camera     = &camera;              // any Camera; 2D apps pass the orthographic one',
        'desc.EcsScene   = m_Scene.get();',
        'if (auto* env = m_Scene->FindEnvironment()) m_Renderer.ApplyEnvironment(*env, desc);',
        'desc.DrawTransparent = [&](const SceneDrawContext& c) { m_Scene->OnRenderSprites(c); };',
        'desc.DrawOverlay2D   = [&] { m_Scene->OnRenderUi(); };',
        'm_Renderer.Render(desc);',
        '```', '',
    ]
    tail = ['', '---', '', '## See also', '',
            '- [`rendering-2d.md`](rendering-2d.md) — `Renderer2D`, `RenderPass`, and how sprites reach the HDR target',
            '- [`sprites-and-tilemaps.md`](sprites-and-tilemaps.md) — `Light2DComponent`, `Ambient2D`, sorting layers',
            '- [`materials-and-shaders.md`](materials-and-shaders.md) — the shader contract and `BindingPoints`',
            '- [`entities-and-components.md`](entities-and-components.md) — `EnvironmentComponent` field-by-field',
            '- [`../design/frame-lifecycle.md`](../design/frame-lifecycle.md) — the resource and render-state contract',
            '- [`../parked-3d/guide/lighting-and-environment.md`](../parked-3d/guide/lighting-and-environment.md) (parked 3D) — the full original chapter',
            '']
    live = nl.join(head + post + ['', '---', ''] + rtt + tail)
    # park the original verbatim (banner) then write the live 2D chapter at the new name
    git_mv(src, 'docs/parked-3d/guide/lighting-and-environment.md')
    wr('docs/parked-3d/guide/lighting-and-environment.md', with_banner(t))
    wr('docs/guide/lighting-2d.md', live)
    subprocess.check_call(['git', 'add', 'docs/guide/lighting-2d.md'], cwd=ROOT)
    moves[src] = 'docs/guide/lighting-2d.md'

def split_readme():
    p = 'README.md'
    t = rd(p); nl = '\r\n' if '\r\n' in t else '\n'
    L = t.split(nl)
    parked_parts = []
    # (a) "### The 2D partition" subsection, verbatim, up to (not including) the next '---'
    i = next(k for k, l in enumerate(L) if l.startswith('### The 2D partition'))
    j = i + 1
    while not L[j].startswith('---'): j += 1
    partition = L[i:j]
    pointer = ['### Where the 3D half went', '',
               'Until 2026-09-18 this section described the *2D partition*: how one source tree built two engines,',
               'which directories the `COSMIC_2D_ONLY` filter excluded and how the fences were classified. **`main` is',
               'now the 2D-only trunk (D-PURGE):** the 3D source, its vendored dependencies (assimp, recastnavigation),',
               'tests, goldens, editor panels and template scripts were deleted from this branch and are preserved on',
               '`engine-3d` (`0e8894b`, tag `cosmic-pre-2d-2026-09-16`). The partition table, the 3D lines of the source',
               'map above, the `Renderer3D` nodes of DG-6 and the old build-flag paragraph were moved verbatim to',
               '[`docs/parked-3d/README-part2-3d-systems.md`](docs/parked-3d/README-part2-3d-systems.md) (parked 3D);',
               'how to resume 3D is in [`docs/parked-3d/README.md`](docs/parked-3d/README.md) (parked 3D).', '']
    L[i:j] = pointer
    parked_parts.append(('README Part II §30 — "The 2D partition" (verbatim)', partition))
    # (b) 3D lines of the source map (inside the ``` block after "## §30 Source File Map")
    s = next(k for k, l in enumerate(L) if l.startswith('## §30 Source File Map'))
    fence = [k for k in range(s, len(L)) if L[k].startswith('```')]
    a, b = fence[0], fence[1]
    map_3d = [l for l in L[a + 1:b] if re.search(r'\b3D\b', l) and ('Renderer3D' in l or 'ShadowMap' in l or 'Model.*' in l or 'Scene3D' in l or 'Components3D' in l or 'WorldSystemRecipes' in l or 'SceneNav' in l or l.lstrip('│├└─ ').startswith(('nav/', 'terrain/', 'water/', 'particles/', 'voxel/')))]
    new_block = []
    for l in L[a + 1:b]:
        if l in map_3d: continue
        if 'TypeRegistry3D.cpp 3D' in l: l = l.replace(' (+ TypeRegistry3D.cpp 3D)', '')
        if 'NavigationCube 3D' in l: l = l.replace(', NavigationCube 3D', '')
        if 'MeshImport.* 3D (assimp/cgltf)' in l: l = l.replace(' + MeshImport.* 3D (assimp/cgltf)', '')
        if l.startswith('Projects/') and 'Frontier' in l: l = 'Projects/         Starforge (editor), SF_Telem, PendulumLab, AnalysisSample, the template projects'
        new_block.append(l)
    L[a + 1:b] = new_block
    parked_parts.append(('README Part II §30 — the 3D lines of the source file map (verbatim)', ['```'] + map_3d + ['```']))
    # (c) DG-6 Renderer3D nodes
    text = nl.join(L)
    m = re.search(r'    class Renderer3D \{.*?\n    \}\n', text, re.S)
    dg6 = []
    if m:
        dg6.append(m.group(0).rstrip('\n')); text = text.replace(m.group(0), '', 1)
    for edge in ('    SceneRenderer ..> Renderer3D : routes opaque/transparent\n', '    Renderer3D ..> RenderCommand\n'):
        if edge in text:
            dg6.append(edge.rstrip('\n')); text = text.replace(edge, '', 1)
    text = text.replace('what the 2D build drops' + nl + 'is `Renderer3D` and the resources only it owns.', 'the `Renderer3D` half that used to sit' + nl + 'beside it is parked (see below).')
    parked_parts.append(('README Part II §35 DG-6 — the Renderer3D nodes and edges (verbatim)', ['```mermaid'] + dg6 + ['```']))
    # (d) §40 COSMIC_2D_ONLY paragraph
    m = re.search(r'Two flags shape what gets built\. \*\*`COSMIC_2D_ONLY`\*\*.*?\n\n', text, re.S)
    if m:
        para = m.group(0)
        parked_parts.append(('README Part II §40 — the build-flag paragraph (verbatim)', para.rstrip('\n').split('\n')))
        text = text.replace(para, ('One flag used to shape what got built: `COSMIC_2D_ONLY` selected the engine configuration. Since AP-05 it is an' + nl +
            '**always-ON compatibility no-op** (`OFF` is rejected at configure) and the remaining options — `COSMIC_BUILD_ENGINE_ONLY`,' + nl +
            '`COSMIC_WITH_JOLT`, `COSMIC_BUILD_TESTS`, `COSMIC_BUILD_RENDER_TESTS`, `COSMIC_SKIP_PROJECTS` — are each a narrower' + nl +
            'switch on one subsystem or target. History: [`docs/parked-3d/README-part2-3d-systems.md`](docs/parked-3d/README-part2-3d-systems.md) (parked 3D).' + nl + nl), 1)
    wr(p, text)
    body = ['# Root README Part II — the 3D system sections (moved verbatim)', '', BANNER, '',
            '> Moved here 2026-09-20 by App Platform AP-D1 from the root `README.md` Part II (sections 30, 35 and 40).',
            '> Text is verbatim; line references were true at Phase 29 and describe the `engine-3d` tree. The live root',
            '> README keeps a one-paragraph pointer in §30.', '']
    for title, lines in parked_parts:
        body += ['## ' + title, ''] + lines + ['']
    wr('docs/parked-3d/README-part2-3d-systems.md', '\n'.join(body))

def write_index():
    idx = '''# Parked 3D documentation

> **PARKED 3D — not on the trunk.** Everything in this directory documents code that lives only on the `engine-3d` branch (`0e8894b`, tag `cosmic-pre-2d-2026-09-16`). The 2D trunk (`main`) no longer builds or ships it (D-PURGE, 2026-09-18). Kept for when 3D resumes.

**Why it is here.** On 2026-09-18 the App Platform campaign decided (D-PURGE, D-DOCS) that `main` is the 2D-only
trunk: `Renderer3D`, terrain, water, particles, voxels, navigation, skeletal animation and model import, their
vendored dependencies (assimp, recastnavigation, cgltf), their tests, goldens, editor panels and template scripts
were deleted from `main` by AP-05. The full pre-purge tree is preserved, byte for byte, at:

| What | Value |
| --- | --- |
| Branch | `engine-3d` |
| Commit | `0e8894b8540029ac57e68540aa9774cf5cf77ebe` |
| Tag | `cosmic-pre-2d-2026-09-16` |

Every file below carries the same banner at line 3. Nothing here is maintained; `file:line` references describe the
`engine-3d` tree, not `main`. Live documentation may link into this directory only with the visible label
"(parked 3D)" (checked by `evidence/AP-D1/check_parked.py`). The link checker treats this tier as warn-only.

## Index

| Parked chapter | Was | What it documents |
| --- | --- | --- |
| [`guide/rendering-3d.md`](guide/rendering-3d.md) | `docs/guide/rendering-3d.md` | `Renderer3D` submit/cull/sort/instance queue, meshes, models, LOD, the material-read-at-flush rule |
| [`guide/lighting-and-environment.md`](guide/lighting-and-environment.md) | `docs/guide/lighting-and-environment.md` | The full 3D frame: sun/point lights, PBR + IBL, sky modes, time of day, shadows, coverage capture, the post chain (the 2D part lives on as [`../guide/lighting-2d.md`](../guide/lighting-2d.md)) |
| [`guide/world-systems.md`](guide/world-systems.md) | `docs/guide/world-systems.md` | Terrain, water, particles as scene content |
| [`guide/voxels.md`](guide/voxels.md) | `docs/guide/voxels.md` | Voxel volumes, palettes, meshing, generation, editing |
| [`guide/navigation-and-ai.md`](guide/navigation-and-ai.md) | `docs/guide/navigation-and-ai.md` | Recast/Detour navmesh bake, `.cnav`, agents, the script `Nav()` proxy |
| [`guide/animation.md`](guide/animation.md) | `docs/guide/animation.md` | Skeletons, clips, GPU skinning, `AnimatorComponent`, sockets, the Animation Editor |
| [`reference/rendering-3d.md`](reference/rendering-3d.md) | `docs/reference/rendering-3d.md` | API skeleton: `Renderer3D`, `Model`, `InstanceSet` (`graphics/Mesh.h` still exists on `main` and is now routed to [`../reference/graphics-resources.md`](../reference/graphics-resources.md)) |
| [`reference/world-systems.md`](reference/world-systems.md) | `docs/reference/world-systems.md` | API skeleton: `Terrain`, `Water` + `GerstnerWave`, `ParticleEmitter`/`RibbonEmitter` + `Presets` |
| [`reference/rendering-pipeline-3d.md`](reference/rendering-pipeline-3d.md) | the 3D half of `docs/reference/rendering-pipeline.md` | API skeleton: `EnvironmentMap`, `ShadowMap`, `CoverageCapture` |
| [`systems/rendering-3d.md`](systems/rendering-3d.md) | `docs/systems/rendering-3d.md` | How the sorted queue works |
| [`systems/rendering-pipeline-3d.md`](systems/rendering-pipeline-3d.md) | the 3D half of `docs/systems/rendering-pipeline.md` | Shadow, reflection, environment and sky passes; lighting theory |
| [`systems/cameras-navigation-3d.md`](systems/cameras-navigation-3d.md) | the 3D half of `docs/systems/cameras-navigation.md` | CAD orbit/fly navigation, `NavigationCube`, `ScenePicker` |
| [`systems/terrain.md`](systems/terrain.md) | `docs/systems/terrain.md` | Heightmap composition, quadtree LOD, splat/triplanar materials |
| [`systems/water.md`](systems/water.md) | `docs/systems/water.md` | Gerstner water, reflections, buoyancy queries |
| [`systems/particles.md`](systems/particles.md) | `docs/systems/particles.md` | GPU-compute particles, ribbons, presets |
| [`systems/build-2d-3d-split.md`](systems/build-2d-3d-split.md) | `docs/systems/build-2d-3d-split.md` | The Phase 29 two-configuration build (`COSMIC_2D_ONLY` filter, fences, `engine-2d` branch) — superseded by the trunk policy |
| [`README-part2-3d-systems.md`](README-part2-3d-systems.md) | root `README.md` Part II §30/§35/§40 | The 2D-partition table, the 3D lines of the source map, DG-6’s `Renderer3D` nodes, the build-flag paragraph |

Archived 3D *plans* (Phases 18, 20, 24, 26, 28) are in [`../plans/archive/`](../plans/archive/README.md); the
3D rows of the feature matrix are under [`../plans/FEATURE-MATRIX.md`](../plans/FEATURE-MATRIX.md) "Parked (engine-3d)".

## How to resume 3D

1. `git fetch --tags` and start from the preserved tree: `git switch -c 3d-resume cosmic-pre-2d-2026-09-16`
   (or branch from `engine-3d`). Do **not** cherry-pick 3D source back onto `main`; the trunk policy is one
   engine, 2D-only.
2. Configure that tree as it documents itself: the pre-purge `README.md` §1.6 and
   [`systems/build-2d-3d-split.md`](systems/build-2d-3d-split.md) describe `COSMIC_2D_ONLY=OFF`, `build_3d.bat`
   and the vendored assimp/recastnavigation.
3. Bring the 2D trunk’s later work across by merging `main` into the 3D branch, not the other way round; the
   `COSMIC_2D_ONLY` fences were removed from `main` (AP-05 Part B, `evidence/AP-05/unfence.py`), so expect
   conflicts in the files that script rewrote (listed in `evidence/AP-05/report.md`).
4. Move the chapters here back to their `Was` paths and drop the banners; the coverage manifest rows that
   pointed at them are the `../parked-3d/…` rows in `docs/reference/README.md`.
'''
    wr('docs/parked-3d/README.md', idx.replace('\n', '\n'))

if __name__ == '__main__':
    main()
