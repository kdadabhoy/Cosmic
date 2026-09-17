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
| Sprites / sprite animation | Sorted sprite draw, stable ordering on equal keys, flips, active/disabled hierarchy; sprite-sheet animation loop/end/large-delta. | C01 | `test_sprite_order.cpp`, `test_sprite_animation.cpp`, `test_animation.cpp` | verify |
| Tilemaps | Bounds/culling; max valid map 1,048,576 cells; atlas indices; batch rollover. | C02 | `test_tilemap.cpp`, `test_tilemap_extra.cpp` | verify |
| 2D lighting | `Light2D` half-res composite (`BlendMode::Multiply`), multiple lights, radius/offset. Supported light **ceiling not yet ratified**. | C03 | `test_light2d.cpp` | verify + known-limitation (ceiling → WO-00/02) |
| UI layout / input | Rect layout + hit-test, anchors, deep hierarchy, canvas scale. The editor viewport-strip snap chip **aborts** on click (Debug) / corrupts ImGui stack (Release). | C03, P01, L05 | `test_ui_rects.cpp`, `test_ui_anchor.cpp` | verify + **needs-fix (KI-1, owner WO-07)** |
| Scenes / prefabs | Round-trip serialize; unknown-component blocks preserved across 2D load/save; hierarchy. | C05 | `test_scene_serializer.cpp`, `test_scene_components.cpp`, `test_scenemanager.cpp`, `test_hierarchy.cpp`, `test_crossbuild_scene.cpp`, `test_scene2d_determinism.cpp` | verify |
| VFS / assets | Filesystem mounts, asset library enumerate/copy/GPU-bytes, file-watch reload. | C06 | `test_filesystem_mounts.cpp`, `test_assetlibrary.cpp`, `test_filewatcher.cpp` | verify |
| Scripts | Script host + template scripts; component/script ownership. | C06 | `test_scripthost.cpp`, `test_template_scripts.cpp` | verify |
| Graphs (Flow / Story) | Flow machine + Story graph: cycles, guards, dangling targets, malformed JSON. | C04 | `test_flowmachine.cpp`, `test_story.cpp` | verify |
| Events (EventBus) | Emit / listener add-remove during emit / nested emit / clear. | C04 | `test_events.cpp`, `test_physics_events.cpp` | verify |
| Jobs | JobSystem with stats; async completion; no callback into unloaded owner. | L04, C06 | none dedicated | needs-testability |
| Audio | Audio lifecycle; teardown under project/module unload. | C06, L04 | `test_audio.cpp` | verify + needs-testability (teardown-under-unload) |
| Shared Jolt physics | Shared 2D physics: backend seam, determinism (`JPH_CROSS_PLATFORM_DETERMINISTIC`), scene/world/events. Explicitly retained (B04 carve-out). | C06 | `test_physics_2d.cpp`, `test_physics_backend.cpp`, `test_physics_determinism.cpp`, `test_physics_scene.cpp`, `test_physics_world.cpp`, `test_physics_events.cpp` | verify |

Note: `test_physics_terrain.cpp`, `test_physics_character.cpp`, `test_frustum.cpp`,
`test_flycamera.cpp`, `test_voxel*.cpp`, `test_nav_*.cpp`, `test_meshimport.cpp`,
`test_components3d_registry.cpp`, `test_material_slots.cpp` exercise **designated 3D** subsystems.
They are not part of the retained 2D surface; whether they compile/run under `COSMIC_2D_ONLY=ON` is
a WO-03 fencing question, not a retained-feature obligation.

## Coverage roll-up

- **verify:** M2, M11 + sprites, tilemaps, scenes/prefabs, VFS/assets, scripts, graphs, events,
  shared Jolt physics (partial: M6, M7, M8, M9, 2D lighting, UI, audio).
- **needs-testability:** M1, M3, M4, M10, jobs, plus the untested halves of M5/M6/M8/M9/audio/M12.
- **needs-fix:** M5 (reported COM crash → WO-05a/WO-05), UI snap-chip abort → **KI-1**/WO-07, 2D
  enforcement → **KI-3**/WO-03.
- **known-limitation:** M7 restricted CSV grammar, 2D-lighting ceiling.

Every row maps to at least one acceptance ID with an owning WO, satisfying the WO-00 DoD that no
required feature lacks an oracle and an owner. Missing hardware/fixtures may keep a *test pending*
but cannot remove the requirement.
