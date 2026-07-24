# Forge Isle — flagship showcase design (Phase 28 / Z1)

> **Created 2026-07-14.** The content spec of record for `docs/plans/27-phase28-flagship-sample-plan.md`
> (Z1–Z7). Forge Isle is a small third-person island adventure built **entirely with Starforge**
> — a packaged, clean-machine-accepted product whose every beat exists to demonstrate an engine
> capability (roadmap decision #12). Sample line: ForgePlayground → ForgePong → ForgeBlocks →
> **Forge Isle**. The §5 feature→moment matrix is the phase contract: every Phase 14–27
> capability maps to a specific moment; Z7's DoD walks the matrix live on a recording.
>
> **House rules (doc 27 §0):** app-side only — zero `Cosmic/src` edits (missing verbs get filed
> in the owning phase doc); all authoring through Starforge paths (scenes/recipes/scripts/
> `.cflow`/`.cstory`); license-clean assets (CC0/self-made + credits file); ≥60 fps at 1080p in
> every scene, measured with the T17 profiler panel.

---

## 1. The pitch (30 seconds)

You wash ashore on a volcanic island at dawn. A hermit at a campfire sends you to relight the
island's three **forge beacons**: one across a collapsed physics ruin, one through a canyon
patrolled by ash-hounds, one buried inside a voxel dig site. Day rolls toward night as you work;
your lantern lights itself at dusk. Lighting the last beacon ignites the **Great Forge** at the
summit — the finale. In the hermit's tent hides **Embers**, a playable retro table-game — a 2D
top-down vignette in the dark, lit by your lantern (the engine's range statement, and doc 16
U8's promoted 2D sample).

## 2. Beat map

Player-facing beats, in order. Each names its scene, its quest-state effect (Q2 flow variables),
and the capabilities it exists to show (full mapping in §5).

| # | Beat | Where | What happens | State effect | Shows (headline) |
| --- | --- | --- | --- | --- | --- |
| B0 | **Title** | `Title.cscene` | Island vista under the physical sky; Start / Quit buttons. Zero C++. | — | U1 UI, U5 flow, X1 sky |
| B1 | **Washed ashore** | `Island.cscene` (south beach) | Dawn (~06:30). Ocean surf behind you, HUD fades in with move hints, minimap in the corner. Walk up the beach. | — | J6 character, terrain+J7, water, X7 minimap |
| B2 | **The hermit's camp** | Island (camp, SE) | Campfire (curl-noise smoke + point light), hermit with an X6 nameplate. Talk: `.cstory` intro with guarded options; quest granted. The tent flap prompt appears (→ B7 any time). | `MetHermit=true` | Q3/Q4 story, X6 anchors, X3 particles |
| B3 | **The ruin** (physics) | Island (west headland) | A collapsed colonnade: Jolt crate/column stacks. Shove a leaning column to crash a ramp down, climb, light **Beacon 1** (interact prompt → flame ignites). | `BeaconsLit 0→1`, time +4h | J1–J5 bodies/events, T13 Active (flame), flow vars |
| B4 | **The canyon** (nav AI) | Island (north) | 4–6 ash-hounds patrol the canyon floor and ramps (DetourCrowd). Spotted ⇒ they converge; break line-of-sight or outrun them. **Beacon 2** on the rim. | `BeaconsLit 1→2`, time +4h | N1–N5 navmesh/agents, H9 systems |
| B5 | **The dig site** (voxel) | Island (east quarry) | Grab the digging tool (socketed to the hand). Dig through a collapsed shaft, place blocks to bridge a gap, reach **Beacon 3** in the excavated vault. Edits persist for the session. | `BeaconsLit 2→3`, time → night | V1–V6 voxels, M4 sockets |
| B6 | **Nightfall + finale** | Island (summit) | By now it is night (physical-sky stars/dusk band); the lantern lit itself at dusk; fireflies at the lake. With 3 beacons lit the summit forge ignites: bloom/vignette swell, finale `.cstory`, then credits. | flow → `Credits` | X1/X2 sky, Q5 vignette, M6 crossfades |
| B7 | **The tent game** (any time after B2) | `TentGame.cscene` (flow push) | **Embers**: one-screen top-down tilemap board in darkness + 2D lights. Guide the spark to collect embers before they cool; win/lose returns to the tent. | `TentWins+=1` on a win | U3/U4 2D+tilemaps, X5 2D lights, U5 push/pop |
| B∞ | **Pause** (anywhere) | `Pause.cscene` (flow push) | Esc pushes a UI-only pause scene — the island fully freezes (see §7.1). Resume pops; Quit returns to Title. Zero C++. | — | U5 stack, U1 UI |

Critical path ≈ 12–18 minutes. Beacons can be lit in any order (each beat gates only on reach,
not sequence); the hermit's hint lines change with `BeaconsLit` (Q2-guarded story options).

## 3. Scene list

| Scene | Kind | Contents | Z-item |
| --- | --- | --- | --- |
| `scenes/Title.cscene` | zero-code UI | Vista camera over the island greybox, physical-sky dawn, title canvas (logo text, Start/Quit buttons emitting `start`/`quit`) | Z1 |
| `scenes/Island.cscene` | THE island | Terrain recipe (island falloff) + TerrainCollider, ocean + lake water recipes, camp/ruin/canyon/dig-site/summit blockouts (→ dressed in Z3), beacons, player, HUD canvas, Environment (physical sky), hermit + hounds (Z4), dig volume (Z3/Z5) | Z1→Z6 |
| `scenes/Pause.cscene` | zero-code UI | Dim backdrop image, Resume / Quit-to-Title buttons (`resume`/`quit_title`) | Z1 |
| `scenes/TentGame.cscene` | 2D vignette | Ortho camera, tilemap board, sprite player+embers, darkness (`Ambient2D`) + `Light2D` lantern radius, hotbar-style UI row, win/lose signals | Z6 |
| `scenes/Credits.cscene` | zero-code UI | Credits text + timer transition back to Title | Z5 |
| `flows/Main.cflow` | app flow | `Title → Island` (`start`), `Island →(push) Pause` (`key:Escape`), `Pause → @pop` (`resume`/`key:Escape`) / `→ Title` (`quit_title`), `Island →(push) TentGame` (`tent_enter`), `TentGame → @pop` (`tent_exit`), `Island → Credits` (`finale_done`), `Credits → Title` (`timer:8`), `Title → @quit` (`quit`). Variables: `MetHermit` (bool), `BeaconsLit` (number), `TentWins` (number) | Z1 (vars used Z5) |
| `stories/HermitIntro.cstory` | dialogue | Intro + quest grant; options guarded on `MetHermit`/`BeaconsLit`; `Once` lore branches | Z5 |
| `stories/HermitHints.cstory` | dialogue | Revisit hints — lines keyed to `BeaconsLit` 0/1/2 | Z5 |
| `stories/Finale.cstory` | dialogue | Summit finale beats; `OnExit` emits `finale_done` | Z5 |

## 4. Asset list (license-clean)

**Default posture: procedural/hand-built** (the `ProceduralMeshes.h` precedent) — the greybox
and most of the dressed island use engine primitives, terrain/water/particle recipes, and solid
`.cmat` colors. Fonts already in-tree: Roboto (Apache 2.0), Lucide (ISC). Every third-party file
lands in `Projects/ForgeIsle/assets/` with a line in `Projects/ForgeIsle/CREDITS.md`.

### 4.1 Needed assets (⚠ user-supplied — flagged early, none block Z1)

| # | Asset | For | Suggested source (license) | Fallback if not supplied |
| --- | --- | --- | --- | --- |
| 1 | Rigged humanoid glTF/GLB + idle/walk/run/jump (+interact) clips | Z2 player | **Quaternius** "Universal Animation Library"/Adventurers (CC0) or **KayKit** Adventurers (CC0) | Capsule + socketed prop "robot" from primitives; crossfades still demoed on #2 |
| 2 | Quadruped creature GLB with 2–3 clips | Z4 ash-hounds | **Khronos Fox.glb** (model CC0, animations CC-BY 4.0 — credit AsoboStudio; already used for A2 acceptance) or Quaternius Animals (CC0) | Primitive-built hound (box body + cone snout), no skinning — N-series still fully demoed |
| 3 | ~6 audio files: waves loop, night crickets loop, fire crackle loop, UI click, beacon-ignite sting, win sting | Z3/Z5/Z6 | **Kenney** Audio packs (CC0) / freesound CC0 picks | Ship silent; audio rows in §5 demo with any single CC0 wav |
| 4 | 16×16 tileset PNG (~8 tiles: floor, wall, water, accent) | Z6 tent game | **Kenney** 1-bit / Tiny Dungeon (CC0) | Procedurally generated tileset PNG written by the project's `BuildTentTileset` dev script (ImageIO::WritePNG) |
| 5 | `icon.png` (≥256²) Forge Isle mark | Z7 branding | Self-made (I'll generate a molten-anvil mark procedurally if none supplied) | Same — generated |

Nothing else is external. Terrain splats use recipe **colors** (no textures needed); particles
use the procedural puff; UI is solid tints + Roboto; the greybox uses zero third-party files.

### 4.2 In-repo/procedural inventory

| Asset | Source | Use |
| --- | --- | --- |
| Terrain | `TerrainComponent` recipe (fBm + `EdgeFalloff` island) | The island landmass |
| Ocean + lake | `WaterComponent` recipes (Ocean / Lake presets) | Shoreline surf, camp lake |
| Blockouts, props | `PrimitiveMeshComponent` (box/sphere/plane/cylinder/cone/torus) + `.cmat` colors | Ruin columns, canyon walls, quarry, camp, beacons, tent |
| Fire/smoke/embers/fireflies | `ParticleEmitterComponent` recipes (+X3 curl noise, X4 bounds) | Campfire, beacon flames, finale, fireflies |
| Dig site earth | `VoxelVolumeComponent` (bounded bake → `.cvox`, ForgeBlocks precedent) | B5 |
| Fonts | Roboto / Lucide (in-tree) | HUD, dialogue, title |

## 5. Feature→moment matrix — THE CONTRACT

Every Phase 14–27 work order → a specific moment. **Venue** column: **[P]** demonstrated
in the packaged game; **[E]** demonstrated in the Z7 recording's editor segment (authoring
walk with the ForgeIsle project open — editor capabilities can't demo inside the packaged exe
by definition). Phases 19/21 are unlock-driven menus (nothing shipped ⇒ out of matrix), except
**R8**, which fired and is included. No other omissions.

### Phase 14 — Starforge hardening (H1–H10)

| Cap | Moment | Venue |
| --- | --- | --- |
| H1 pose-based orbit | Orbiting the camp while placing props — no MMB jump (authoring walk) | [E] |
| H2 SceneRenderer everywhere | The whole game: packaged Forge Isle renders sky/shadows/fog/HDR/post through the same path as the editor viewport — side-by-side shot | [P] |
| H3 lighting unification | Campfire + beacon point lights light the default-material blockouts and the hermit; light billboards while authoring | [P] |
| H4 HDRI skies | The environment-cube pipeline H4 built is what X1's physical sky bakes into — IBL matches the visible sky at every time of day (dawn vs night screenshots); HDRI mode itself shown on the Title vista A/B during the walk | [P] |
| H5 chrome rebuild | The authoring walk: one menu bar, toolbar, panel ✕, viewport titled `Island` | [E] |
| H6 native file dialogs | Import dialog for the character/creature GLBs (Z2/Z4 authoring) | [E] |
| H7 logging | `user://logs` from a packaged session shown post-run; Console sink during the walk | [E] |
| H8 ForgePlayground v2 | Lineage: the playground's scene-camera adoption is the Title/finale camera pattern | [E] |
| H9 SystemScript tier | `DayCycleSystem` (one system drives the sun), `HoundSystem` (one brain, every hound), `EmberSystem` (tent game pieces) | [P] |
| H10 consistency sweep | The authoring walk itself (consistent chrome/naming everywhere) | [E] |

### Phase 15 — Physics (J1–J9)

| Cap | Moment | Venue |
| --- | --- | --- |
| J1 Jolt vendored | Everything below | [P] |
| J2 PhysicsWorld service | The island Play session | [P] |
| J3 reflected body/collider components | Ruin columns/crates authored as Dynamic bodies in the Inspector | [E]+[P] |
| J4 play-session integration + tick order | Ruin simulates on Play; identical in packaged exe | [P] |
| J5 script API + collision events | Beacon braziers are trigger volumes (`OnTriggerEnter` → prompt); shoved column `OnCollisionEnter` → dust puff + thud | [P] |
| J6 character controller | The player: walk/run/jump over terrain, slopes, the crashed ramp | [P] |
| J7 terrain collider | Walking the island at all (≤2 cm parity — the beach-to-summit hike) | [P] |
| J8 editor authoring/debug draw | Collider gizmos + Physics Debug over the ruin (authoring walk) | [E] |
| J9 determinism proof | Trailer beat: the column crash lands identically in two recorded runs | [P] |

### Phase 16 — App platform (S1–S8)

| Cap | Moment | Venue |
| --- | --- | --- |
| S1 external projects | `Projects/ForgeIsle` is one (self-contained folder, VFS mounts, registry) | [E] |
| S2 Starforge.exe boot | The authoring walk happens in the dedicated editor | [E] |
| S3 product homescreen | Forge Isle tile (with thumbnail) on the library screen | [E] |
| S4 Launcher = dev tool | Dev-loop b-roll (optional beat) | [E] |
| S5 packaging v2 | Z7: Release build, embedded icon, window title/size from manifest, zip + Inno installer | [P] |
| S6 per-app user:// | Save/log isolation shown under `%LOCALAPPDATA%` after a clean-machine run | [P] |
| S7 run standalone / thumbnails / About | Run-standalone from the editor; the tile thumbnail | [E] |
| S8 clean-machine acceptance | Z7's DoD run | [P] |

### Phase 17 — UI, flow, 2D (U1–U8)

| Cap | Moment | Venue |
| --- | --- | --- |
| U1 UI entities + hit-testing | Title/Pause/Credits buttons; HUD (beacon counter, prompts); dialogue panel | [P] |
| U2 scene event bus | Button `start` signal → flow transition; `nav.arrived`, `beacon_lit`, story signals | [P] |
| U3 2D authoring mode | The tent game authored in 2D mode (pixel grid, sprite picking) — authoring walk; sprites pixel-crisp in game | [E]+[P] |
| U4 tilemaps + sprite animation | Embers board is a Tilemap; the spark is a flipbook `SpriteAnimation` | [P] |
| U5 FlowMachine + `.cflow` | The whole app spine: title→island→pause/tent push/pop→credits, zero C++ | [P] |
| U6 Flow Graph editor | `Main.cflow` edited on the node canvas (authoring walk) | [E] |
| U7 game view + eject | Every Play test through the primary-camera game view; eject mid-play over the canyon (walk); aspect presets checked for the HUD | [E] |
| U8 samples duty | **Absorbed here:** the tent game satisfies U8's "2D sample plays packaged" (dated note goes in doc 16 U8 when Z6 lands) | [P] |

### Phase 18 — Voxels (V1–V7)

| Cap | Moment | Venue |
| --- | --- | --- |
| V1 chunks/palette/serialization | Dig-site volume baked to `.cvox`, loads instantly | [P] |
| V2 mesher (greedy) | The quarry walls' merged faces (wireframe A/B in the walk) | [E]+[P] |
| V3 render integration | Dig site draws through the S12 queue with the rest of the island | [P] |
| V4 edit tools + script API | The digging tool: LMB dig / RMB place, gated to holding it | [P] |
| V5 Jolt chunk collision | Stand in the hole you just dug; bridge the gap with placed blocks | [P] |
| V6 generation/streaming | The dig-site interior is generated (caves recipe), bounded bake | [P] |
| V7 ForgeBlocks demo | Superseded-by: the dig site is the recorded voxel demo now | [P] |

### Phase 20 — Asset pipeline & animation (fired trio)

| Cap | Moment | Venue |
| --- | --- | --- |
| A1 assimp ON + rich import | Character + creature GLBs import (multi-mesh `#i` children, per-material `.cmat`s) | [E] |
| A4 PreviewRig | Their thumbnails in the Content Browser; Material panel preview sphere while tinting beacon `.cmat`s | [E] |
| A2 skeletal runtime | The player character + hounds animating in the packaged game (SSBO palettes, skinned shadows) | [P] |

### Phase 22 — Editor shell & viewport (R8, K1–K13)

| Cap | Moment | Venue |
| --- | --- | --- |
| R8 view modes | Canyon navmesh + ruin debugged in Wireframe/Entity-ID (walk) | [E] |
| K1 drop-a-file branding | Forge Isle `icon.png` in the window/taskbar + editor top-bar while its project is open; the Z7 hot-swap clip | [E]+[P] |
| K2 product toolbar | Transport used for every Play in the walk | [E] |
| K3 layout presets | Switching to the Animation preset to inspect the character | [E] |
| K4 undo UI | Undo badge/history popup during island dressing | [E] |
| K5 status bar | Visible all walk | [E] |
| K6 viewport strip + per-op snap | Snapping ruin columns at 0.25 m / 15° | [E] |
| K7 fly/possess camera | Flying the canyon while placing patrol markers; possessing the vista camera | [E] |
| K8 axis navigator | Used while blocking the quarry | [E] |
| K9 stats chips | On while dressing the island (tris/draws sanity) | [E] |
| K10 infinite grid | Under the tent-game board in 2D mode | [E] |
| K11 universal gizmo | Placing beacons (move+rotate in one gizmo) | [E] |
| K12 selection outline | Selected hermit under the campfire light | [E] |
| K13 viewport drag-drop | Dragging beacon/crate prefabs-from-browser into the ruin | [E] |

### Phase 23 — Asset workflows & Inspector/Hierarchy v2 (T1–T18)

| Cap | Moment | Venue |
| --- | --- | --- |
| T1 reflection metadata v2 | Units/tooltips on Environment + NavMesh fields during tuning | [E] |
| T2 asset accounting | Resources panel with the island loaded (GPU bytes per asset) | [E] |
| T3 file-drop events | Dropping the creature GLB + tileset PNG from Explorer | [E] |
| T4 Content Browser two-pane | Navigating `scenes/ stories/ flows/ models/` | [E] |
| T5 search | Finding `Beacon*` assets | [E] |
| T6 rename + retarget | Renaming `Hound_A.cmat` → scene re-resolves | [E] |
| T7 preview + metadata panel | Character GLB bounds/counts before placing | [E] |
| T8 import button | The GLB import path | [E] |
| T9 Inspector search | Filtering the 40-field Environment to `fog` | [E] |
| T10 tooltips (ⓘ) | NavMesh recipe fields while tuning the canyon bake | [E] |
| T11 asset slots | Assigning the tileset into the Tilemap slot via picker | [E] |
| T12 component QoL (enable/copy/paste) | Copying tuned particle settings campfire→beacons; disabling a light | [E] |
| T13 per-entity Active | Beacon flames start `Active=false`; lighting one flips the subtree live (also the runtime gate the game leans on) | [P] |
| T14 Hierarchy icons/columns | The island tree readable at a glance | [E] |
| T15 live-Play Inspector | Tuning hound `ChaseRadius` DURING Play | [E] |
| T16 Console v2 | Filtering `[Quest]` logs during a Play session | [E] |
| T17 profiler port | **The ≥60 fps proof for every scene** (doc 27 §0 bar) — recorded chip + panel | [E] |
| T18 jobs/resources dock | Voxel meshing jobs visible while digging in editor Play | [E] |

### Phase 24 — Animation editors & materials (M1–M6)

| Cap | Moment | Venue |
| --- | --- | --- |
| M1 asset-editor host | Character clip inspection in a tabbed document | [E] |
| M2 Timeline widget | Scrubbing the walk clip | [E] |
| M3 Animation Editor | Skeleton tree + bone overlay on the player character; picking the hand joint to name the lantern socket | [E] |
| M4 joint sockets | The lantern socketed to `hand.l`, tracking through walk/run; the dig tool socketed while held | [P] |
| M5 material slots | The character/creature multi-material meshes (body + trim slots swapped in Inspector Materials list) | [E]+[P] |
| M6 Animator crossfade | idle↔walk↔run↔jump blends from `PlayerController` (`Animator().CrossfadeTo`) — pop-free in the packaged game | [P] |

### Phase 25 — Graphs & story (Q1–Q6)

| Cap | Moment | Venue |
| --- | --- | --- |
| Q1 FlowEditor document | `Main.cflow` open as a document; two flows side-by-side b-roll | [E] |
| Q2 flow variables | `MetHermit`/`BeaconsLit`/`TentWins` — guards on hermit options + the finale transition; HUD reads the counter | [P] |
| Q3 StoryGraph runtime | Hermit dialogues run zero-code via the stock binding pattern | [P] |
| Q4 Story Editor | Authoring `HermitIntro.cstory` (portraits, guarded options, Once flags, in-panel Play preview) | [E] |
| Q5 vignette | Finale swell + the tent game's permanent soft vignette | [P] |
| Q6 post-chain view | Tuning the finale bloom/vignette through the graph view (undo parity with the panel) | [E] |

### Phase 26 — Navigation & AI (N1–N5)

| Cap | Moment | Venue |
| --- | --- | --- |
| N1 Recast/Detour vendored | Everything below | [P] |
| N2 collision-sourced bake + `.cnav` | Canyon+camp navmesh baked from the island colliders (FromChildren), `.cnav` sidecar ships | [P] |
| N3 editor authoring + overlay | Regenerate-now + translucent nav-poly overlay over the canyon (walk) | [E] |
| N4 agents + crowd + `Nav()` | Hounds patrol/investigate/chase without interpenetrating; hermit wanders camp; `nav.arrived` drives waypoint advance | [P] |
| N5 nav sample | Superseded-by: the canyon IS the recorded nav demo | [P] |

### Phase 27 — World rendering & 2D parity (X1–X7)

| Cap | Moment | Venue |
| --- | --- | --- |
| X1 physical sky | The day/night cycle: dawn beach → noon ruin → dusk lantern → night finale (4 screenshot times, doc 26 acceptance style) | [P] |
| X2 env polish | Sun elevation/azimuth widget drives the cycle in authoring; `AmbientIntensity`/`Gamma`/`SunAngularSize` tuned for the island look; Project Settings nav | [E]+[P] |
| X3 particle curl noise | Campfire smoke + beacon flames + finale embers curl (off = laminar A/B in the walk) | [P] |
| X4 noise preview + bounds | Curl preview thumbnail while tuning; fireflies bounded to the lake clearing (`BoundsExtents` wrap) | [E]+[P] |
| X5 2D lights | The tent game: darkness (`Ambient2D` ~0.12) + warm lantern radius + cool ember glows | [P] |
| X6 world-anchored UI | Hermit nameplate; beacon/tool interaction prompts pinned to world points (hide behind camera) | [P] |
| X7 render-to-texture | The HUD minimap: ortho top-down RTT into a corner `UiImage.RuntimeTexture` | [P] |

## 6. Trailer script (Z7 §trailer)

~90 s, captured at 1080p60 from the packaged build (telemetry-recorded run where useful — the
E21/J9 pattern), plus a 60–90 s authoring segment for the [E] matrix rows.

1. **0:00** Black → surf audio → dawn beach fade-in, walk toward camera-forward island (X1 sky, water).
2. **0:08** Camp: fire curl-smoke close-up → hermit nameplate → dialogue panel choice click (Q3).
3. **0:18** Ruin: column shove → crash → dust → climb → Beacon 1 ignites (J-series, bloom pop). HUD counter ticks 1/3.
4. **0:30** Canyon: hound patrol → spotted → chase sprint (M6 blends) → Beacon 2. Minimap visibly tracking (X7).
5. **0:42** Dig site: dig burst-cut (V4), block-bridge, vault reveal → Beacon 3 at dusk.
6. **0:52** Lantern self-lights (M4 socket glow) → fireflies over the lake (X3/X4) → night sky stars (X1).
7. **1:00** Summit: Great Forge ignition — ember column, vignette swell (Q5), finale story card.
8. **1:10** Smash-cut: the tent game — darkness, lantern radius, embers collected (X5, U4). Win sting.
9. **1:18** Pull back through the flow: pause overlay → title (U5). Logo + "Built with Starforge" + credits.
10. **Authoring segment** (separate chapter): homescreen tile → island in the editor → the [E] rows in §5 walked in order (chrome→viewport→browser→inspector→anim editor→flow/story editors→nav overlay→profiler ≥60 fps chip).

## 7. Engineering constraints (learned in the Phase 17–27 pre-flight review, 2026-07-14)

These are load-bearing for Z2–Z7; violating them means re-work.

1. **Pause must be its own pushed scene.** A scene-less overlay push keeps the under-scene
   ticking (PlayerLayer only rebinds when the flow's top scene CHANGES). `Pause.cscene` as a
   pushed state fully suspends the island (scripts destroyed, physics stopped).
2. **Push/pop re-instantiates scripts.** The scene OBJECT survives in the flow stack (component
   state persists — transforms, env, voxel edits), but scripts re-run `OnCreate/OnStart` on pop.
   ⇒ **Project rule: scripts hold no durable state.** Quest state lives in flow variables;
   world state lives in components. Every `OnStart` must be idempotent.
3. **Story graphs can't set variables from nodes** (nodes emit signals only). Durable dialogue
   facts are written by the listener script (`HermitDialogue` maps story signals → `Flow().SetVar`)
   or by gameplay code (beacons call `Flow().SetVar("BeaconsLit", …)`).
4. **Minimap pattern (X7):** a project script owns a small `FrameBuffer` + its own
   `SceneRenderer` instance (the A4 multi-instance precedent), renders an ortho top-down desc
   with shadows/SSAO/bloom disabled at 256², every 2nd frame, and sets
   `UiImageComponent::RuntimeTexture`.
5. **Cursor capture is script-owned.** `capture_cursor=true` in the manifest would capture on
   the Title screen (mouse menus). Instead `PlayerController` asserts capture per-frame during
   gameplay and releases on destroy; Esc's flow push destroys it ⇒ pause menu gets the cursor.
6. **Animators tick at render rate; movement at fixed rate** (J4 contract; the 07-14 fix wired
   `UpdateAnimators` into both Play paths — pause freezes the pose by design).
7. **Timer transitions** (`timer:N`), `key:<Name>`, and button signals are the whole flow
   vocabulary — Credits auto-returns via `timer:8`.
8. **≥60 fps budget:** terrain 513² res; ocean+lake grids ≤257; particle systems ≤4k alive
   total; voxel volume bounded (≤ 8×2×8 chunks); navmesh solo-bake over canyon+camp only
   (FromChildren); minimap RTT 256² at half rate; 2D lights half-res (engine-fixed). Measured
   per scene with the T17 profiler for the Z7 ledger.

## 8. Z-scope map (what lands when)

| Z | Ships | This doc's sections |
| --- | --- | --- |
| Z1 ✅ | This doc; `Projects/ForgeIsle` scaffold; greybox island (terrain/water/blockouts/beacon markers), Title/Pause/HUD canvases, `Main.cflow` spine, `PlayerController` v0 (capsule walker + mouse-look), walkable B1→B6 route in the game view | §2, §3, §5 contract |
| Z2 | Rigged character (+asset #1), M6 crossfades, camera follow, interact raycast + X6 prompts, socketed lantern | B1–B6 traversal |
| Z3 | Day/night `DayCycleSystem`, dressed camp/beacons (X3 particles), fireflies, lake, ambience audio (#3), dig-site voxel volume | B2/B5/B6 |
| Z4 | Canyon navmesh bake, hounds (+asset #2) + `HoundSystem`, wandering hermit | B4 |
| Z5 | Stories (+`HermitDialogue` binding), HUD counter/prompts wired to flow vars, minimap, finale + credits | B2/B6 |
| Z6 | The tent game (+asset #4 or generated tileset) | B7 |
| Z7 | icon (+#5), Project Settings, Packager (zip+installer), trailer + matrix walk, clean-machine run, doc 16 U8 note, D40 row | §6 |

---

*Review gate (Z1 acceptance): this document is presented to the user before any Z2+ work.*
