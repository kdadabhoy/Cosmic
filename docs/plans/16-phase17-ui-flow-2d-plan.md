# Phase 17 Plan — In-Game UI, Screen Flow, and 2D Game Authoring

> **STATUS 2026-07-11 (UNcommitted) — PHASE CODE-COMPLETE (U1–U8 all ✅).** The
> editor/on-GPU remainder landed on top of the 07-08 engine foundation: **U1**
> editor click-consumption (+ `UiSystem::HitTest` edit-mode UI select), **U3**
> full 2D authoring mode (NEW engine `camera/Camera2DController`, pixel grid,
> sprite rect-pick, `Scene::OnRenderSprites` in editor+player, SpriteRenderer
> `TexturePath`/`YSort`, `pixel_art` sampling preset + New-Project template
> entry), **U4** Tilemap (engine component + int-array Cells serialization +
> culled draw + Tile Palette painter with stroke-coalesced undo), **U6** Flow
> Graph panel (VENDORED `imgui-node-editor` master `021aa0ea` w/ one guarded
> local patch — see its VENDOR-NOTES.md; generic `widgets/NodeCanvas` kept
> flow-free for Phase 25), **U7** game view (primary-camera Play, eject,
> letterboxed aspect presets via a new `UiSystem::Render` band overload,
> `Window::SetCursorCaptured` + `capture_cursor`, PLUS flow-driven editor Play
> closing U5's remainder), **U8** staged samples (zero-code **FlowDemo** +
> **ForgePong**, homescreen-built; template scripts compile-smoked headless).
> Build green Debug+Release **zero warnings**, `CosmicTests` **272/272**
> (241→272), GL-conformance clean, compat gate held (no shipped app attaches
> the new components; the sprite/UI hooks early-out on scenes without them).
> **REMAINING (user ledger):** the recorded acceptance —
> [`../design/ui-flow-2d-acceptance.md`](../design/ui-flow-2d-acceptance.md).
>
> **STATUS 2026-07-08 — engine foundation code-complete.** The
> headless-testable engine core of Phase 17 landed and is verified: build green
> across all 5 projects **zero warnings**, `CosmicTests` **241/241** (219→241,
> +22). What shipped this session:
> - **U1 (engine + tests):** `scene/ui/UiComponents.h` (Canvas / RectTransform /
>   UiImage / UiText / UiButton, reflected under "UI") + `scene/ui/UiSystem`
>   (pure `ResolveRect` / `StepButtonState` / `CanvasScale`, hit-test, `Update`
>   → EventBus, `Render` via Renderer2D incl. 9-slice/text/tint). Wired into
>   PlayerLayer (`DrawOverlay2D` + pointer `Update`) AND the Starforge viewport
>   (overlay render) + Entity▸UI create menu. `tests/test_ui_rects.cpp`.
>   *Deferred:* editor viewport click-consumption in the ScenePicker path.
> - **U2 (✅):** `scene/EventBus.h/.cpp` (named + ConnectAny, handle-unsubscribe,
>   re-entrancy-safe), a Scene member, `ScriptableEntity::Signals()`/`OnSignal`,
>   ScriptHost ConnectAny routing. Tests in test_ui_rects + test_scripthost.
> - **U3/U4 (engine slice):** `SpriteRendererComponent` gained `SourceRect` /
>   `PixelsPerUnit` / `ZOrder` (ABI-appended); NEW `SpriteAnimationComponent`
>   (flipbook, pure `SelectFrame`/`FrameUV`) + `Scene::UpdateSpriteAnimations`;
>   Entity▸2D create menu (Sprite / ortho Camera). Tests in test_scene_components.
>   *Deferred:* 2D editor mode (ortho toggle/pixel grid/painters), Tilemap +
>   tile painter (needs custom Cells serialization).
> - **U5 (engine ✅):** `scene/FlowMachine.h/.cpp` — `.cflow` parse/save/validate
>   (nlohmann), state stack w/ overlay push/pop, signal/key/timer transitions,
>   reflected-field guards, emit/setField actions, injected scene-loader; wired
>   into PlayerLayer (`startup_flow` manifest key, compat-gated).
>   `tests/test_flowmachine.cpp`.
> - **Build note:** TypeRegistry.cpp now compiles with `/bigobj` (MSVC section
>   limit hit by the reflection-thunk additions).
>
> ~~**REMAINING (editor / on-GPU — not headless-verifiable):** U1 editor click
> consumption; U3 full 2D authoring mode; U4 Tilemap + painter; **U6** Flow Graph
> node-editor panel (vendor imgui-node-editor); **U7** game-view correctness; **U8**
> zero-code app + ForgePong samples + recorded acceptance.~~ **All landed 2026-07-11**
> (see the banner above and each item's Status).

> **Created 2026-07-04.** Delivers three user asks that share one foundation:
> 1. **Design app screens in Starforge** — homescreens, menus, HUDs, built from entities.
> 2. **Visual screen-flow authoring** — "if this button is pressed, navigate to this scene"
>    as a node graph with conditional logic (user-approved: node-graph asset, industry-standard
>    state-machine shape, not ad-hoc wiring).
> 3. **2D / pixel-art games** as first-class Starforge projects (user-approved alongside voxel,
>    which is doc 17).
>
> The foundation they share: **UI elements are entities** (reflected components → Inspector,
> serializer, undo, prefabs, scripts all work on them for free — the E1/E2 dividend), and
> **navigation is a data asset** (`.cflow`) executed by an engine `FlowMachine` over the
> existing `SceneManager` (E5).
>
> **Anti-goals (v1):** no general visual *scripting* (the graph is screens/transitions only —
> gameplay logic stays C++ scripts/systems per the Phase-13/14 doctrine); no ImGui in shipped
> game UI (ImGui is editor chrome; game UI renders through Renderer2D so it ships styled and
> DPI-scaled); no complex layout engine (anchors + offsets, not flexbox).
>
> **Depends on:** Phase 14 (H2 SceneRenderer path — UI composites after post; H5/H6 editor
> chrome+dialogs). Independent of Phase 15 (physics) and 16 (platform), except U8's packaged
> acceptance which uses doc 15 S5 if it has landed (else E19 packaging).

---

## 0. Execution notes

1. Roadmap build recipe; doc 13 §0 engine rules; compat gate (shipped apps attach none of the
   new components; `FlowMachine` untouched by them).
2. **Read before writing:** `renderer/Renderer2D.h` (draw + text surface — verify whether a
   `DrawString`/glyph-atlas path exists; if not, U1 adds one, engine-generic),
   `scene/Components.h` (`SpriteRendererComponent` exists and is E1-registered — the 2D path
   is engine-old, editor-new), `scene/SceneManager.h` (E5 — the flow runtime drives it),
   `docs/design/responsive-rendering-and-pause.md` (pause semantics the flow's pause-overlay
   pattern rides).
3. UI + flow logic must be headless-testable: hit-testing, anchoring math, and the state
   machine are pure; only the draw is GL.
4. One work order per session; status banners; no git writes.

---

## 1. Architecture

```
engine ui-runtime (NEW Cosmic/src/scene/ui/ — generic, no editor code):
  RectTransformComponent { AnchorMin, AnchorMax (0..1 of parent rect), OffsetMin, OffsetMax,
                           Pivot, ZOrder }          // resolved rect = pure function (tested)
  CanvasComponent        { ScaleMode: ConstantPixel | ScaleWithHeight(refH) ; SortOrder }
  UiImageComponent       { TexturePath, Tint, NineSlice{l,t,r,b}, PreserveAspect }
  UiTextComponent        { Text, FontPath, SizePx, Color, HAlign, VAlign, Wrap }
  UiButtonComponent      { Signal (string), NormalTint/HoverTint/PressedTint/DisabledTint,
                           Interactable }
  UiSystem  (engine)     resolve rects (parent chain = E3 hierarchy) → hit-test pointer →
                         button states → emit UiEvent{ entity, signal } to the scene bus →
                         draw pass via Renderer2D in canvas space
  scene/EventBus (engine, tiny): named signals w/ payload Entity; scripts subscribe via
                         ScriptableEntity::OnSignal(name, entity) or Signals().Connect(...)
  scene/FlowMachine (engine): executes a FlowAsset (.cflow) — states, transitions on
                         signals/timers/keys, guard conditions on reflected fields, actions
                         (LoadScene via SceneManager, SetField, EmitSignal)
Starforge:
  2D editor mode (ortho camera, sprite tools), UI edit overlay (rect handles), Flow Graph
  panel (node editor over .cflow), tilemap painter
```

**Rendering order contract:** world (SceneRenderer HDR+post) → **canvas UI** (Renderer2D,
screen-space, sorted by Canvas.SortOrder then ZOrder) → editor overlays. In the editor the
canvas draws inside the viewport at the game resolution; in PlayerLayer it draws to the window.
SceneRenderer's existing `DrawOverlay2D` callback is the injection point (it runs after
composite with the LDR target bound — verified in `SceneRenderer.h`).

---

## 2. Work orders

### U1 — UI components + UiSystem (rects, draw, hit-test)

**Files:** NEW `Cosmic/src/scene/ui/UiComponents.h` (+ registrations in
`reflect/TypeRegistry.cpp`), `scene/ui/UiSystem.h/.cpp`; MODIFY `renderer/Renderer2D.h/.cpp`
ONLY if text/9-slice verbs are missing (verify first — add `DrawString(font, text, rect, …)`
and a 9-slice quad helper as generic verbs if absent; font atlas: reuse the engine's existing
font machinery if any renders outside ImGui, else vendor stb_truetype baking into a
`Texture2D` — decide by reading `graphics/`, note the choice); NEW `tests/test_ui_rects.cpp`.

**Spec:** rect resolution is a pure function: parent rect × anchors + offsets, pivot-aware;
canvas root = viewport rect (ScaleWithHeight multiplies all sizes by `viewportH / refH` —
one scalar, keeps pixel art crisp at integer scales). Hit-test: topmost interactable button
under the pointer (Z then hierarchy order); pointer position comes from the existing
screen-px mouse contract (`Input::GetMouseScreenPosition` − viewport pos — the
`BeginViewportOverlay`/`IsViewportHovered` pattern). Button states Normal/Hover/Pressed fire
`UiEvent{signal}` on release-inside (standard). `UiSystem::Update(scene, viewportRect, input)`
+ `UiSystem::Render(scene, viewportRect)` are engine free functions so editor and PlayerLayer
share them. Consume the click (set the event Handled / skip ScenePicker) when UI is hit —
wire into Starforge's `ViewportController::OnUpdate` pick path and PlayerLayer input.

**Gotchas:** the E3 world-transform memo is for 3D — UI rect resolution walks the same
`RelationshipComponent` links but must NOT create `TransformComponent` dependencies (UI
entities still have Transform from `CreateEntity`; ignore it, RectTransform is authoritative
under a Canvas). DPI: canvas scaling is game-resolution-based, not OS-DPI-based (games render
at viewport size; ImGui DPI rules don't apply).

**Acceptance:** headless: anchor/pivot matrix of cases (centered, stretched, corner-pinned,
nested) each ±0.5 px; hover/press/release-inside/release-outside state transitions. In-app: a
canvas with an image + text + 3 buttons renders in editor viewport AND via `--project`,
hover/press tints work, clicks don't leak to 3D picking.

**Status:** ✅ 2026-07-11 (engine + tests + PlayerLayer 2026-07-08; editor click path
2026-07-11). Editor-viewport click consumption landed in `ViewportController::OnUpdate`:
while PLAYING the canvas is live in the viewport (`UiSystem::Update` — hover/press tints +
EventBus signals) and a pointer over interactable UI consumes the click before the
ScenePicker; while EDITING a click on any UI element SELECTS it via the new engine verb
`UiSystem::HitTest` (topmost drawable element under a point — the mesh-only ID pass can't
see rect UI). Paused Play behaves like edit (PlayerLayer's pause gate parity). Headless
acceptance passes (anchor matrix, button transitions, EventBus click routing, HitTest
topmost-Z — `test_ui_rects.cpp`, CosmicTests 263/263). The in-app visual pass (tints +
no-leak click check on GPU) rides the user's phase acceptance (U8 ledger row).

### U2 — Scene event bus + script hookup

**Files:** NEW `Cosmic/src/scene/EventBus.h` (per-Scene member, tiny: `Emit(name, Entity)`,
`Connect(name, fn) -> handle`, unsubscribe-on-destroy discipline like the telemetry
unsubscribe-handle pattern); MODIFY `scripting/ScriptableEntity.h` (`OnSignal(const
std::string&, Entity source)` virtual + `Signals().Emit(...)` proxy), `ScriptHost` dispatch;
tests.

**Spec:** signals are strings (author-friendly, serialized in buttons/flow); dispatch is
same-frame, ordered, main-thread. Buttons emit through it (U1); FlowMachine subscribes (U5);
scripts can emit (`Signals().Emit("door_opened")`) — this is the one channel connecting UI,
flow, and logic. Telemetry-style no-op when a scene has no subscribers.

**Acceptance:** headless: button-sim → script `OnSignal` receives; script emit → second
script receives; destroyed subscriber never fires (handle safety).

**Status:** ✅ 2026-07-08. `scene/EventBus.h/.cpp` + Scene member + `Signals()`/`OnSignal`
+ ScriptHost ConnectAny routing. Acceptance covered by test_ui_rects (bus + click) and
test_scripthost (OnSignal reach + emit + route-drops-on-Destroy).

### U3 — 2D authoring mode in Starforge

**Files:** Starforge `StarforgeApp`/`ViewportController` (camera mode), `Prefabs.h`/Entity
menu (2D creates), template additions (`assets/templates/` gains a 2D scene variant);
engine: verify `SpriteRendererComponent` fields (it exists + is reflected since E1) cover
source-rect; extend it (end-of-struct, ABI note) with `SourceRect` + `PixelsPerUnit` if absent.

**Spec:** **2D mode toggle** (toolbar): switches the editor camera to orthographic top-down…
no — to a Z-facing ortho (XY plane, +Y up, sprites at Z by sort order), pan/zoom bindings
(MMB pan, wheel zoom — reuse the orbit controller's ortho support if it has one; else a small
`Camera2DController` engine-side mirroring the app-side patterns), grid becomes a 2D pixel
grid with unit snapping. Entity ▸ 2D ▸ Sprite/Camera(ortho)/… creates. **Pixel-perfect
preset:** per-texture `.cmeta`-style sampling choice or a project default — point filtering +
no mips for pixel art (the `Texture2D` sampling verb exists — S4 hardening added
`Texture::SetSampling`); a "Pixel Art" project template preselects it. Sorting: sprites sort
by `ZOrder` (add to SpriteRenderer if absent) then Y (toggle) — document the 2D draw pass
(Renderer2D already batches; scene 2D render path: verify `Scene` has an `OnRender2D` — if
sprites currently render only through app code, add the generic scene sprite pass here,
engine-side, default-off unless sprites exist [compat gate]).

**Gotchas:** mixing 2D sprites + 3D in one scene is allowed (HUD-in-world, 2.5D) — the sprite
pass draws inside the main scene with depth write off, sorted back-to-front, exactly like the
existing transparent queue; don't invent a second compositor.

**Acceptance:** author a small sprite scene (background, 10 sprites, ortho camera) entirely
in-editor; crisp at 1x/2x/3x zoom (screenshot); plays standalone; 3D scenes unaffected.

**Status:** ✅ 2026-07-11 (engine slice 2026-07-08). Landed this session:
- **2D editor mode:** toolbar "2D" toggle → NEW engine `camera/Camera2DController` (ortho XY
  rig looking down −Z, MMB pan, wheel zoom-about-cursor, `FrameBounds`; pure `ScreenToWorld`/
  `PanBy`/`ZoomAboutPoint` headless-tested in `test_camera2d.cpp`), pixel grid (1-unit minors
  when ≥6 px, 10-unit majors, XY axes) + sprite wire-rect selection outlines
  (`DrawOverlayContent2D`), 1-unit snap armed on entry, F frames the XY extent, sprite
  rect-picking (topmost by ZOrder/key) ahead of the mesh ID pass, gizmo manipulates through
  the ortho camera (ImGuizmo auto-detects). `OrthographicCamera` gained a near/far
  `SetProjection` overload; the editor forces Skybox/IBL/Shadows off in 2D mode (a skybox
  under ortho is degenerate).
- **Generic sprite pass:** `Scene::OnRenderSprites(viewProjection, w, h)` — painter-sorted
  (ZOrder, then per-sprite `YSort ? -Y : Z`, then id), transparent-queue contract (depth test
  ON / write OFF / alpha), runs from the `DrawTransparent` hook in BOTH Starforge and
  PlayerLayer; a scene with no sprites returns before any GL call (compat gate).
  `SpriteRendererComponent` gained `TexturePath` (AssetPath, lazy-resolved) + `YSort` +
  the shared pure sizing rule `WorldSize` (ABI-appended, reflected).
- **Pixel-perfect preset:** engine `AssetLibrary::SetDefaultTextureSampling(filter, wrap)` /
  `Clear…` applied at texture load; `pixel_art` manifest key honored by PlayerLayer AND
  Starforge (set on project open, cleared on close); New Project dialog gained the
  "Pixel Art 2D (point-filtered textures)" template entry that stamps the key.
  `ProjectManifest` also gained `StartupFlow` so a settings save no longer drops the U5 key.
- Editor Play now advances flipbooks (`UpdateSpriteAnimations` in `TickPlay`).
Build green, `CosmicTests` **267/267**. The authored-scene screenshot pass (crisp at
1x/2x/3x) rides the user's phase acceptance.

### U4 — Sprite animation + tilemap v1

**Files:** engine `scene/Components.h`: `SpriteAnimationComponent{ SheetPath, FrameW, FrameH,
Frames, FPS, Playing, Loop, Row }` (flipbook over a sheet — mirrors the particle flipbook
pattern); `TilemapComponent{ TilesetPath, TileW, TileH, Columns, GridW, GridH,
std::vector<uint16> Cells }` + a packed serialization note (base64 or int array — pick the
diff-friendlier int array, it compresses fine in git); engine tilemap draw = one Renderer2D
batch walk; Starforge: tile painter overlay (LMB paint, RMB erase, palette panel showing the
tileset grid, flood fill, rect fill), sprite-sheet preview in the Inspector row.

**Gotchas:** tilemap edits go through undo as **cell-run commands** (coalesce a drag into one
command — the E7 merge-key pattern), not per-cell spam. Big maps: v1 caps GridW/H at 1024
(one draw batch per visible chunk of 64×64 — cull by camera rect).

**Acceptance:** paint a 100×60 map from a tileset, undo/redo strokes, save/reload identical;
animated sprite plays in editor Play + standalone at the authored FPS independent of frame
rate.

**Status:** ✅ 2026-07-11 (sprite animation 2026-07-08). Landed this session:
- **Engine `TilemapComponent`** (TilesetPath/TileW/TileH/Columns/GridW/GridH/ZOrder +
  `Cells` vector<uint16>, reflected except Cells; grid clamped 1..1024 via `EnsureCells`;
  pure 4-connected `FloodFill` shared by the editor + tests). Cells serialize as a plain
  int array through a named custom block in SceneSerializer (the NativeScript-Fields
  pattern) — diff-friendly, round-trip + save-of-load byte-stability headless-tested in
  `test_tilemap.cpp` on the acceptance-size 100×60 map.
- **Draw**: tilemaps interleave with sprites in `Scene::OnRenderSprites`' painter order
  (ZOrder, then Z); the cell walk is culled to the camera's world rect (invVP NDC-cube
  bounds — exact for the 2D ortho camera); one cell = one world unit from the entity's
  bottom-left; one SubTexture per distinct tile id per draw. Entity rotation/scale ignored
  (v1, documented in the component header).
- **Starforge painter**: NEW `panels/TilePalettePanel` (tileset grid picker, tool radios,
  viewport-paint toggle; View▸Tile Palette; Entity▸2D▸Tilemap creates + opens it),
  viewport painting in 2D mode (LMB Paint drag = ONE coalesced undo stroke via
  `Commands::TileEdit` merge keys — the VoxelEdit pattern; Flood + Rect commit as single
  `TileEditRun` steps; RMB erases), map-bounds/hover-cell/rect-preview overlay lines.
- *Deferred (noted):* the Inspector sprite-sheet preview row rides Phase 23's Inspector v2
  (asset-slot previews, doc 22) — the Tile Palette's atlas grid covers tileset preview.
Build green, `CosmicTests` **270/270**. The interactive paint/undo/save pass on GPU rides
the user's phase acceptance.

### U5 — FlowMachine + `.cflow` asset (engine runtime)

**Files:** NEW `Cosmic/src/scene/FlowMachine.h/.cpp`, `.cflow` = JSON via the existing
generic reflected-struct serializer (`SceneSerializer::SaveReflectedToString` family — E17) or
a bespoke small schema (nodes need arrays-of-structs; use nlohmann directly in the .cpp,
schema below); MODIFY `layers/PlayerLayer` (owns one; starts at the manifest's startup flow if
present) + Starforge play mode (same); NEW `tests/test_flowmachine.cpp`.

**Schema (`.cflow`, versioned):**
```json
{ "cosmic_flow": 1,
  "start": "MainMenu",
  "states": [
    { "name": "MainMenu",  "scene": "project://scenes/MainMenu.cscene",
      "onEnter": [ {"emit": "menu_shown"} ],
      "transitions": [
        { "on": "play_clicked",  "to": "Game", "transition": "Fade" },
        { "on": "quit_clicked",  "to": "@quit" },
        { "on": "continue_clicked", "to": "Game",
          "if": { "entity": "SaveSlot", "component": "SaveState", "field": "HasSave",
                  "op": "==", "value": true } } ] },
    { "name": "Game", "scene": "project://scenes/Main.cscene",
      "transitions": [ { "on": "key:Escape", "to": "PauseOverlay", "push": true },
                        { "on": "timer:180", "to": "TimeUp" } ] },
    { "name": "PauseOverlay", "scene": "project://scenes/Pause.cscene", "overlay": true,
      "transitions": [ { "on": "resume_clicked", "to": "@pop" } ] } ] }
```
**Semantics:** events = EventBus signals (U2) + `key:` bindings + `timer:` seconds-in-state;
guards (`if`) read any reflected field via the E1 registry (entity found by Tag in the ACTIVE
scene; missing entity/field = guard false + one Console warning); actions v1: `emit`,
`setField`, scene changes implied by `to`. `@quit` closes the app (PlayerLayer) / stops Play
(editor). **Overlay states** (`overlay`/`push`/`@pop`): v1 implements push/pop as *pause the
under-scene + additively keep it rendered* using `Application::Pause` Feature-B semantics —
if additive scene rendering proves heavy, v1 fallback: overlay = same-scene UI canvas toggle,
and push/pop is deferred to v2 (**decide during implementation, document which shipped**).
The machine itself is scene-agnostic and headless-testable (inject a fake scene-loader — the
`SceneLoader` std::function E5 already defines).

**Acceptance:** headless: start→transition-on-signal→guard-blocks→timer fires→quit reached,
with a scripted fake loader proving load calls + order; unknown state/scene = validation error
list (used by U6). In-app: menu→game→pause→resume works via real buttons.

**Status:** ✅ engine 2026-07-08. `scene/FlowMachine.h/.cpp` — full `.cflow` parse/save/
validate, state stack (overlay push/pop), signal/key/timer transitions, reflected-field
guards, emit/setField actions, injected scene-loader. Wired into PlayerLayer behind the
`startup_flow` manifest key (compat-gated). **Shipped decision:** overlay push/pop is a
machine-level STATE STACK; a scene-less overlay reuses the under-scene, the host renders the
TOP scene — additive under-scene rendering (pause menu over live game) is a v2 follow-up.
`tests/test_flowmachine.cpp` covers the full run + guards + validation + round-trip.
**2026-07-11:** Starforge play-mode adopted the flow (see U7's status — the "Flow" Play
toggle boots the manifest `startup_flow` with scene-swap script/physics rebinding); the
in-editor menu→game demo rides the user's U8 acceptance recording.

### U6 — Flow Graph editor panel

> **2026-07-11 (v4 roadmap):** build the canvas internals REUSABLE from day one — Phase 25
> (doc 24 Q1) extracts them into `widgets/NodeCanvas` for the Story Graph + post-chain
> editors. Keep node/pin/link plumbing separate from flow-specific node content.

**Files:** VENDOR `Cosmic/dependencies/imgui-node-editor` (thedmd, MIT — **verify it compiles
against the vendored imgui version first**; fallback: ImNodes [Nelarius, MIT], smaller
feature set, same data model); Starforge NEW `panels/FlowGraphPanel.*`; View menu toggle.

**Spec:** open/create `project://flows/*.cflow`; states as nodes (scene name + preview
thumbnail if S7 wrote one), transitions as edges labeled `on` (+ guard badge); edit via side
inspector (event name picker listing known signals — scan UiButton components of the target
scene + hand-entered), start-state marker, `@quit` node built-in. Validate on save (U5's
validator): unreachable states + missing scenes render as red badges. Layout persisted in the
`.cflow` (`"editor": {"pos": …}` block — ignored by the runtime). Undo: v1 = document-level
(snapshot on structural change through the CommandStack), noted.

**Acceptance:** author the U5 acceptance flow entirely in the panel (no hand-JSON); reopen
preserves layout; a deleted scene shows red; runtime executes the authored file unchanged.

**Status:** ✅ 2026-07-11. Landed:
- **Vendored** `Cosmic/dependencies/imgui-node-editor` (thedmd, MIT, master
  `021aa0ea` 2026-03-29 — the tag v0.9.3 predates the current-imgui fixes). ONE local
  patch: `imgui_extra_math.inl`'s `operator*(float, ImVec2)` version-guarded (imgui 1.92.x
  ships it itself; collided C2084). See the dependency's `VENDOR-NOTES.md`. Compiled into
  Starforge.dll with warnings off for the vendored TUs (external code); ImGui symbols come
  from the propagated static imgui in-tree / the bundled sources standalone.
- **Separable canvas** (the 2026-07-11 v4 note): `widgets/NodeCanvas.h/.cpp` — a generic
  RAII node/pin/link host (context lifecycle, Begin/End bracket, `QueryEdits` create/delete
  gestures, selection + node-position API, settings file disabled: the DOCUMENT owns
  layout). Zero flow knowledge — Phase 25 Q1 extracts it for the Story Graph.
- **`panels/FlowGraphPanel`** (View▸Flow Graph): open/create `project://flows/*.cflow`,
  states as nodes (start marker, scene stem, RED badges for missing/unset scenes and
  unreachable states — BFS from start on top of `FlowAsset::Validate`), transitions as
  per-row out-pins with links (drag a row pin to retarget; drag the node's "+ link" pin to
  add a transition; Del deletes links/nodes), built-in `@quit` node (`@pop` shows as a row
  badge — no dangling edge), side inspector (state rename retargets references, scene
  picker from `project://scenes`, overlay flag, set-start, onEnter emit/setField actions;
  transition On picker fed by UiButton signals parsed from the flow's scene JSONs +
  `key:Escape` + hand-entered, To picker incl. @quit/@pop, push flag, full guard editor),
  node drags persist into `FlowState::EditorPos` (`"editor":{"pos":…}` — runtime-inert,
  round-trip now covered in `test_flowmachine.cpp`).
- **Deviation (documented):** v1 undo is PANEL-LOCAL snapshot Undo/Redo buttons rather
  than the scene CommandStack — flow edits are file-scoped documents; sharing the scene
  stack would make viewport Ctrl+Z pop flow edits. Scene-preview thumbnails on nodes are
  deferred (S7 thumbnails are per-project, not per-scene).
Build green, `CosmicTests` **271/271**. The in-panel authoring pass (mouse-driven) rides
the user's phase acceptance with U8.

### U7 — Game-view correctness (play like it ships)

**Files:** Starforge `StarforgeApp` (play camera + resolution), engine none expected.

**Spec:** closes the E13 deviation: during Play the viewport renders from the **primary
`CameraComponent`** (fallback: editor camera + warning) with an **eject toggle** (toolbar) to
fly free while the sim runs; aspect/resolution presets for the game view (Free / 16:9 /
1920×1080 / custom from project.cproj) letterboxing the viewport so authored UI anchors are
truthful; optional cursor-capture checkbox for mouse-look games (esc releases — PlayerLayer
gets the same via manifest key).

**Acceptance:** a 16:9-authored menu shows identical proportions in editor Play (letterboxed)
and packaged fullscreen; eject works and re-docking returns to the game camera.

**Status:** ✅ 2026-07-11. Landed:
- **Primary-camera Play:** while playing (not ejected) the viewport renders from the scene's
  primary `CameraComponent` (`PoseCamera` — the editor twin of PlayerLayer's holder); no
  primary camera = editor camera + a once-per-session Console warning (mirrors the player).
  Picking/voxel rays unproject through the ACTIVE render camera (`renderCamOverride`).
- **Eject toggle** (Play controls): fly the editor camera while the sim runs; unchecking
  returns to the game camera. Each Play starts docked.
- **Aspect presets** (Free / 16:9 / 1920×1080 / Project = manifest `[window]` size): the
  frame is NDC-scaled into a centered letterbox band, bars drawn by the viewport overlay,
  and the canvas UI lays out INSIDE the band (new engine `UiSystem::Render(scene,
  canvasRect, targetW, targetH)` overload — anchors identical to the shipped app; the UI
  pointer uses the same band). Band ratios stored as viewport fractions (DPI-safe).
- **Cursor capture:** new generic engine verb `Window::SetCursorCaptured(bool)` (GLFW
  disabled-cursor mode). Editor: "Capture" checkbox, Esc releases + unchecks, Stop/eject
  release. PlayerLayer: `capture_cursor` manifest key — captured on boot, Esc releases,
  click recaptures, released on detach.
- **BONUS (closes U5's remaining line): flow-driven editor Play** — when the manifest names
  `startup_flow` (and the new "Flow" toggle next to Play is on), Play boots the `.cflow`
  from its start state exactly like the shipped player: button signals drive transitions,
  scene swaps rebind scripts+physics, `key:Escape` feeds the flow, `@quit` stops Play. A
  state referencing the OPEN scene plays the live snapshot (unsaved edits included).
Build green, `CosmicTests` **271/271**. The 16:9-proportions screenshot comparison
(editor Play vs packaged) rides the user's phase acceptance.

### U8 — Phase acceptance: the zero-code app + the 2D sample

> **2026-07-11 (v4 roadmap):** the 2D-sample half may alternatively be satisfied by Forge
> Isle's tent-game vignette (doc 27 Z6) if that lands first — note it here when it does.
> The zero-code two-screen app requirement is unchanged either way.

**Files:** template additions; `docs/design/` demo script.

**Spec (recorded, the phase's definition of done):**
1. **Zero-code two-screen app:** new project → author MainMenu scene (canvas, title text,
   Play/Quit buttons) + a game scene → author `Main.cflow` in the graph panel (menu → game on
   `play_clicked`, Escape → pause overlay, quit node) → Play in editor → **Package → the
   shipped exe boots to the menu and navigates** — no C++ written.
2. **2D sample "ForgePong"** (template pickable): sprites, ortho camera, a `PaddleController`
   script (6 lines), score `UiText`, flipbook ball-hit effect, flow (menu→game→win screen).
   Proves 2D + UI + flow + scripts together; packaged and run clean.

**Status:** ✅ STAGED 2026-07-11 — the recorded demo itself is on the USER's acceptance
ledger (script: [`../design/ui-flow-2d-acceptance.md`](../design/ui-flow-2d-acceptance.md)).
What's in the tree:
- **Zero-code app "FlowDemo"** (homescreen ▸ *Flow Sample*, self-built): MainMenu
  (canvas/title/Play/Quit) + Game (world + HUD hint) + Pause (scrim overlay) scenes,
  `flows/Main.cflow` (menu→game on `play_clicked`, Escape→Pause **push**, Resume→`@pop`,
  Quit→`@quit`), manifest boots the flow — NO scene references a script. Openable in the
  U6 Flow Graph panel; plays via the U7 "Flow" Play toggle; packages via S5.
- **"ForgePong"** (homescreen ▸ *Pong Sample*, self-built): 16×9-unit sprite court +
  ortho primary camera (U3), `PaddleController` (six lines, W/S vs arrows via a reflected
  field override) + `PongBall` (bounce/steer/speed-up, writes ScoreL/ScoreR `UiText`s,
  emits `left_wins`/`right_wins`) — both shipped as TEMPLATE scripts (registered in the
  scaffold `Module.cpp`, compile-smoked headless in `test_template_scripts.cpp`), a
  procedurally generated 8-frame ring-burst sheet (`ImageIO::WritePNG`) driving the U4
  flipbook hit effect, menu→game→win `.cflow` with rematch/menu/Escape routes.
Build green, `CosmicTests` **272/272**.

---

## 3. Parked (with unlocks)

| Item | Unlock |
| --- | --- |
| Visual *logic* scripting (blueprint-style general graphs) | explicit user demand after flow ships — the doctrine stays "logic is C++" |
| Rich text/markup, localization tables | a shipped app needs them |
| UI animation curves (tweening component) | first juice-pass on a real app (LookupTable curves are the seam) |
| Tilemap auto-tiling/rule tiles, colliders from tiles | a real 2D game project (doc 14 Jolt makes tile colliders trivial to add then) |
| Controller/keyboard UI navigation (focus chain) | gamepad-first app request |

## 4. Order

U1 → U2 → U3/U4 (parallel) → U5 → U6 → U7 → U8. U5 can start any time after U2 (engine-only).

## Kickoff prompt

> You are implementing ONE work order from `docs/plans/16-phase17-ui-flow-2d-plan.md` in
> `C:\dev\Cosmic`. Read §0–§1 first — the rendering-order contract and the entities-are-UI
> doctrine bind every item. Verify the engine surface named in your work order by reading the
> headers (several items say "verify first — add only if absent"). Engine gains only generic
> modules; UI/flow logic must be headless-testable; compat gate for all shipped apps. Build
> with the roadmap's non-interactive cmake recipe; never run git write commands. Finish with
> the Acceptance + status banner update.
