# UX-G0: World-mode design doc and the five reserve-now contract changes

**Gate:** G2 · **Wave:** 2 (∥ UX-04, UX-D2; lands after UX-04, before UX-D2) · **Runs:** worktree
`build\_lanes\ux-g0`, branch `ux/g0` · **Base:** `main` after UX-01, UX-02 and UX-03 have landed ·
**Depends on:** wave 1 (UX-03's `CS_TEST_FIXTURE()` is used by the new probe fixture) ·
**Acceptance:** WM01–WM03 · **Model:** Opus 5.5 · **Effort:** max · **Status:** not started

Kaden's item 11 and decision D-WORLD: the 2D game/world mode is designed now and built later. The
assessment found that most of it already works (world sprites and a HUD canvas in one scene, ortho and
perspective cameras, `ZOrder`/`YSort` sorting, flipbooks, tilemaps, 2D lights, the post chain,
user-coded fixed-step physics). This lane writes that up as a design with a worked example that really
plays, and lands only the five small reserve-now changes so the later world packet is purely additive:
the manifest keeps keys it does not know, the 2D world contract is verified, scenes warn on a newer
version, physics initialises after services, and player and editor share one clear colour with post
effects previewed in editor 2D Play. It also registers the sim-scale risks as KIs.

## Copy-paste prompt

~~~text
Execute only UX-G0 from the Cosmic "UX & Shipping" packet (docs/plans/ux-shipping-2026-09-24/).
Nobody answers questions: decide, proceed, report honestly (blocked/failed is reported as such).
Read first, and nothing else until you edit a file named below:
 1. work-orders/README.md (global rules, lane rules L1-L5, build commands, next free KI number)
 2. 01-Contracts.md §6 (the 2D world contract), §10 (UX-G0 row), §11 (register format)
 3. 03-Acceptance-Catalog.md rows WM01-WM03
 4. 00-Start-Here.md, decision D-WORLD and "What is deliberately deferred"
 5. Projects/Starforge/src/ProjectManifest.h (all 75 lines)
 6. Cosmic/src/layers/PlayerLayer.cpp:55-162 and :324-425
 7. Projects/Starforge/src/StarforgeApp.cpp:598-713 (PlayScene) and :1174-1215 (viewport clear/env)
 8. Cosmic/src/scene/SceneSerializer.cpp:463-503 and Projects/Starforge/src/AP03AuthoringSelfTest.cpp
Names and rules in 01-Contracts.md §6 win over this prompt; any deviation goes in the report.

Lane: from C:\dev\Cosmic run  git worktree add build\_lanes\ux-g0 -b ux/g0 main  (main = after UX-03
landed), work only inside C:\dev\Cosmic\build\_lanes\ux-g0, set $env:COSMIC_SDK to that folder, build
into <worktree>\build with the README commands (-DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=ON; add
-DCOSMIC_BUILD_RENDER_TESTS=ON for WM02). UX-04 and UX-D2 run at the same time. Before any change, record
the base hashes WM02 compares against (below) and hash tests/render/goldens/*.png.
Revalidate: print git rev-parse HEAD, git status --short, cmake --version; re-check every anchor below
(true at 0c2edd8; wave 1 did not own these lines, confirm).

KIs first (known-issues.md in docs/plans/2d-stability-2026-09-16/contracts/, template :14-22, numbers from
the next free number in work-orders/README.md, renumber at rebase if taken):
(k1) ProjectManifest::Save rewrites the file from the known key set (ProjectManifest.h:52-73; the header
     comment :5-7 says so) and drops capture_cursor, which PlayerLayer.cpp:87 reads. Fixed here.
(k2) File ▸ Project Settings ▸ Save builds a fresh ProjectManifest (StarforgeApp.cpp:3265-3272): kind is
     written as "game" (ProjectManifest.h:59), startup_flow and pixel_art are dropped, so an app project
     loses its flow. Fixed here: load, change the dialog's fields, save.
(k3) float SpriteAnimationComponent::Elapsed (Components.h:232) accumulated per frame (Scene.cpp:537):
     record the drift onset and stall times at 60 and 144 Hz (float spacing; 2^18 s is about 3.0 days).
(k4) one CreateRef<SubTexture2D> per textured sprite per frame (Scene.cpp:729-731; tilemaps cache per
     tile id at :684-696).
(k5) the editor Camera2DController zoom cap of 10 000 units half-height (Camera2DController.h:118-119).
k1/k2 get failing-before (isolated stash) and passing-after; k3-k5 are registered with disposition open
unless you fix them (optional, own commit, own test).

The five reserve-now items, each its own commit, in this order:
(a) Manifest. ProjectManifest::Save(diskPath) edits in place: it reads the existing file, rewrites the
    value of a known key only when it changed, adds an absent known key only when its value differs from
    the Load default (:32-49), and keeps every other line byte for byte (comments, unknown keys, unknown
    tables such as [world], unknown keys inside [window], blank lines, the file's CRLF or LF). A missing
    file is written in today's layout (:56-71). §6.1 says "canonical order, unknown keys after": that
    cannot give WM02's byte-identical round trip of commented manifests; record the in-place rule as the
    §6.1 deviation. Plus the k2 fix in StarforgeApp.cpp:3262-3275.
(b) Contract. Verify every clause of 01-Contracts.md §6 against the code; correct wrong anchors and add
    an "Evidence:" line per clause (test ID or file:line). No decision changes.
(c) Scene version. SceneSerializer::LoadFromString (:489-503) warns once (CS_CORE_WARN) when
    cosmic_scene is a number > 1 and still loads; SaveToString keeps writing 1 (:479).
(d) Init order. PlayerLayer::OnAttach calls m_Physics.Init() (:114) before m_Services.Instantiate (:122-124,
    which runs every OnAttach, ServiceHost.cpp:118) and PhysicsWorld::Init resolves the registry default
    at init time (PhysicsWorld.cpp:35). Move Init after :124 and before the flow/scene load (:126-157), so
    a service can PhysicsBackendRegistry::SetDefault (PhysicsBackend.h:149-169). The editor already does
    services (StarforgeApp.cpp:683) before Init (:696): confirm, change nothing there.
(e) Clear colour and post. Player {0.06,0.07,0.10,1} (PlayerLayer.cpp:381, :390) vs editor
    {0.086,0.098,0.129,1} (StarforgeApp.cpp:1181, :1195): one header-inline constant with the player's
    value, used at all four sites (put it in Cosmic/src/renderer/SceneRenderer.h or a new header-only file;
    record the file). StarforgeApp.cpp:1197 skips ApplyEnvironment whenever m_Mode2D; in 2D Play run it,
    mirroring PlayerLayer.cpp:412-421; 2D edit mode unchanged.

Design doc docs/plans/ux-shipping-2026-09-24/04-World-Mode-Design.md, written from source with anchors:
A. What works today: world + HUD in one scene (Projects/Starforge/assets/templates/samples/ForgePong/
   scenes/Game.cscene, Projects/PendulumLab/scenes/Lab.cscene); CameraComponent ortho/perspective (Components.h:397-420, OrthoSize = half-height
   :406); sort (Scene.cpp:556-598); flipbooks (Components.h:221-232, Scene.cpp:528-554); tilemaps;
   Light2DComponent (Components.h:378); RenderToTexture (SceneRenderer.h:218); user physics via
   AppService::OnFixedUpdate (AppService.h:96), SystemScript OnFixedUpdateAll (ScriptableEntity.h:340),
   the fixed order (PlayerLayer.cpp:324-337) and "no RigidBody => the engine never writes your
   Transform" (Projects/PendulumLab/src/screens/LabScreen.h:44-62); a whole-world IPhysicsBackend (PhysicsBackend.h:68); kind is never read
   by the engine (no "kind" in Cosmic/src); FlowMachine is scene-type agnostic.
B. The worked space scene = the WM03 fixture, embedded with its screenshot.
C. The contract: a summary pointing at §6, never a second copy.
D. The additive backlog as a future packet outline: parallax (ParallaxLayer), Camera2DFollow + zoom,
   Particle2D, SortingLayer, polygon fills, sequencer, GIF/MP4, GetFixedAlpha, "New World Scene"
   scaffold, the to-9km sim as the sample; for each: where it hooks in and what it must not change.
E. to-9km notes: k3-k5, float positions at 9 km, and what the sim needs from the physics seam.
docs/plans/FEATURE-MATRIX.md: one row per backlog item, status ⏸, home = the future world packet.

Tests. tests/test_project_manifest.cpp ("UX-G0 WM01 ..."): capture_cursor = true and [world]
pixels_per_unit = 32 survive Load -> edit WindowTitle -> Save; untouched Load -> Save is byte-identical;
k2's dialog path keeps kind, startup_flow, pixel_art. tests/test_world_contract.cpp: cosmic_scene 2
warns exactly once (a Log sink) and loads; the clear-colour constant equals the player's value; a host
case, skipped by default and run with Run-WO10Case.ps1 -IsolateCwd, loads a new fixture DLL
tests/UXG0PhysicsProbeFixture.cpp (CS_MODULE + CS_SERVICE whose OnAttach registers and SetDefaults a
probe backend recording Init; CS_TEST_FIXTURE(); restores the default on detach) through the real
PlayerLayer: the sequence is OnAttach then Init. A source audit (Select-String) shows no clear-colour
literal left in PlayerLayer.cpp or StarforgeApp.cpp. WM02: for templates app/game/blank, samples
FlowDemo/ForgePong and PendulumLab: Load -> Save of project.cproj reproduces the working-tree bytes, and
the SHA-256 of SceneSerializer Load -> SaveToString per .cscene equals the base hash list; the 15 goldens
byte-identical; ap02-gpu and wo08-gpu unchanged (no GPU = ENVIRONMENT_BLOCKED, named).
WM03: fixture project tests/fixtures/uxg0/SpaceScene/ (game-template CMakeLists, src/Module.cpp with
CS_SYSTEM(OrbitSystem), scenes/Space.cscene: Primary perspective camera at +Z, sprites at three Z
depths, a HUD canvas, an Environment with Bloom). Self-test host Projects/Starforge/src/
UXG0WorldSelfTest.cpp (COSMIC_UXG0_SELFTEST=<result.json>, COSMIC_UXG0_ROOT, COSMIC_UXG0_SHOTS), hooked
beside the AP03 hooks (StarforgeApp.cpp:139-140, :188-189, :1021-1022, :1279, :1662-1663; StarforgeApp.h:
379-395): copy, open, BuildScripts, Play 600 frames, orbit radius within 1e-3 of analytic after every
fixed step, a readback pixel near the brightest sprite differs with Bloom on vs off, PNG saved.
Wrapper tests/acceptance/fixtures/Run-UXG0World.ps1; manifests tests/acceptance/manifests/
uxg0-world.manifest.json (WM01, WM02 units, WM03) and uxg0-gpu.manifest.json (WM02 render half).

Evidence: docs/plans/ux-shipping-2026-09-24/evidence/UX-G0/report.md in the WO-10 layout (scope and
provenance, toolchain, what was built, status per ID, defects registered before fixing, measured
numbers incl. the k3 times and golden hashes before/after, caveats, files, local commits); commit
*-excerpts.txt, JSON, hash lists, the WM03 PNG; *.log stays ignored. §11 rows: manifest key preservation,
world contract items. Contract deviations: the §6.1 rule, the constant's file, the k2 and hook lines in
StarforgeApp.cpp, the probe fixture.

Land (L2): git rebase main (UX-04 may be in; keep both sides in tests/CMakeLists.txt and the KI
register); rebuild Debug + Release, 0 warnings; CosmicTests both configs; CosmicRenderTests + goldens
hashed; Run-Acceptance.ps1 -Manifest <absolute uxg0-world / uxg0-gpu path> -Config Debug|Release
-TempRoot <worktree>\build\_temp\uxg0; retained ap03-editor, ap04-sample, apq1-y02, wo10-sample;
check_gl_conformance, check_docs_coverage, check_docs_links (+ check_api_matrix if UX-D2 has landed)
exit 0. Stage explicit paths, commit on ux/g0 as kdadabhoy <kdadabhoy28@gmail.com>, no Co-Authored-By,
no AI trailer: KIs, (a)-(e) one commit each, design doc + FEATURE-MATRIX, tests + fixtures. Never push.
Report (<= 40 lines): WM01-WM03 per config, KI numbers and dispositions, golden hashes equal, the §6
anchors you corrected, deviations, SHAs, git status --short.
~~~

## Files to read first (and nothing else)

`work-orders/README.md`; `01-Contracts.md` §6, §10, §11; the WM catalog rows; `00-Start-Here.md` D-WORLD;
`ProjectManifest.h`; `PlayerLayer.cpp:55-162, 324-425`; `StarforgeApp.cpp:598-713, 1174-1215`;
`SceneSerializer.cpp:463-503`; `AP03AuthoringSelfTest.cpp`.

## Owns / May touch

See `01-Contracts.md` §10 (UX-G0 row). **Owns:** `04-World-Mode-Design.md`; `01-Contracts.md` §6
(evidence and anchors only); `docs/plans/FEATURE-MATRIX.md` (rows); `Projects/Starforge/src/ProjectManifest.h`;
`Cosmic/src/scene/SceneSerializer.cpp` (version warning); `Cosmic/src/layers/PlayerLayer.cpp` (init order,
clear colour); `StarforgeApp.cpp:1181-1197`; the clear-colour constant's header; `tests/test_project_manifest.cpp`,
`tests/test_world_contract.cpp`, `tests/UXG0PhysicsProbeFixture.cpp`, `tests/fixtures/uxg0/**`;
`Projects/Starforge/src/UXG0WorldSelfTest.cpp`; `tests/acceptance/fixtures/Run-UXG0World.ps1`;
`tests/acceptance/manifests/uxg0-*.json`; `evidence/UX-G0/**`. **May touch:** `StarforgeApp.cpp`
(`:3262-3275` for k2 and the five self-test hook lines, recorded as deviations); `StarforgeApp.h` (hook
declarations); `tests/CMakeLists.txt` (test rows, the probe fixture target); `StarforgeAppServices.cpp`
(only if the editor's order turns out not to mirror; it does at `0c2edd8`); the KI register (append).

## Scope

- **In:** (a)–(e), the design doc, FEATURE-MATRIX rows, k1–k5 registered (k1, k2 fixed), WM01–WM03.
- **Out:** any world feature itself (parallax, follow camera, `Particle2D`, sorting layers, the to-9km
  sample); a `physics_backend` manifest key (only named in the design doc as a later option); changing
  a golden; changing the scene file version.

## Deliverables

The five commits; the k2 fix; `04-World-Mode-Design.md` with the example and its screenshot; §6 evidence
lines; FEATURE-MATRIX rows; the tests, the probe fixture, the SpaceScene fixture, the self-test host,
wrapper and two manifests; `evidence/UX-G0/report.md`; §11 rows.

## Done when (DoD)

WM01–WM03 pass on Debug and Release (WM02's render half on a GPU or honestly blocked); every existing
template, sample and PendulumLab manifest and scene round-trips byte-identical; the 15 goldens are
unchanged; k1–k5 are registered, k1 and k2 closed with failing-before / passing-after; both audits and
the link checker exit 0.

## Rollback

Revert the lane's commits in reverse order; (a)–(e) are independent commits. Reverting (a) restores the
known-key rewrite (and the `capture_cursor` loss); the design doc and FEATURE-MATRIX rows are docs only.
