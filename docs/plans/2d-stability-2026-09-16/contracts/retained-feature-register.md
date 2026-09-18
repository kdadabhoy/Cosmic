# Cosmic 2D — retained-feature register

Status: WO-00 decision record, 2026-09-16. Gate G0.
Revalidated at HEAD `72b47771c869666f3a645a47bfbfae917d3167f2`.

This is the finite register of features the stable 2D trunk **retains** and must keep working. It
replaces the original brief's "no functionality or bug fix may be lost" with an evidence-backed
list: for each item, current behaviour, the acceptance ID(s) that cover it, the existing test(s)
if any, and a class tag. A demonstrated defect is **not** retained as compatibility (see KI-1).

Class tags:
- **verify** — behaviour exists and has meaningful coverage; the WO re-runs/extends it as a check.
- **needs-testability** — behaviour exists but the current suite cannot reach the important path
  (needs a seam, a golden, a host-dispatch harness, or a GPU/UI tier) before it can be gated.
- **needs-fix** — a known or strongly-suspected defect the WO must fix with a failing-before /
  passing-after regression.
- **known-limitation** — an intentional restriction of scope; preserved and documented, *not* a bug.

Acceptance IDs and tiers are defined in [`../03-Acceptance-Test-Catalog.md`](../03-Acceptance-Test-Catalog.md);
owning work orders in [`../02-Stability-Work-Orders.md`](../02-Stability-Work-Orders.md).

## Part A — the original brief's M1–M12

| # | Feature | Current behaviour (source-observed) | Acceptance ID(s) | Existing test(s) | Class |
| --- | --- | --- | --- | --- | --- |
| M1 | Plugin load / hot-reload | Two distinct lifecycles: `Application` runtime plugin (adopts ImGui/ImPlot contexts before create) and Starforge `GameModule` (`CosmicModule_Register` + separate unload; `ReloadModule` preserves a serialized edit-scene, clears selection/undo, stops Play). No single hot-reload concept. | B05, L01–L05, P01 | `test_scripthost.cpp`, `test_template_scripts.cpp` (scripts only — no plugin/module teardown test exists) | needs-testability |
| M2 | Primitives + text | Renderer2D quads/lines/rects, SDF discs/rings/ellipse, SDF text with font fallback; documented per-flush primitive grouping. | R01–R02, C01 | `test_primitives.cpp`; `tests/render/render_2d.cpp` goldens | verify |
| M3 | Instanced 2D | 2D instancing path exists; batch limit 10,000, 20,000-instance chunking. **No 2D-instancing golden** — `instancing.png` belongs to the 3D path. | R03, R07 | `tests/render/render_2d.cpp` (no 2D-instancing golden) | needs-testability |
| M4 | ImPlot | Time-series/XY/scatter/band/legend/log-axis plotting under adopted ImGui/ImPlot contexts. | P01, L02–L05, X01 | none headless (GPU/UI tier only) | needs-testability |
| M5 | Serial + telemetry | `SerialPort` async `BeginOpen` + `Close` joins the connection worker; `SerialLink` auto-reconnect + one-shot `ConsumeJustConnected`; `TelemHub` `IngestChunk` parse seam; COBS-framed binary-safe write. Connected-state path unreachable headlessly. Reported (un-reproduced) crashes: close-after-open, link-loss-while-open. | T01–T06, D05, S01 | `test_serial_lifecycle.cpp` (unreachable-port only), `test_sftelem_hub.cpp` (UI-only gaps), `test_sftelem_protocol.cpp`, `test_telemetry_robustness.cpp`, `test_telemetry_roundtrip.cpp`, `test_framing.cpp`, `test_sockets.cpp` | needs-testability (see **KI-2**) + needs-fix (reported COM crash, owner WO-05a) |
| M6 | Replay | `DataPlayer` load/seek/interpolate; v1 recording compatibility; truncation/corrupt-count guards (from `451b926`). Seek is stored-sample interpolation, **not** reverse integration. | D01–D05, X01, S03 | `test_telemetry_roundtrip.cpp`, `test_telemetry_robustness.cpp` | verify + needs-testability (D02 seek/interp matrix, D05 durability) |
| M7 | CSV | `DataExport` writes `double` columns at `max_digits10`; loader (`LookupTable`) is a **restricted numeric** reader that rejects ragged / non-numeric rows. Not a general quoted-CSV parser. | D06, X01 | `test_lookuptable.cpp` (asserts ragged-row rejection) | verify + known-limitation (restricted grammar; see [`contracts.md`](contracts.md)) |
| M8 | Camera / multi-pass / RTT | 2D camera pan/zoom/anchor; nested `RenderPass`; render-to-texture + `UiImage`. | R04–R06 | `test_camera2d.cpp`; render suite | verify + needs-testability (R05 RTT/multipass restoration, R06 GPU readback) |
| M9 | Animation / timeline | Sprite + skeletal clips, crossfade, `TimelineState`; **but** the wall-clock scheduler lives in `Application` dispatch, which `TimelineState` unit tests do not exercise. | C01, N01–N02, X01 | `test_animation.cpp`, `test_sprite_animation.cpp`, `test_crossfade.cpp`, `test_timeline_state.cpp` | verify + needs-testability (N01 host dispatch) |
| M10 | Still capture | CPU PNG encode/decode is headless; GPU framebuffer capture needs a valid GL context (hidden-window runner is supported scope). | R06, K03 | none dedicated (GPU tier) | needs-testability |
| M11 | Sim / math | RK4/semi-implicit integrators, filters, PCG RNG, noise — deterministic, same-build/seed scope. | N03–N04, X01 | `test_integrators.cpp`, `test_filters.cpp`, `test_random.cpp`, `test_noise.cpp` | verify |
| M12 | Builds / boots / authors | Config/preset load, scene serialize incl. **unknown/3D-block preservation** on 2D load/save, exe-resource + branding load. Packaging/CI not yet 2D-enforced. | B01–B05, C05, K01–K04, DOC01–DOC03 | `test_config.cpp`, `test_presets.cpp`, `test_scene_serializer.cpp`, `test_crossbuild_scene.cpp`, `test_exe_resources.cpp`, `test_branding.cpp` | verify + needs-testability (K/DOC packaging+docs) + needs-fix (2D enforcement; see **KI-3**) |

## Part B — shared surface the brief's acceptance table omitted

"2D-only" retains a large shared surface. These must not be dropped when the 3D subsystems are
fenced out.

| Feature | Current behaviour | Acceptance ID(s) | Existing test(s) | Class |
| --- | --- | --- | --- | --- |
| Sprites / sprite animation | Sorted sprite draw (ascending ZOrder, key, entt handle — finite keys first, any NaN/inf key LAST since WO-09 / KI-40), flips, active/disabled hierarchy (4,096-node activity guard); sprite-sheet animation loop/end/large-delta (evaluated in double since KI-41: a one-shot never restarts at 0). | C01 | `test_sprite_order.cpp`, `test_sprite_animation.cpp`, `test_animation.cpp`, **WO-09:** `test_wo09_c01_sprites.cpp`, `render/render_wo09_sprites.cpp` | **verified (WO-09)** |
| Tilemaps | Bounds/culling (the walk is conservative to whole cells: `[floor(min), ceil(max)]`); max valid map 1,048,576 cells, 1,025 clamped; a short `Cells` buffer never reads out of bounds; ids past the atlas wrap; batch rollover at 10,000 quads. | C02 | `test_tilemap.cpp`, `test_tilemap_extra.cpp`, **WO-09:** `test_wo09_c02_tilemap.cpp`, `wo09_tilemap_oracle.h`, `render/render_wo09_tilemap.cpp` | **verified (WO-09)** |
| 2D lighting | `Light2D` half-res composite (`BlendMode::Multiply`), multiple lights, radius/offset; zero/negative radius, zero intensity, black colour and off-screen lights contribute nothing; odd targets (1x1 / 3x3 / 1919x1079) composite through `(w+1)/2`; the X5 A/B byte-identity holds on every size. **Proposed light ceiling: 100 per frame at 1080p** (measured 2.0 ms for 100 and 2.1 ms for 1,000 on the reference GPU — see `numeric-bar-policy.md`). | C03 | `test_light2d.cpp`, **WO-09:** `render/render_wo09_ui_lights.cpp` | **verified (WO-09)** + known-limitation (ceiling = 100, proposed; Kaden ratifies) |
| UI layout / input | Rect layout + hit-test, anchors, canvas scale; inverted / zero / NaN rects are never hit; 2,001 controls exact; **depth ceiling ratified at 4,096 nodes** (`Scene::kMaxHierarchyDepth`, KI-42 — every hierarchy walker is iterative or guarded, cycles terminate). The editor snap-chip abort was KI-1 (fixed, WO-07). | C03, P01, L05 | `test_ui_rects.cpp`, `test_ui_anchor.cpp`, **WO-09:** `test_wo09_c03_ui.cpp`, `render/render_wo09_ui_lights.cpp` | **verified (WO-09)** |
| Scenes / prefabs | Round-trip serialize; unknown-component AND 3D blocks preserved verbatim across 2D load/edit/save/load cycles and the prefab path; hierarchy; malformed links (cycles, self-child, a child claimed twice) refused at load; duplicate ids get a fresh id (KI-45); documents nesting deeper than 512 levels rejected (KI-44); the real-project create/import/save/reopen/play/stop/undo/redo/delete sequence through the editor's own commands. | C05 | `test_scene_serializer.cpp`, `test_scene_components.cpp`, `test_scenemanager.cpp`, `test_hierarchy.cpp`, `test_crossbuild_scene.cpp` (extended), `test_scene2d_determinism.cpp`, **WO-09:** `test_wo09_c05_json.cpp`, `Projects/Starforge/src/C05ProjectLifecycleSelfTest.cpp` | **verified (WO-09)** |
| VFS / assets | Filesystem mounts, asset library enumerate/copy/GPU-bytes, file-watch reload; a missing texture is a degraded (0x0) cached object, `Reload` evicts it, `Clear` releases everything; TOML config: a malformed table header is a rejected parse, never a toml++ assertion (KI-49). | C05, C06 | `test_filesystem_mounts.cpp`, `test_assetlibrary.cpp`, `test_filewatcher.cpp`, **WO-09:** `test_wo09_c06_services.cpp` | **verified (WO-09)** |
| Scripts | Script host + template scripts; component/script ownership; `LiveCount` exact across Instantiate / Destroy / re-Instantiate and an entity destroyed mid-play (KI-46: the instance is released and `OnDestroy` runs from the registry hook). | C06 | `test_scripthost.cpp`, `test_template_scripts.cpp`, **WO-09:** `test_wo09_c06_services.cpp` | **verified (WO-09)** |
| Graphs (Flow / Story) | Flow machine + Story graph: cycles bounded by the 100,000-iteration cascade guard (push cycles grow the stack to 100,002 — pinned), dangling targets, missing entry, malformed / mistyped JSON is a failed load (KI-43), a double-emit self-loop drains in milliseconds (KI-48). Fuzzed: 2,000 seeded cases per parser + committed F-CORRUPT fixtures. | C04 | `test_flowmachine.cpp`, `test_story.cpp`, **WO-09:** `test_wo09_c04_graphs.cpp`, `tests/fixtures/wo09/corrupt/` | **verified (WO-09)** |
| Events (EventBus) | Emit / listener add-remove during emit / nested emit / clear during emit; exact counts and unique handles at 10,000 listeners. | C04 | `test_events.cpp`, `test_physics_events.cpp`, **WO-09:** `test_wo09_c04_graphs.cpp` | **verified (WO-09)** |
| Jobs | JobSystem with stats; 1,000-job completion; the drain-before-unload owner rule (KI-33, WO-07 L04 mode 2); Shutdown → Initialize re-arms the pool (KI-47). | L04, C06 | `test_wo07_l04.cpp` (runner), **WO-09:** `test_wo09_c06_services.cpp` | **verified (WO-09)** |
| Audio | Audio lifecycle: init / one-shot / loop / pitch / volume / group pause / Stop / StopAll / Shutdown with voices live / re-Init, on a real device (runner case `C06-AUDIO`, `audio-device` capability — ENVIRONMENT_BLOCKED without one); teardown under project/module unload = WO-07 L04. | C06, L04 | `test_audio.cpp`, **WO-09:** `test_wo09_c06_services.cpp` (the `skip(true)` audible case) | **verified (WO-09, reference machine)** |
| Shared Jolt physics | Shared 2D physics: backend seam, determinism (`JPH_CROSS_PLATFORM_DETERMINISTIC`), scene/world/events. Explicitly retained (B04 carve-out). Kept green through the WO-09 retained run. | C06 | `test_physics_2d.cpp`, `test_physics_backend.cpp`, `test_physics_determinism.cpp`, `test_physics_scene.cpp`, `test_physics_world.cpp`, `test_physics_events.cpp` | verify (retained, green) |

Note: `test_physics_terrain.cpp`, `test_physics_character.cpp`, `test_frustum.cpp`,
`test_flycamera.cpp`, `test_voxel*.cpp`, `test_nav_*.cpp`, `test_meshimport.cpp`,
`test_components3d_registry.cpp`, `test_material_slots.cpp` exercise **designated 3D** subsystems.
They are not part of the retained 2D surface; whether they compile/run under `COSMIC_2D_ONLY=ON` is
a WO-03 fencing question, not a retained-feature obligation.

## Coverage roll-up

- **verify:** M2, M11 + shared Jolt physics (partial: M6, M7, M8, M9).
- **verified by WO-09 (2026-09-18):** sprites, tilemaps, 2D lighting, UI, scenes/prefabs, VFS/assets,
  scripts, graphs, events, jobs, audio — each with boundary + lifetime coverage (C01–C06), ten
  defects fixed with regressions (KI-40..KI-49).
- **needs-testability:** M1, M3, M4, M10, plus the untested halves of M5/M6/M8/M9/M12.
- **needs-fix:** M5 (reported COM crash → WO-05a/WO-05), UI snap-chip abort → **KI-1**/WO-07, 2D
  enforcement → **KI-3**/WO-03.
- **known-limitation:** M7 restricted CSV grammar; 2D-lighting ceiling (proposed 100 lights per
  frame at 1080p, WO-09 — see `numeric-bar-policy.md`); hierarchy depth ceiling 4,096 nodes
  (ratified, WO-09).

Every row maps to at least one acceptance ID with an owning WO, satisfying the WO-00 DoD that no
required feature lacks an oracle and an owner. Missing hardware/fixtures may keep a *test pending*
but cannot remove the requirement.
