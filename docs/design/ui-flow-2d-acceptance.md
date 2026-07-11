# Phase 17 acceptance demo — UI, screen flow, 2D (doc 16 U8)

> **Created 2026-07-11.** The recorded, user-run acceptance for Phase 17. Everything
> below is staged in the tree: the two samples build themselves from the homescreen,
> and every editor feature referenced shipped with U1/U3/U4/U6/U7. Record one take
> (or a take per section); park the recording with the other acceptance media.

## A. The zero-code two-screen app (U8 spec #1 — the phase's definition of done)

1. Launch `Starforge.exe` → homescreen → **Flow Sample** (builds `FlowDemo` on first
   click, then opens it). *Nothing in this project references a script.*
2. Look around: `scenes/MainMenu.cscene` (canvas + title + Play/Quit buttons),
   `scenes/Game.cscene` (world + HUD hint), `scenes/Pause.cscene` (scrim + Resume/Quit).
3. **View ▸ Flow Graph** → the flow picker already shows `Main` — open it. Confirm:
   three state nodes + the built-in `@quit` node, `[start]` marker on MainMenu,
   transition rows (`on play_clicked`, `on key:Escape [push]`, `on resume_clicked -> @pop`),
   green "valid". Drag nodes around → **Save** → close/reopen the panel → layout kept.
   Temporarily rename a scene file in the Content Browser → reopen the panel → the state
   shows RED (missing scene) + the problem listed; rename it back.
   *(To demo authoring from scratch instead: **New** flow, **+ State** twice, drag the
   "+ link" pin between nodes, set On/To in the side inspector — no JSON is ever typed.)*
4. **Play** (leave the *Flow* toggle checked) → the viewport boots the MENU. Hover the
   buttons (hover/press tints — U1), click **Play** → the game scene loads. Press
   **Esc** → the pause screen pushes; click **Resume** → back in the game (`@pop`);
   Esc again → **Quit** → Play stops (`@quit`).
5. **Package** (File ▸ Package) → run the shipped exe from `dist/FlowDemo/` → it boots
   to the menu and navigates identically. **No C++ was written.**

## B. ForgePong (U8 spec #2 — 2D + UI + flow + scripts together)

1. Homescreen → **Pong Sample** (builds `ForgePong` on first click) → **Build Scripts**
   (Ctrl+B; compiles `PaddleController` — six lines — and `PongBall`) → **Play**.
2. Menu → **Play** → the court: sprites + ortho camera (U3), score `UiText`s (U1),
   W/S vs Up/Down paddles, ball speeds up per return, the ring-burst **flipbook** plays
   at each impact (U4). First to 5 → the WIN screen (flow on `left_wins`/`right_wins`) →
   **Rematch** and **Menu** both work. Esc from the game returns to the menu.
3. Package → the shipped exe runs the same match clean.

## C. Editor-feature spot checks (the per-item on-GPU passes)

- **U1 click consumption:** in FlowDemo's game scene during Play, click a HUD-free area
  → picking still selects world entities; click a button → NO 3D pick occurs. In edit
  mode, click a button → the UI entity is selected (not deselected/cleared).
- **U3 2D mode:** open ForgePong `Game.cscene`, toolbar **2D** → ortho XY view, MMB pan,
  wheel zoom about the cursor, pixel grid + XY axes, clicking sprites selects them
  (paddles/ball), F frames the court, gizmo translates in-plane with 1-unit snap armed.
  Zoom to 1x/2x/3x on the ball → crisp edges (screenshot). 3D scenes look unchanged.
- **U4 tilemap:** in any 2D project: Entity ▸ 2D ▸ Tilemap → set `TilesetPath` (any
  texture; try the pong `textures/hit.png` at TileW/H=16) → Tile Palette: pick a tile,
  enable *Paint in viewport* (2D mode on) → LMB-drag paints (ONE undo step per stroke —
  Ctrl+Z removes the whole stroke), RMB erases, Flood + Rect fill, save + reopen the
  scene → the map is identical.
- **U7 game view:** in ForgePong Play, set the aspect combo to **16:9** and resize the
  viewport tall → letterbox bars appear and the menu buttons keep their authored
  proportions (compare against the packaged fullscreen run). **Eject** → fly the editor
  camera while the ball keeps moving; uncheck → back to the game camera. **Capture** →
  cursor locks; Esc releases.

## D. Where the artifacts land

| Artifact | Path |
| --- | --- |
| Zero-code sample | `assets/projects/FlowDemo/` (self-built by the homescreen button) |
| Pong sample | `assets/projects/ForgePong/` (self-built; scripts in `src/scripts/`) |
| Flow files | `<project>/flows/Main.cflow` (open in View ▸ Flow Graph) |
| Packaged exes | `dist/<Project>/` after File ▸ Package |
