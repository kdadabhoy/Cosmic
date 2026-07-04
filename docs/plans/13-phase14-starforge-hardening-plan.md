# Phase 14 Plan — Starforge Hardening (bugs, rendering truth, editor UX)

> **Created 2026-07-04** from a live triage session: every bug below was reproduced in the
> running editor and root-caused in code the same day (file/line references were verified then —
> re-verify by content before editing, they will drift). This phase makes what Phase 13 built
> **correct and pleasant** before any new feature phase (15+) stacks on top of it.
>
> **Scope decisions (user-approved 2026-07-04):**
> - Phase 14 executes **first** among the new phases — every later phase's demos look broken
>   until the viewport renders environments/lights truthfully.
> - The **C++ SystemScript tier** (logic bound to a *class* of entities) lands here (H9); the
>   Lua tier stays parked in doc 20.
> - The full homescreen/product-launcher redesign is **doc 15** (Phase 16); H10 only takes the
>   quick UX wins that don't depend on the platform decisions.

---

## 0. Execution notes — READ ONCE, THEY APPLY TO EVERY ITEM

1. **Build/test (non-interactive):** never run `build_all.bat`/`build.bat` (they `pause`). Use
   the VS-bundled cmake recipe in `00-MASTER-ROADMAP.md` §"Working agreement". Outputs land in
   `build/Runtime/<Config>/`; run `CosmicApp.exe --project Starforge`. Tests:
   `build/Runtime/<Config>/CosmicTests.exe`. Re-run cmake **configure** after adding new
   *engine* source files (engine GLOB lacks CONFIGURE_DEPENDS).
2. **Engine rules:** no `gl*`/`GL_*` outside `platform/OpenGL/`; new GPU state =
   `RendererAPI`/`RenderCommand` verbs; GPU-owning classes non-copyable; claim binding points
   in `renderer/BindingPoints.h` first; reserved sampler units are assigned unconditionally via
   `Renderer3D::ApplySceneBindings`. The engine gains only **generic** modules — never
   Starforge-specific names or UI (doc 11 §0 rule 2 still binds).
3. **Compatibility gate (non-negotiable):** Engine3DDemo, Frontier, SF_Telem, ViperSim build
   and run identically after every item. Smoke-run Engine3DDemo + Frontier whenever an item
   touches `Cosmic/src/scene/`, `renderer/`, `camera/`, or `core/Log`.
4. **Testability split:** anything unit-testable lands engine-side; `CosmicTests` links engine
   only, headless (no GL). `doctest::Approx.epsilon` is RELATIVE — use absolute tolerances for
   world coordinates.
5. **State-restore contracts:** any new pass (ID readback, thumbnails) restores depth ON/ON,
   cull None, blend Alpha, and re-binds the framebuffer it replaced.
6. **Process:** one work order per session on a phase branch; finish with the item's
   **Acceptance** (build + tests + the manual repro check), update the item's status banner
   here (✅ + date + one-line result). **Never run git write commands — the user commits.**

---

## 1. What this phase fixes (bug ↔ work order map)

| Reported symptom (user, 2026-07-04) | Root cause (verified) | Work order |
| --- | --- | --- |
| MMB press moves the view before any drag | `OrbitCameraController::ReanchorAround` re-targets a look-at rig → camera rotates toward the cursor pivot on press | **H1** |
| Skybox/environment toggles do nothing in the editor | Editor viewport + PlayerLayer render via `Scene::OnRender3D`, which never consumes `EnvironmentComponent`; `SceneRenderer::ApplyEnvironment` exists but is never called on this path | **H2** |
| Lights "aren't a thing" / adding lights changes nothing | Default (no-`.cmat`) meshes draw with legacy `Mesh3D.glsl` Lambert which **ignores the lights UBO**; also `SceneLightsDesc` defaults to a full sun so a new DirectionalLight ≈ no delta; lights have no viewport billboard so they're invisible objects | **H3** |
| (found in triage) `SkyMode::HDRI` is authorable but unimplemented | `EnvironmentComponent` reflects the enum + `HdriPath`; `EnvironmentMap` has no equirect loader | **H4** |
| Top bar spacing wrong; Play/Build toolbar missing | Top dock is a fixed 6%-height ratio; the "Starforge" window's tab header + menu row consume it and the toolbar row clips; two "File" menus on screen (engine chrome + Starforge) | **H5** |
| Viewport is literally named "Viewport" | Hard-coded in `WorkspaceLayer` (`ImGui::Begin("Viewport")`) | **H5** |
| No ✕ to close panels like Material Editor | Every panel calls `ImGui::Begin("Name")` without the `bool* p_open` arg | **H5** |
| File paths are typed by hand (import dialog etc.) | No engine file-dialog verb; two ad-hoc `GetOpenFileName` usages exist (LauncherLayer, telemetry panel) | **H6** |
| Terminal logs uncolored; logs land in odd places | spdlog pattern lacks `%^…%$` color markers; `Log::SetLogDirectory` is pointed at `assets/projects/<name>/logs` (read-only content area) on project attach | **H7** |
| ForgePlayground looks broken; camera spawns oddly | Editor camera ignores the scene's authored Camera and spawns **inside** the 26 m island (backfaces culled → "nothing renders"); lake/campfire/ball are authored *below* the terrain surface | **H8** |
| Need scripts that drive a *class* of entities | Per-entity `ScriptableEntity` exists; no query/system tier | **H9** |
| Misc.: `[]` placeholder glyphs, unlabeled asset rows, floating Home panel | Assorted small gaps | **H10** |

Suggested execution order: **H1 → H2 → H3 → H5 → H8** (the visible-quality spine), then
H4/H6/H7/H9/H10 in any order (parallel-safe once H2 lands).

---

## 2. Work orders

### H1 — Pose-based orbit: kill the MMB jump

**Files:** MODIFY `Cosmic/src/camera/OrbitCameraController.h/.cpp`; NEW test cases in
`tests/test_s5_navigation.cpp` (file exists — extend it).

**Problem (exact):** `RecalculateCamera()` is `m_Camera.LookAt(m_Target + PoseToOffset(yaw,
pitch, dist), m_Target)` — the camera *always looks at* `m_Target`. On the first frame of a CAD
orbit drag, `ReanchorAround(pivot)` (called from `OnUpdate` when `m_DragMode == Orbit` begins)
sets `m_Target = pivot` (the point under the cursor) and re-derives yaw/pitch/distance to keep
the camera **position**. Position is kept — but the *orientation* snaps to aim at the new
target, so the view visibly rotates the moment MMB goes down (before any mouse movement).
Two secondary jumps: the re-derived distance/pitch are clamped
(`m_MinDistance/m_MaxDistance/m_MinPitchDeg/m_MaxPitchDeg`), which can also translate the
camera when the pivot is far away (sky/far-plane hits).

**Spec — decouple "orbit pivot" from "view center" (the Blender/Fusion model):**
1. Add a persistent view-offset to the rig: `glm::vec3 m_ViewOffset{0}` — the vector from
   `m_Target` (the orbit pivot) to the **look-at point**. `RecalculateCamera()` becomes
   `LookAt(eye, m_Target + m_ViewOffset)` where `eye = m_Target + PoseToOffset(yaw, pitch,
   dist)`... **No — simpler and exactly equivalent to industry behavior:** store the camera
   pose as (position, yaw, pitch) and treat orbiting as **rotating the camera pose rigidly
   around the pivot**:
   - On orbit-drag start: compute `pivot` (existing `ComputeCursorPivot`); store
     `m_OrbitPivot = pivot`. Do **not** touch target/yaw/pitch/distance. No state that affects
     the rendered view may change on press.
   - Per orbit-drag frame with deltas (dYaw, dPitch): build `R = Ry(dYaw) * Raxis(right,
     dPitch)`; new position = `pivot + R * (position - pivot)`; new orientation = `R *
     orientation` (equivalently `yaw += dYaw; pitch += dPitch` since the rig is yaw/pitch-only).
   - On drag end: leave `m_Target`/`m_Distance` re-derived from the final pose so
     zoom-to-cursor, pan scaling, F-frame, snap views, and bookmarks keep working: project the
     camera forward ray onto the old target's depth (`m_Target = position + forward *
     oldDistanceAlongForward`), `m_Distance = |m_Target - position|`. This end-of-drag
     re-derivation changes nothing visually (target sits on the view ray).
2. Keep the pitch clamp by clamping the *accumulated* pitch during the drag (never re-derive
   pitch from an arbitrary pivot). Drop the distance clamp from the orbit path entirely — it
   belongs to zoom only.
3. `ComputeCursorPivot`'s fallback plane logic is unchanged; Starforge should now also install
   the precise probe: `SetPivotProbe` with a lambda that reads the viewport depth via
   `ScenePicker`/`FrameBuffer::ReadDepth` (Engine3DDemo has the S5.1 precedent — copy its
   wiring into `Projects/Starforge/src/StarforgeApp.cpp` `OnAttach`).
4. Classic nav style and every public API keep current behavior (`SnapView`, `Frame`,
   `BeginPoseAnimation`, scroll zoom-to-cursor are untouched).

**Gotchas:** the drag's first frame must still produce **zero** delta (the existing
latch-don't-delta logic — keep it). `m_OrbitVelocity` inertia should use the same rigid
rotation. After a drag that ends with the pivot behind the camera, the target re-derivation
must clamp the along-forward distance to ≥ `m_MinDistance` (degenerate-pose guard).

**Acceptance:** headless test: construct the controller, aim at a known pose, simulate an MMB
press with a pivot 30° off-axis → assert view matrix is **bit-identical** before any movement
frame; simulate a 90° orbit → assert the pivot's world point re-projects to the same NDC ±1e-3.
Manual: in Starforge, MMB-press on an off-center object — zero motion until the mouse moves;
orbit pivots about the point under the cursor; pan/zoom/F/snap views unchanged.

**Status:** ☐

---

### H2 — SceneRenderer becomes the editor + player render path

**Files:** MODIFY `Projects/Starforge/src/StarforgeApp.h/.cpp` (`RenderViewport`),
`Cosmic/src/layers/PlayerLayer.cpp` (`RenderScene`), `Cosmic/src/renderer/SceneRenderer.h/.cpp`
(one new convenience — see spec), `Cosmic/src/scene/Scene.h/.cpp` (desc-building helper);
possibly `Cosmic/src/renderer/PostProcessStack` (entity-ID interplay — see gotchas).

**Problem (exact):** `StarforgeApp::RenderViewport` and `PlayerLayer` render with
`Scene::OnRender3D(camera)` + `Scene::OnRenderWorldFX(...)` into the LDR viewport FBO. That
path: never calls `SceneRenderer::ApplyEnvironment` (so `EnvironmentComponent` — sky mode,
time-of-day, fog, IBL, exposure, bloom/SSAO/FXAA/flare — is authorable but **dead**), has no
shadow pass, no HDR/post chain, and water renders with the IBL-fallback reflection against an
IBL that was never baked (dark/wrong). Meanwhile `SceneRenderer` (the F2 orchestrator) already
consumes ECS content for shadow/coverage casters via `desc.EcsScene` and exposes
`ApplyEnvironment(env, desc)` (E4) — nothing editor-side drives it.

**Spec:**
1. **Engine — one generic verb, no editor concepts:** add
   `SceneRenderer::BuildSceneDesc(Scene& scene, const Camera& camera, SceneRenderDesc& out)`
   (or a free helper in `scene/`): fills `out` from the ECS — camera matrices; lights (reuse
   the exact gather loop currently in `Scene::OnRender3D`: first `DirectionalLightComponent` =
   sun, all `PointLightComponent`s); `TerrainSystem` = first built `TerrainComponent` asset;
   `WaterBodies` from `WaterComponent`s (`PrimaryReflectionWater` = nearest to camera);
   `Emitters` from `ParticleEmitterComponent`s; `EcsScene = &scene`; `DrawOpaque` = a callback
   that submits every `MeshRendererComponent`/`LODGroupComponent` through the existing
   world-transform logic (`WorldOf`), routing by `ScenePass` (the S12 queue applies in
   Reflection/Main; `ShadowDepth` goes to `c.DrawMesh` → `ShadowMap::DrawCaster`, which
   `SceneDrawContext` already handles). If an `EnvironmentComponent` exists, call
   `ApplyEnvironment` last. `Scene::OnRender3D` **stays** (compat gate + the cheap path) but its
   body should share the gather/submit helpers so there is one truth for "what does a scene
   draw".
2. **Starforge:** `RenderViewport` constructs the desc via the helper, keeps
   `Scene::SyncPrimitiveMeshes()`/`SyncWorldSystems()` running first (move the calls in front of
   desc-building — they currently run inside `OnRender3D`), then `m_SceneRenderer.Render(desc)`
   with the viewport FBO bound (the PRE/POST contract in `SceneRenderer.h` — the FBO is re-bound
   after). Editor overlays (`ViewportController::DrawSceneOverlay` grid/axes/selection) draw
   **after** `Render` inside a thin `Renderer3D::BeginScene/EndScene` block, depth-tested
   against the composited depth — see gotcha 3.
3. **PlayerLayer:** identical wiring (it owns its own `SceneRenderer` member; `Init` on attach,
   `Shutdown` on detach, `SetViewportSize` on resize). This closes the E13/E18 deviation "the
   SceneRenderer env/shadow/post upgrade is a shared follow-up for both surfaces" — shipped
   apps get sky/shadows/post identical to the editor.
4. **Play-mode parity:** Play renders the runtime scene through the same desc path (the
   editor camera or, after H8's adopt-toggle, the primary `CameraComponent`).
5. Default `SceneRendererSettings` must keep a scene with **no** EnvironmentComponent looking
   like today's editor output (grey-blue clear, no fog): set `Settings.ClearColor` from the
   current editor clear `{0.086, 0.098, 0.129, 1}` and leave every post toggle at its default
   unless the component drives it.

**Gotchas:**
1. **Entity-ID picking:** `ScenePicker` reads the entity-ID attachment of
   `Application::GetFrameBuffer()`. SceneRenderer's opaque pass renders into its own HDR MRT —
   verify which target carries the ID attachment in Engine3DDemo's combined HDR+picking setup
   (it has both S6.1 HDR and S5.5 picking; mirror its solution). If the HDR target owns the ID
   attachment, `ScenePicker` must read from `SceneRenderer`'s post-stack target — add a generic
   accessor (`SceneRenderer::GetEntityIdSource()` returning {fbo, attachment-index}) rather
   than hard-coding.
2. **GL conformance:** run `tests/check_gl_conformance.ps1` — no raw GL may appear in the new
   code.
3. **Overlay depth:** after `Render`, the LDR viewport FBO's depth buffer does NOT contain the
   scene depth (post writes color only). For occluded-correct grid/selection lines either
   (a) blit the HDR depth into the viewport FBO via the existing `BlitCopy` path, or (b) draw
   overlays into the HDR target pre-post (SceneRenderer's `DrawTransparent` hook is the
   sanctioned injection point — use it; it runs with HDR + scene depth still bound).
4. **Perf:** the full pipeline (shadow+post) on a 2-object sandbox must stay trivially ≥60 fps;
   expose `SceneRendererSettings` through the existing Statistics window so regressions are
   visible.
5. Water in the desc path gets real planar reflection (`PrimaryReflectionWater`) — delete the
   editor's IBL-fallback expectations; `Scene::OnRenderWorldFX` remains for any caller that
   wants the cheap path but Starforge/PlayerLayer stop calling it.

**Acceptance:** in Starforge: toggling Environment-panel fields visibly works — sky mode
(procedural sun scrub changes the sky), fog, bloom/SSAO/FXAA, exposure; a `DirectionalLight`
rotation moves real shadows; ForgePlayground's lake shows a reflection. `--project
ForgePlayground` (PlayerLayer) renders byte-identically to the editor viewport (screenshot
diff by eye). Engine3DDemo + Frontier render identically to before (compat gate — they drive
their own descs). Zero GL errors in the log; conformance script green.

**Status:** ☐

---

### H3 — Lighting unification: scene lights affect everything; lights are visible/pickable

**Files:** MODIFY `Cosmic/src/renderer/Renderer3D.h/.cpp`, `Cosmic/assets/shaders/Mesh3D.glsl`
(or its replacement), `Cosmic/src/scene/Scene.cpp` (light gather already exists);
`Projects/Starforge/src/ViewportController.cpp` (billboards). Read first:
`assets/shaders/MeshLit.glsl`, `PBR.glsl`, `renderer/BindingPoints.h`.

**Problem (exact):** three shading paths disagree about lights. (a) `DrawMesh(mesh, xform,
color)` — what every Starforge primitive and default `MeshRenderer` uses — draws with the
legacy Lambert `Mesh3D.glsl` driven by `s_Data.LightDirection`/`s_Data.Ambient`
(`SetLightDirection`/`SetAmbient`), and **ignores the lights UBO entirely** (the S4.5 comment
in `Renderer3D.h` says so explicitly). So point lights never affect a default mesh and a
`DirectionalLightComponent`'s direction only affects `.cmat`/PBR materials. (b)
`SceneLightsDesc` defaults to a white sun at intensity 1 — a scene with zero light entities is
already fully lit, so adding the first light changes almost nothing (user-visible as "lights do
nothing"). (c) Lights have no viewport representation at all.

**Spec:**
1. **One lit default path:** rewrite `Mesh3D.glsl` to read the std140 `LightsBlock` (same
   declaration as `MeshLit.glsl`: sun direction/color/intensity + ambient + up to
   `kMaxPointLights=16` points) while keeping its cheap Lambert BRDF and its existing
   vertex contract + `u_Color`/`u_EntityID`/shadow uniforms. Alternative (bigger, reject
   unless trivial): route the color path through `MeshLit.glsl` with a white material.
   `SetLightDirection`/`SetAmbient` become thin writers into the same UBO state (kept for API
   compat — Engine3DDemo/older samples call them; deprecate in the header comment).
2. **Honest defaults:** keep `SceneLightsDesc`'s built-in sun (removing it would black out
   every lightless scene — compat gate), but make the *editor* authoring truthful: when a scene
   contains at least one `DirectionalLightComponent`, that light fully defines the sun (already
   true in the gather); when it contains none, the Environment panel shows "Default sun
   (no DirectionalLight in scene)" so the user knows why changes don't stick (Starforge-side
   label, one line).
3. **Point-light falloff sanity:** verify `MeshLit/PBR` attenuation reaches visibly beyond
   `Radius/2` at `Intensity 1` on a default-grey mesh at 5 m — if a new point light at default
   values is imperceptible next to the sun, raise the component's default `Intensity` (E1
   reflection hint range too) so *dropping a light into a scene visibly does something* (the
   actual user complaint). Document the chosen default in the work-order banner.
4. **Light billboards + selection (Starforge):** in `ViewportController::DrawSceneOverlay`,
   draw an unlit billboard glyph (sun / bulb, Lucide font texture or a 16 px generated icon)
   at every `DirectionalLightComponent`(fixed offset above origin)/`PointLightComponent`
   entity's world position, tinted by the light color; a wire sphere of `Radius` for the
   selected point light; billboards write entity IDs (an ID-quad through the existing picking
   path — E9's "billboard pick = ID quad" note) so lights are click-selectable.
5. Sun **gizmo**: selected `DirectionalLightComponent` shows a direction arrow (existing line
   batch), rotatable via the rotate gizmo (its Direction field already reflects — the gizmo
   edits Rotation; map rotation→Direction in the component's inspector section or accept
   field-edit-only in v1 — note the choice).

**Gotchas:** `Mesh3D.glsl` is used by every shipped app's color-path draws — the rewrite must
be visually near-identical under the default lights (same sun dir/ambient defaults; verify
Engine3DDemo's aircraft + Frontier boot look unchanged, then run both smoke tests). UBO
binding: use `Bindings::LightsUbo` — never a new literal. GLSL 4.5 std140 arrays: mirror the
exact layout of `GpuLightsBlock` (`Renderer3D.cpp` ~line 40).

**Acceptance:** in Starforge with no `.cmat` assigned: a point light dropped next to the anvil
visibly lights it and pans as it moves; deleting the scene's Sun darkens the scene to ambient;
light entities are visible as billboards and click-selectable; Engine3DDemo/Frontier
smoke-runs unchanged. Headless: extend `tests/` with a lights-UBO packing test if one doesn't
already cover point count/truncation.

**Status:** ☐

---

### H4 — HDRI environments (close the authorable-but-dead SkyMode)

**Files:** MODIFY `Cosmic/src/renderer/EnvironmentMap.h/.cpp` (+ its bake shaders under
`assets/shaders/`), `Cosmic/src/renderer/SceneRenderer.cpp` (`ApplyEnvironment` — route the
HDRI case), `Cosmic/src/assets/` (RGBE/.hdr decode — stb_image already vendored? verify;
`Texture2D` gained decode-from-memory in S6.2). Starforge: Environment panel gains a file
slot (uses H6's dialog when it lands; path field until then).

**Spec:** load a `.hdr` (RGBE) or `.exr`-lite (defer EXR — note it) equirectangular image →
GPU equirect→cubemap pass (one fullscreen-per-face draw into the existing bake FBO —
`EnvironmentMap` already owns the bake path for the procedural sky; add a second source) →
existing irradiance/specular prefilter chain unchanged → skybox draws the new cube.
`EnvironmentComponent.SkyMode == HDRI` + `HdriPath` (both already reflected, E4) drive it via
`ApplyEnvironment`; re-bake only when the path changes (hash guard like the E15/E18 signature
pattern). Failure = log + fall back to procedural (never a black scene).

**Gotchas:** RGBA16F cube (RGB16F is not color-renderable off NVIDIA — Phase 9 hardening
note); bake-FBO completeness check exists — reuse. Large HDRIs: clamp the source to ≤4k and
mip it before prefilter (bake time). VFS: `HdriPath` is a `project://` asset path resolved in
the calling DLL.

**Acceptance:** assign a downloaded 2k studio HDRI in the Environment panel → skybox + IBL
lighting + water reflections all match it; switching back to Procedural restores the sun-driven
sky; a bad path logs and keeps the previous sky. Engine3DDemo/Frontier untouched (they never
set HDRI).

**Status:** ☐

---

### H5 — Editor chrome: top bar, dock names, panel close buttons

**Files:** MODIFY `Cosmic/src/layers/WorkspaceLayer.h/.cpp`;
`Projects/Starforge/src/StarforgeApp.cpp` (`ApplyDockLayout`, `DrawTopBar`), every
`Projects/Starforge/src/panels/*.cpp` (`Begin` signature). Read `docs/design/starforge-ui.md`
and update it as part of this item.

**Problems (verified live):** (a) the top area stacks a "▼ Starforge" dock-tab row, the
File/Edit menu row, and the "▼ Viewport" tab row — and the Play/Build/gizmo toolbar row is
**fully clipped** because the top dock is a fixed height ratio (`SetEdgeRatios(0.19, 0.22,
0.06, 0.26)` — 6% of an 820 px workspace ≈ two text rows). (b) Two "File" menus exist: the
engine window chrome (top-left "File  View") and Starforge's menu bar. (c) The central view is
hard-named "Viewport" (`WorkspaceLayer.cpp` `ImGui::Begin("Viewport")` + DockBuilder lines).
(d) No panel has a close ✕ (`ImGui::Begin("Name")` without `p_open` in all 8 panels).

**Spec:**
1. **Engine — WorkspaceLayer verbs (generic):**
   - `SetViewportTitle(const std::string&)` — window/dock-builder name for the central view
     (default "Viewport" so every shipped app is unchanged). Note: ImGui windows are keyed by
     name — use the `"Title###Viewport"` ID-suffix idiom so the dock node identity is stable
     while the visible title changes per scene.
   - Per-port **pixel-height/width minimums**: extend `SetEdgeRatios` (or add
     `SetEdgeMinPixels(top, bottom, left, right)`) so a dock edge is `max(ratio·size,
     minPx·dpiScale)`; Starforge sets the top edge min to fit menu+toolbar (~64 px at 100%).
   - `DockWindow(name, port, DockFlags)` gains an optional flags arg with `NoTabBar` →
     `ImGuiDockNodeFlags_NoTabBar` on that node (kills the "▼ Starforge"/"▼ Viewport" tab
     headers where an app wants chrome-less docks).
   - Chrome menu suppression: a `WorkspaceLayer` (or Application) option letting the active
     app hide/merge the engine "File / View" chrome menus while it supplies its own (the
     borderless title bar keeps min/max/close). Verify where those chrome menus are drawn
     (`ImGuiLayer`/window chrome from WS6) before choosing hide-vs-merge; either is
     acceptable, both must restore on project exit (Launcher unaffected).
2. **Starforge:** top bar window gets `NoTabBar`; toolbar row (Play ▶⏸⏭⏹ + Build + gizmo
   strip) must be fully visible at 100% and 125% DPI and at a 1280×720 window; viewport title =
   `SetViewportTitle(sceneName + (dirty ? " *" : ""))` (then drop the duplicate scene-name
   overlay text in the corner, keep the selection-count chip); all 8 panels take `bool* open`
   from `StarforgeApp`'s existing `m_Show*` fields → ✕ works and the **View menu checkmarks
   stay in sync** (they already read the same bools).
3. Reset Layout (View menu, exists) must restore any closed panel.

**Gotchas:** dock-node ids must never be persisted/captured (house rule — `DockWindow` port
mode only). ImGui `###` id-stability is the difference between "rename works" and "layout
resets every save" — test scene rename + Reset Layout. DPI: multiply pixel minimums by the
existing DPI scale the chrome already uses.

**Acceptance:** maximized and 1280×720, 100%/125% DPI: menu row + full toolbar visible, no
stacked tab headers, one File menu total, viewport tab shows the scene name with dirty star,
every panel closes with ✕ and reopens from View, Reset Layout restores everything.
Engine3DDemo/SF_Telem/ViperSim dock layouts unchanged (they don't opt in to the new flags).

**Status:** ☐

---

### H6 — Native file dialogs everywhere a path is typed

**Files:** NEW `Cosmic/src/utils/FileDialog.h/.cpp` (public API; Win32 `IFileDialog` COM
implementation pimpl'd in the .cpp — no `windows.h` in the header, `FileWatcher` precedent);
MODIFY `Projects/Starforge/src/StarforgeApp.cpp` (Import Model popup → Browse…; Save-As →
optional Browse), `Projects/Starforge/src/widgets/PropertyRows.h` (AssetPath rows get a "…"
button), `panels/EnvironmentPanel`/`MaterialEditorPanel`/`WorldSystemsPanel` (inherit via
PropertyRows), `Cosmic/src/layers/LauncherLayer.cpp` + `Cosmic/src/telemetry/TelemetryPanel.cpp`
(replace the two ad-hoc `GetOpenFileName` usages).

**Spec:** `FileDialog::Open(const OpenDesc&) -> std::optional<std::string>` and
`Save(const SaveDesc&)`, plus `PickFolder(...)`. Desc: title, filter list
(`{"3D models", "*.obj;*.fbx;*.stl"}`…), initial dir (accepts a VFS path — resolve through
`FileSystem::Resolve`), default extension. Modal on the main thread (the engine already runs
modal frame pumps — W4; a blocking call during a frame is acceptable here, matching the
existing usages). Returns absolute paths; **the caller decides** whether to copy into
`project://` (Import Model does; AssetPath "…" buttons must translate a picked file that
already lives under the project root into its `project://` relative form, and offer
copy-into-project when it doesn't — small confirm popup).
`PropertyRows`: the AssetPath widget's asset-type tag (E1 `AssetPath("mesh"/"material"/…)`)
maps to a filter preset table in one place.

**Gotchas:** COM init: `CoInitializeEx` may already be done by the OS dialogs used elsewhere —
init defensively per call (`COINIT_APARTMENTTHREADED`, tolerate `RPC_E_CHANGED_MODE`).
Fullscreen/borderless: pass the native window handle (`Window` exposes it or add an accessor)
so the dialog parents correctly and can't vanish behind the app. Never call from a render
callback; UI-event context only.

**Acceptance:** Import Model, scene Save-As, New Project location (homescreen), every
AssetPath row (mesh/material/texture/heightmap/HDRI), Launcher's existing dialog, and the
telemetry panel's existing dialog all open native dialogs with correct filters; picking a file
outside the project offers copy-in; cancel is a clean no-op. The two legacy `GetOpenFileName`
blocks are gone (grep proves it).

**Status:** ☐

---

### H7 — Logging: colors on, files in the right place, engine log in the Console panel

**Files:** MODIFY `Cosmic/src/core/Log.h/.cpp`; the call sites that invoke
`Log::SetLogDirectory` (grep — the project-attach path passes
`assets/projects/<name>/logs`, observed live 2026-07-04); Starforge
`panels/ConsolePanel.*` + `src/EditorContext.h` (sink hookup).

**Spec:**
1. **Terminal color:** the sinks are already `stdout_color_sink_mt`, but the pattern
   `"[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v"` contains no color range → nothing is colored.
   Set the console sink's pattern to `"[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v"` (level token
   colored per severity — spdlog wincolor handles the Windows console). Keep the FILE sink's
   pattern marker-free (no escape junk in files). Two patterns, one logger — set per-sink, not
   per-logger.
2. **Location policy:** boot logs keep going to `<exe-dir>/logs/` (pre-VFS). Once
   `FileSystem`'s user root is decided, logs move to **`user://logs/`** — one call
   `Log::SetLogDirectory(FileSystem::Resolve("user://logs"))` at the point the VFS becomes
   ready (Application init), NOT per-project. Delete/repoint whatever currently redirects into
   `assets/projects/<name>/logs` (content dirs are read-only by policy; packaged apps under
   Program Files literally cannot write there). Log one line at redirect time stating both the
   old and new paths ("Log files → C:\...\user\logs"). The per-project redirect feature stays
   possible via the same API for apps that want it — just no longer the default.
3. **Console panel sink:** engine-side generic `Log::AddSink(spdlog::sink_ptr)` (or a
   `CallbackSink` helper: severity + formatted line → `std::function`, thread-safe — messages
   may originate off-main; queue + drain like `FileWatcher`). Starforge registers one on
   attach, removes on detach, feeding `EditorContext::Log` with severity mapping (info/warn/
   error → the panel's existing severities + color). This closes E6's noted follow-up (the
   Console currently only shows the editor's own lines). Console panel gains severity filter
   toggles + a "engine log" visibility toggle (tiny).
4. Both loggers currently `flush_on(trace)` — leave as-is (crash-safety was the reason), but
   note in the header that high-rate logging is already throttled by this choice.

**Gotchas:** `Log::Init` re-creation on redirect swaps logger pointers under a mutex — added
sinks must survive the swap (re-attach inside `Init`, or better: keep one `dist_sink` that
owns the children and never swap it). Hot-reload safety comment in `Log.h` explains the
existing shared-mutex discipline — preserve it.

**Acceptance:** `CosmicApp --project Starforge` from a terminal shows colored levels; log
files appear under the user root (and NOT under `assets/projects/…` — grep the tree after a
session); engine `CS_CORE_WARN` lines appear live in the Starforge Console with the right
color; 20 hot-reloads later the sink still works (reload path exercises the logger swap).

**Status:** ☐

---

### H8 — ForgePlayground v2 + camera adoption (first impressions)

**Files:** MODIFY `Projects/Starforge/src/StarforgeApp.cpp` (`BuildForgePlayground`,
`OpenScene` camera logic, `GenerateSampleTake` stays); `Projects/Starforge/src/EditorPrefs.h`
(one pref). Depends on H2 (env/sky live) — do after.

**Problems (verified live):** the sample authors content against y=0 while its own terrain
recipe raises ~20 m of island at the origin: the lake (`SurfaceHeight 2`), campfire (y=3),
bouncing ball (y=8) are all **inside the hill**; the editor orbit camera spawns at the default
rig pose — *underground* — so the first view is a void (culled backfaces) with floating
embers; the authored Camera entity (0, 7, 20) is ignored; the BouncingBall script silently
does nothing until Build Scripts runs.

**Spec:**
1. **Author against the built terrain:** after creating the Terrain entity, build it
   immediately (`BuildTerrainSpec` + `Terrain::Create` — the same call `SyncWorldSystems`
   makes; it's GL-free) and use `SampleHeight(x, z)` to place: campfire ON the surface at a
   flat-ish spot, anvil/ingot on ground nearby, bouncing ball 6 m above its landing spot, lake
   `SurfaceHeight` = a real waterline (either lower `EdgeFalloff`/add a basin via recipe
   params so a shoreline exists, or raise the lake to sit in a terrain depression — verify
   visually, that's the acceptance). Add a `HoverController`-driven primitive as a second
   script demo (the scaffold already ships it).
2. **Camera adoption rule (general, not playground-specific):** on scene open, if the scene
   has a `Primary` `CameraComponent`, the editor camera adopts its pose (position + look
   direction mapped to the orbit rig: target = position + forward·10, distance 10); else
   frame-all (existing F-frame logic over the scene AABB). Pref
   `adopt_scene_camera` (default on). This kills the "spawn inside the terrain" class of bug
   for every project, not just the sample.
3. **Environment defaults that show off H2:** the sample's `EnvironmentComponent` sets
   procedural sky + warm ToD (17.5 stays), fog on, bloom on — values that visibly change when
   toggled (the panel's purpose).
4. **Script UX:** on opening a project whose module DLL is absent but whose scenes reference
   `NativeScriptComponent` classes, show a one-line Console warning + a non-modal toast/hint
   "Scripts not built — Ctrl+B" (the Console already gets the per-entity unresolved-class
   warnings from E11; add the single actionable summary line).
5. Regenerate the shipped sample: bump a `playground_version` in prefs; if the existing
   `ForgePlayground` folder was auto-generated by an older builder version (marker file in the
   project folder, add one now), offer "Rebuild sample?" on open.

**Acceptance:** fresh user path: Welcome → Open sample → the first frame shows an island,
water with reflections, campfire smoke ON the beach, sky — no navigation needed; press Play →
ball bounces on the ground (post-Phase-15 it can use real physics; today the script's floor
constant is set to its spawn-spot terrain height); Ctrl+B hint visible until built. Camera
adoption verified on a second hand-made scene with a Primary camera.

**Status:** ☐

---

### H9 — SystemScript tier: logic over a *class* of entities

**Files:** MODIFY `Cosmic/src/scripting/ScriptableEntity.h` (sibling base class),
`scripting/ModuleRegistry.h/.cpp` + `scripting/ModuleMacros.h` (`CS_SYSTEM`),
`scripting/ScriptHost.h/.cpp` (instantiate/tick), `scene/Components.h`
(`SystemScriptComponent` — scene-level holder), Starforge Inspector (already generic via
reflection; verify the class picker lists systems separately); NEW `tests/test_scripthost.cpp`
cases.

**Spec (user need: "an airplane class controlled by one physics script"):**
```cpp
class COSMIC_API SystemScript
{
public:
    virtual ~SystemScript() = default;
protected:
    virtual void OnCreate() {}
    virtual void OnStart() {}
    // Called ONCE per tick with the matching entity set (not per entity).
    virtual void OnUpdateAll(std::span<Entity> entities, float ts) {}
    virtual void OnFixedUpdateAll(std::span<Entity> entities, float fixedDt) {}
    virtual void OnDestroy() {}
    Scene& GetScene() const;
    // Reflected fields work exactly like ScriptableEntity's (CS_FIELD).
};
```
- **Membership = a query**, declared at registration:
  `CS_SYSTEM(AirplanePhysics).Requires<TransformComponent>().WithTag("airplane")` — Requires =
  component types (entt view), WithTag = optional `TagComponent` match (v1: exact tag or
  prefix; component-based grouping is the primary mechanism — recommend a user-defined marker
  component via `CS_COMPONENT(AirplaneMarker)` in the docs).
- **One instance per scene** per registered system, held by a scene-level
  `SystemScriptComponent{ std::string ClassName; Fields overrides }` on any entity (mirror of
  `NativeScriptComponent`; the editor's Add Component popup lists it under "Systems").
  `ScriptHost::Instantiate` resolves systems after per-entity scripts; tick order: systems
  first, then per-entity scripts (document it; deterministic).
- Ordering between multiple systems: registration order within the module; expose
  `.Order(int)` hint. Serialization/inspection/undo/telemetry come free via the reflection
  path (fields are a descriptor like any script).
- The span passed to `OnUpdateAll` is rebuilt per tick from the view (entities may spawn/die);
  guarantee stable iteration order (entt view order is fine — document that it's not
  user-sortable in v1).

**Gotchas:** hot reload (E12) must strip/restore `SystemScriptComponent` exactly like
`NativeScriptComponent` (the module-owned-storage-cleared-before-FreeLibrary rule); the system
instance itself is runtime-only. Play/Stop: instances are created at Play with the runtime
scene and destroyed at Stop — never against the edit scene (same as per-entity scripts).

**Acceptance:** headless: register a test system requiring `TransformComponent` + marker,
spawn 10 entities, tick — all move; spawn/destroy mid-run — membership updates; fields
round-trip through the serializer. Sample: template project gains `FlockSystem` (boids-lite
over a marker component) demonstrating the airplane pattern; runs in editor Play and via
`--project` standalone.

**Status:** ☐

---

### H10 — Editor consistency pass (small, buffered last)

**Files:** Starforge panels + `widgets/PropertyRows.h`; no engine changes expected.

**Spec (each its own checkbox, all small):**
1. AssetPath rows currently render with bare/blank labels in some panels (observed: Terrain's
   four splat "texture" slots) — every reflected field row shows its display name; fix at the
   PropertyRows level (label column min-width), not per-panel.
2. Content Browser thumbnails render literal `[]` glyph boxes when the Lucide icon for a type
   is missing (observed on scene/unknown files) — add a per-type fallback glyph table + a
   plain-text extension badge.
3. Homescreen quick wins (full redesign is doc 15): center the Home window, size it to the
   workspace, list editor-made projects (folders WITH `project.cproj`) separately from
   plugin-app asset folders (SF_Telem/ViperSim showed up as openable "projects" — filter on
   the manifest); show each project's startup scene + last-modified date.
4. Inspector: "Add Component" popup — hide runtime-only components (`OpaqueComponents`,
   `Relationship`, `Prefab`, `ID`) behind a "show internal" toggle (they're registered, so
   they currently appear).  Verify which of those are actually listed before changing.
5. Import Model popup: with H6 landed, replace the path `InputText` with Browse… (keep the
   unit-scale preview line).
6. Console: timestamps on lines + copy-to-clipboard on right-click.

**Acceptance:** visual checklist per item, screenshot in the status banner; compat gate
(panels only — no engine surface).

**Status:** ☐

---

## 3. Parked in this phase (goes elsewhere)

| Item | Where |
| --- | --- |
| Homescreen/product launcher redesign, external project folders | doc 15 (Phase 16) |
| Material-edit undo, offscreen preview rig + thumbnails (E17 deviations) | doc 19 §A4 |
| Wireframe fill-mode verb + ID-buffer visualize (E9 deviation) | doc 18 §R8 |
| Play-mode "eject camera" toggle (E13 deviation) | doc 16 §U6 (with the game-view work) |
| In-place texture reload into held Refs (E10 deviation) | doc 19 §A5 |

## 4. Order & dependencies

```
H1 (camera)  ──►  independent, do first (worst daily irritation)
H2 (SceneRenderer path) ──► H3 (lights) ──► H4 (HDRI)   [H3/H4 read H2's env plumbing]
H5 (chrome) ──► independent               H6 (dialogs) ──► independent
H7 (logging) ──► independent              H8 (playground) needs H2 (+H3 ideally)
H9 (systems) ──► independent              H10 last (sweeps behind everything)
```

## Kickoff prompt (paste to start each implementation session)

> You are implementing ONE work order from `docs/plans/13-phase14-starforge-hardening-plan.md`
> in `C:\dev\Cosmic`. Read that doc's §0 execution notes first — they bind. Then read your work
> order (H__) fully, including Files/Spec/Gotchas/Acceptance. Re-verify every quoted code fact
> by content before editing (line references were true 2026-07-04 and drift). Rules that bite:
> engine gains only generic modules (never Starforge-specific names); CosmicTests links engine
> only; the compat gate (Engine3DDemo/Frontier/SF_Telem/ViperSim unchanged) is non-negotiable;
> build with the non-interactive cmake recipe from `00-MASTER-ROADMAP.md`; never run git write
> commands. Finish with the Acceptance procedure + update the work order's status banner here
> (✅ + date + one-line result).
