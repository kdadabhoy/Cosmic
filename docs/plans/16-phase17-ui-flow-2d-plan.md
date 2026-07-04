# Phase 17 Plan — In-Game UI, Screen Flow, and 2D Game Authoring

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

**Status:** ☐

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

**Status:** ☐

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

**Status:** ☐

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

**Status:** ☐

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

**Status:** ☐

### U6 — Flow Graph editor panel

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

**Status:** ☐

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

**Status:** ☐

### U8 — Phase acceptance: the zero-code app + the 2D sample

**Files:** template additions; `docs/design/` demo script.

**Spec (recorded, the phase's definition of done):**
1. **Zero-code two-screen app:** new project → author MainMenu scene (canvas, title text,
   Play/Quit buttons) + a game scene → author `Main.cflow` in the graph panel (menu → game on
   `play_clicked`, Escape → pause overlay, quit node) → Play in editor → **Package → the
   shipped exe boots to the menu and navigates** — no C++ written.
2. **2D sample "ForgePong"** (template pickable): sprites, ortho camera, a `PaddleController`
   script (6 lines), score `UiText`, flipbook ball-hit effect, flow (menu→game→win screen).
   Proves 2D + UI + flow + scripts together; packaged and run clean.

**Status:** ☐

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
