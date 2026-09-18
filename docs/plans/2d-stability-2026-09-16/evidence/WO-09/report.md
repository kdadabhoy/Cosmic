| KI-50 | Build gap: `Starforge.exe` (target StarforgeEditor) and `Starforge.dll` shared one `Starforge.pdb`; a parallel `-m` build sometimes fails to link the DLL (LNK1201) — struck once in this WO's final Release build | `failing-before/ki50-pdb-race/` (the LNK1201 excerpt; the Release `C05-I` case that ran without the DLL) | the rebuilt tree: both PDBs present, both configs clean | `PDB_NAME` / `COMPILE_PDB_NAME` = `StarforgeEditor` in `Runtime/CMakeLists.txt` |
# WO-09 execution report — 2026-09-18 (authored 2D content and shared services, C01–C06)

Only WO-09 was executed, directly on `main` (main-only campaign, D-WORKFLOW). All six
acceptance cases were implemented, driven through the WO-04 acceptance runner in **Debug and
Release**, and every one **PASSED** on the reference machine. Ten engine defects were found by
the new cases, each registered in the known-issue register **before** its fix with
failing-before / passing-after evidence (**KI-40..KI-49**; KI-49 — a vendored toml++ assertion —
surfaced only in the Debug runner pass, and was registered and fixed before the final pass). The two open contract ceilings were
measured: the hierarchy **depth ceiling is ratified at 4,096 nodes** and a **2D light ceiling of
100 per frame at 1080p is proposed** (numbers written into `contracts/numeric-bar-policy.md` and
`contracts/retained-feature-register.md`). Nothing is `ENVIRONMENT_BLOCKED` in the final runs:
GPU, OpenGL 4.5, Windows, an audio device and the WO-04 runner were all present. The two trunk
source audits stay red for the pre-existing KI-39 reasons only (no WO-09 file adds a violation).

## Scope and provenance

- Initial `HEAD`: `3ef69810c89318ffa044b3f18627862dfb033c3b` ("Report WO-08 renderer/camera/capture
  results and evidence"), `origin/main == main` (Kaden had pushed WO-08), `main` 0 ahead — as the
  handoff stated. No branch, worktree, push, preservation-ref move or tag change was made;
  `engine-3d` and `cosmic-pre-2d-2026-09-16` were not touched; the registered worktree
  `.claude/worktrees/epic-clarke-338e7f` was left alone.
- The untracked root plan `Cosmic - 2D Trunk Consolidation & Acceptance Plan.md` and the untracked
  `recordings/` directory were never staged, moved or overwritten.
- Commits are authored **and** committed as `kdadabhoy <kdadabhoy28@gmail.com>` with no
  `Co-Authored-By`, AI or "Generated with" trailer; only explicit WO-09 paths were staged.
- The runner recorded `dirty=True` with `commit=3ef6981` in every `results.json` because the runs
  happened on the uncommitted WO-09 tree; the commit SHA(s) are given at the end.

## Toolchain and environment

- CMake `C:\Program Files\Microsoft Visual Studio\18\Community\…\CMake\bin\cmake.exe`, VS18 2026
  x64, configured `-DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=ON -DCOSMIC_BUILD_RENDER_TESTS=ON`
  (`configure.log`; cache pins `COSMIC_2D_ONLY:BOOL=ON`). Reconfigured after adding the new
  `.cpp` files (the two test lists are explicit; the Starforge GLOB has `CONFIGURE_DEPENDS`).
- Full builds of every target: Release and Debug both **0 warnings / 0 errors**
  (`build-release.log`/`.exit`, `build-debug.log`/`.exit`).
- Acceptance runner: Windows PowerShell 5.1, absolute `-OutDir` under this directory,
  repository-local `-TempRoot C:\dev\Cosmic\build\wo09-accept-temp`, `-KeepArtifacts`,
  repository-local child `TEMP`/`TMP`, and `-GoldenDir C:\dev\Cosmic\tests\render\goldens` so the
  runner's own before/after SHA-256 covers the real goldens (18 entries: 17 PNGs + `.gitignore`).
- Reference machine (from `results.json`): **DESKTOP-SEOA4BT**, Windows 11 Education 26200.9457,
  AMD Ryzen 7 7800X3D (SSE4.2), 31.2 GiB, **NVIDIA GeForce RTX 5070 Ti** driver 32.0.16.1692,
  OpenGL 4.5 (the harness logs "OpenGL 4.5 — NVIDIA GeForce RTX 5070 Ti/PCIe/SSE2").

## Baseline facts recorded (and respected)

- Sprites: `Scene::BuildSpriteDrawList` (public, pure) is the draw-list oracle; the sprite pass draws
  with depth test ON / depth WRITE OFF / alpha — every GPU overlap assertion here renders through
  `Wo08::BeginFrame`, which sets exactly that state.
- Tilemaps: `TilemapComponent::kMaxGrid = 1024` → the maximum map is 1,048,576 cells against a
  10,000-quad batch (31 non-white textures per batch). `EnsureCells` clamps to 1..1024, so **1,025 is
  clamped, never a 1,025-wide map**; `At()` guards a short `Cells` buffer.
- UI/lights: `UiSystem::Render` restores depth ON/ON + alpha on exit (documented). `Light2DRenderer`
  uses a half-res `(w+1)/2 × (h+1)/2` RGBA16F buffer; the X5 A/B invariant is kept (`light2d A/B`
  golden case + the odd-size A/B checks in C03-G).
- Flow/Story/EventBus: `EventBus::Emit` re-checks liveness per listener; `FlowMachine::OnUpdate` has a
  100,000-iteration cascade guard.
- Scene/prefab JSON: `LoadEntityComponents` preserves unknown blocks verbatim and carries
  `PendingFields`; hierarchy links go through `Scene::SetParent` (refuses cycles);
  `SaveToString` = `dump(2)`.
- EnTT: `ENTT_PACKED_PAGE = 1024`, `ENTT_SPARSE_PAGE = 4096` (asserted by C06-U).

## What was built

**Headless tests (`CosmicTests`, tier U, explicit list in `tests/CMakeLists.txt`):**
`tests/test_wo09_c01_sprites.cpp` (C01), `test_wo09_c02_tilemap.cpp` (C02),
`test_wo09_c03_ui.cpp` (C03 incl. the depth ladder that runs this exe as a child per rung),
`test_wo09_c04_graphs.cpp` (C04 + the Flow/Story fuzz), `test_wo09_c05_json.cpp` (C05 + the
scene/prefab/material/config fuzz), `test_wo09_c06_services.cpp` (C06; the audible audio case is
`skip(true)`), and the WO-09 extension of `test_crossbuild_scene.cpp` (the 2D preservation
obligation across two edit cycles + the prefab path). Shared: `tests/wo09_fuzz.h` (seeded byte +
structural mutator with a per-parse deadline and a "current input" evidence file) and
`tests/wo09_tilemap_oracle.h` (brute-force visible-cell counts from the camera's own rectangle).

**GPU tests (`CosmicRenderTests`, tier G):** `tests/render/render_wo09_sprites.cpp` (C01-G),
`render_wo09_tilemap.cpp` (C02-G), `render_wo09_ui_lights.cpp` (C03-G). **No new golden** — every
assertion is a pixel sentinel, a byte-exact in-process A/B or an armed `Renderer2D` counter.

**Editor host (tier I):** `Projects/Starforge/src/C05ProjectLifecycleSelfTest.cpp`, armed by
`COSMIC_C05_SELFTEST` (the WO-07 L02 pattern), hooked at `OnAttach`/`OnUpdate`/`OnDetach` of
`StarforgeApp`.

**Fixtures:** `tests/fixtures/wo09/content/` (F-CONTENT: `scenes/Main.cscene` written by the
serializer with one of every 2D content type + an unknown block + a hierarchy;
`prefabs/Imported.cprefab` with a 3D `MeshRenderer`, a 3D `DirectionalLight`, an unknown
`FutureThing` and a child) and `tests/fixtures/wo09/corrupt/` (F-CORRUPT: 14 files — the minimized
fuzz culprits `fuzz-flow-0x0904F10A-0.cflow` / `fuzz-config-0x090570A1-539.toml`, typed/root/truncated/deep-nesting/unterminated/header
variants for every parser; see its README).

**Runner:** `tests/acceptance/fixtures/Run-WO09Case.ps1` (any doctest selection of either exe as a
deadline-bounded child; the H03 golden gate; `COSMIC_WO09_EVIDENCE_DIR`; `-Env`; refuses commas in
filters — doctest splits on them), `Run-WO09Lifecycle.ps1` (the editor host + the out-of-process
scene oracle), manifests `wo09-units`, `wo09-gpu`, `wo09-editor`, `wo09-retained`. The runner's
capability probe (`AcceptanceRunner.psm1`) gained `audio-device` (Win32_SoundDevice) so the C06
audible case is honestly `ENVIRONMENT_BLOCKED` on a machine without one.

**Engine — defect fixes (see the register):** `Scene.cpp` (KI-40 comparator; KI-42 iterative
`DestroyEntity` / `WorldOf` / bounded `IsAncestor`), `Scene.h` (`kMaxHierarchyDepth`),
`Components.h` (KI-41 `SelectFrame`), `ui/UiSystem.cpp` (KI-42 iterative `VisitUi`),
`SceneSerializer.{h,cpp}` (KI-42 `GatherSubtree`; KI-44 `kMaxJsonNestingDepth` +
`JsonNestingDepth`; KI-45 duplicate ids), `FlowMachine.{h,cpp}` (KI-43 try scope + tolerant `pos`;
KI-48 deque), `StoryGraph.cpp` (KI-43), `scripting/ScriptHost.{h,cpp}` (KI-46 destroy hook),
`jobs/JobSystem.cpp` (KI-47), `utils/Config.cpp` (KI-49 table-header gate; `Load` now reads the
text itself so the gate runs before toml++ does). Build: `Runtime/CMakeLists.txt` (KI-50 — the
launcher's PDB is `StarforgeEditor.pdb`, no longer colliding with the project DLL's).

**Docs:** `docs/reference/ecs.md` (walker ceilings, the ordering policy, `SelectFrame`),
`docs/guide/scenes-and-serialization.md` (bounded parsing rules), `docs/guide/flow-and-story.md`
(schema errors), `docs/guide/scripting.md` (mid-play destroy rule), the two contracts and the
register.

## Final-pass method

The last engine change (KI-49's `Config.cpp` gate) was followed by a full rebuild of every target
in both configurations and a complete run of all four manifests in Release and Debug. The Release
build of that pass struck the KI-50 PDB race once (`Starforge.dll` not linked → the Release
`C05-I` case ran without an editor DLL and failed; every other case passed on the rebuilt
`Cosmic.dll` / `CosmicTests.exe` / `CosmicRenderTests.exe`). After the one-line KI-50 fix the tree
was reconfigured and both configurations rebuilt clean (0 warnings). SHA-256 of the binaries before
and after that rebuild: `Cosmic.dll` and `CosmicRenderTests.exe` **byte-identical** in both
configurations (so the `wo09-gpu` results below are on the final binaries); `CosmicTests.exe`
relinked (the reconfigure regenerates `wo05_kiss.inc`, no source change) and `Starforge.exe`
relinked (its PDB name) — therefore the `wo09-units`, `wo09-editor` and `wo09-retained` manifests
were run **again** in both configurations on the final binaries; those are the numbers in the
table. Nothing is reported from a binary that differs from the final tree.

## Acceptance-case status (final pass, final binaries, through the runner)

| Case | Tier | Debug | Release | Evidence |
| --- | --- | --- | --- | --- |
| C01-U sprites (headless) | U | PASSED (8 / 4,643) | PASSED (8 / 4,643) | `c01u-<cfg>/`, `units-runner-excerpts.txt` |
| C01-G sprites (GPU) | G | PASSED (3 / 538,880) | PASSED (3 / 538,880) | `c01g-<cfg>/captures/` |
| C02-U tilemaps (headless) | U | PASSED (5 / 159) | PASSED (5 / 159) | `c02u-<cfg>/` |
| C02-G tilemaps (GPU) | G | PASSED (3 / 369,709) | PASSED (3 / 369,709) | `c02g-<cfg>/captures/` |
| C03-U UI (headless, incl. depth ladder + cycles) | U | PASSED (6 / 83) | PASSED (6 / 83) | `c03u-<cfg>/captures/c03-depth-ladder-<cfg>.txt` |
| C03-G UI + lights (GPU) | G | PASSED (3 / 697,582) | PASSED (3 / 697,582) | `c03g-<cfg>/captures/` |
| C04 Flow / Story / EventBus + fuzz | U | PASSED (8 / 1,094) | PASSED (8 / 1,094) | `c04-<cfg>/` |
| C05-U scene / prefab / material / config + fuzz | U | PASSED (7 / 125) | PASSED (7 / 125) | `c05u-<cfg>/` |
| C05-XB 2D preservation obligation | U | PASSED (3 / 44) | PASSED (3 / 44) | `c05xb-<cfg>/` |
| C05-I real-project lifecycle (editor) | I | PASSED (2 / 2; 9 steps, 0 failed checks; oracle PASS) | PASSED (2 / 2; 9 steps, 0 failed checks; oracle PASS) | `c05i-<cfg>/` (`c05-result.json`, `c05-expected-final.json`, `c05-saved-Main.cscene`) |
| C06-U shared services (headless) | U | PASSED (6 / 86) | PASSED (6 / 86) | `c06u-<cfg>/` |
| C06-AUDIO audible lifecycle | W | PASSED (1 / 18) | PASSED (1 / 18) | `c06audio-<cfg>/` |
| retained-units (400 + the crossbuild extension) | U | PASSED (401 / 23,181,090) | PASSED (401 / 23,250,930) | `retained-units-<cfg>/` |
| retained-goldens (6 cases, 17 goldens) | G | PASSED (6 / 29), byte-exact | PASSED (6 / 29), byte-exact | `retained-goldens-<cfg>/goldens-sha256-*.txt` |
| retained-wo08-gpu (R01–R06, 22 run + 2 R07 skipped) | G | PASSED (22 / 21,121,754) | PASSED (22 / 21,121,754) | `retained-wo08-<cfg>/` |

`golden_mutated=False` in every `children.json`; no `.actual`/`.diff` diagnostics were produced in
any final run; the 17 goldens' SHA-256 before and after every GPU child are identical to WO-08's
`goldens-sha256-after.txt` (`goldens-sha256-before.txt` / `-after.txt` here). Source audits:
`audit-gl-conformance.txt` (8 violations — the KI-39 WO-07 fixtures), `audit-docs-coverage.txt`
(2 unlisted headers — KI-39); no WO-09 file appears in either.

### C01 — sprites

Headless: `BuildSpriteDrawList` at **0 / 1 / 10,000 / 20,000** items (the 20,000 include 5,000
tilemaps), exact counts, order equal to an independent reference total order —
**0.87 ms** for 10,000 and **1.74 ms** for 20,000 (Release; 21.7 ms for 20,000 in Debug). Equal keys: two
builds identical; the tie-break is pinned as the **entt handle value** — after destroying and
recreating every other sprite, every recycled entity (higher version) draws after every
never-recycled one (creation order is *not* the rule). Nonfinite keys: 10,000 sprites with ~30 %
NaN / ±inf keys over 8 seeds — before the fix 9,990+ positions were off the reference and hundreds
of adjacent **finite** same-layer pairs were inverted per seed (the KI-40 poison); after it 0 and 0,
and the engine order equals the reference exactly. Zero / negative / nonfinite Scale, Rotation and
Color leave the list alone; the `WorldSize` sizing rule pinned (flips not applied there; PPU ≤ 0 or
NaN reads as 1; a NaN scale stays NaN). Active/disabled hierarchy: a 2,000-deep chain under an
inactive root is excluded, a mid-chain inactive node hides 1,000, T12 hides one; the activity guard
is pinned at **4,096 nodes incl. self (4,095 ancestors)**. Animation: `SelectFrame` equals a
double-precision definition at loop / end / negative time; **large delta** (3e8, 1e9, 1e12, 3e38 s,
+inf) — before the fix a one-shot returned frame 0 (KI-41), after it the last frame; loops stay in
range; NaN → 0; 0/1-frame clips and fps 0 / −8 / NaN → 0; `FrameUV` out-of-range frames finite;
`UpdateSpriteAnimations` under 1e30 / −1e30 / ±inf / NaN dt keeps a defined `Elapsed` and the
`SourceRect` untouched without a sheet.

GPU: a 60-sprite equal-key overlap chain draws in exactly list order, before and after slot
recycling (recycled sprites cover their odd neighbours at every overlap); a NaN-keyed sprite sorts
last and changes no pixel. Flips: a 2×2 four-colour texture at 20 px/texel — FlipX / FlipY / both
move the quadrants exactly; negative scale == FlipX; zero scale draws nothing. NaN / inf position,
scale and rotation produce **no fragments anywhere** (0 pixels of their colours in the frame) while
the two finite sentinels around them are fully painted (6,400 / 6,400 pixels each).

### C02 — tilemaps

Headless: `EnsureCells` at 0 / −1 / 1 / 1,024 / **1,025 → 1,024** / kMaxGrid+1 / INT_MIN / INT_MAX,
buffer sized exactly. The maximum map: `EnsureCells(1024×1024)` **0.23 ms**, 2,097,152-byte buffer,
1,048,576 cells; a full JSON round-trip of the map (16,044,050 bytes) **save 111 ms / load 122 ms**
Release (save 1,327 ms / load 2,157 ms in Debug), every cell equal. The short-`Cells` crash vector: `At` / `FloodFill` /
`EnsureCells` on a 64×64 grid with a 10-value buffer never read past the buffer, and `EnsureCells`
repairs it preserving the 10 values. Serializer: `Cells` shorter (padded) / longer (truncated) than
the grid, ragged values (`"x"`, null → 0; 2.9 → 2; 65536 → 0 and 65537 → 1 through the uint16 wrap;
−1 → 65535 — pinned, no rejection), `GridW` 1,025 / 0 / −7 / 1e9 clamped, a non-array `Cells`
ignored. The oracle agrees with the catalog's pinned cull geometry (121 strict / 144 enclosed for
the half-unit window; 0 outside; origin offsets).

GPU (all counts vs the oracle): the 1,048,576-cell map built in **2.4 ms**; seven views —
10×5.6 cells: 180 quads / 1 draw; 200×112: 54,237 / 6; a corner: 121 / 1; **whole map + margin:
786,432 / 79**; fully outside: 0 / 0; just outside the right edge: 0 / 0; **extreme zoom-out
(±100,000 units): 786,432 / 79 in 38 ms** — every non-empty cell exactly once, in ceil(n/10,000)
draws, never a million draws. The partial-cover camera edge: 653 strict / 707 enclosed / 740 drawn
— the top edge minus the origin is exactly 20.0 and the inverse-projection's last bit pushed
`ceil()` one row up, so the count is bracketed by the exact and the 1e-3-widened oracle (740 =
the widened count); every one of the 500+ fully visible cell centres shows its atlas tile colour
and the margins are clear. Tile ids 17 / 255 / 65535 on a 16-tile atlas draw (wrapped) without a
crash; `Columns = 0` derives 4 and is pixel-identical to `Columns = 4`; a null texture, `TileW = 0`
and `TileH = −16` skip the map (0 quads); the 10,000-quad seam: a 101×100 map = 10,100 quads in
**2 draws** with cells 0 / 9,999 / 10,000 / 10,099 all painted.

### C03 — UI and lights (the two ceilings)

Headless: inverted / zero / NaN / inf rectangles resolve, are never hit (an inverted button under
the pointer does not arm) and do not poison a sibling; 2,001 controls: exact element count, the
topmost hit (a ZOrder-100 modal over the grid), exactly one emit per click and no stray signal,
`CollectElements` **0.25 ms** / `Update` 0.24 ms per frame (Release; 4.7 / 5.0 ms in Debug);
canvas-scale extremes (`ReferenceHeight` 0 / −5 / 1e-30 / 1e30 / NaN / inf; 0×0 / 1×1 / inverted
viewports) all finite and pinned.

**Depth ladder** (children of `CosmicTests.exe` per rung, all four walkers — UI collect, world
transform, prefab gather, subtree destroy): before WO-09, Release overflowed the stack at 2,750
(`CollectElements`), ~3,000–4,096 (`GetWorldTransform`), 4,096–8,192 (`DestroyEntity`) and
8,192–16,384 (`SavePrefab`); Debug at 4,096 (`failing-before/c03u-Debug/`). After WO-09 every
rung **64 / 256 / 1,024 / 4,095 / 4,096 / 4,097 / 8,192 survives** in both configurations with the
documented truncation past the ceiling (4,095 laid-out UI nodes, a 4,096-node transform chain,
4,096 prefab entities, 4,096 destroyed + the rest orphaned). Hierarchy **cycles** (A↔B through
`Children` + `Parent`, a self-child) terminate in every walker in 2.6 ms — before the fix
`CollectElements` crashed and `IsAncestor` hung. → **Ratified: `Scene::kMaxHierarchyDepth` =
4,096 nodes per path** (the existing `IsActiveInHierarchy` guard, now shared by every walker).

GPU: 2,001 controls render as **exactly 2,001 quads in 1 draw** (1.1 ms) with index-coded
sentinels at the corners and the modal; a 4,200-deep canvas chain renders **4,095 quads** with the
deepest laid-out node's colour at the centre; an inverted rect is pinned as *painting its mirrored
quad* and is never hit. Lights: zero radius, zero intensity, a black colour, a fully off-screen and
a just-off-screen light are **byte-identical** to the ambient-only frame; a −60 radius equals +60
(pinned); a light centred on the left edge lights pixel (0, 90) and neither the right side nor the
pixel past its radius; 1 / 10 / 100 / 1,000 lights never darken a pixel below ambient. **Light
ceiling ladder** (5-frame mean incl. sprites + composite + `FinishGpu`, Release): 320×180 —
0.15 / 0.14 / 0.19 / 0.56 ms for 1 / 10 / 100 / 1,000 lights; **1920×1080, radius 120 — 2.15 ms for
100 and 2.33 ms for 1,000**. The pass is fill-bound in the light radius, not the count. → **Proposed:
100 lights per frame at 1080p** (a support line well under 15 % of a 60-Hz frame on the reference
GPU; correctness holds at 1,000). Odd targets **1×1 / 3×3 / 1919×1079** composite through the
half-res buffer and light the centre pixel; the X5 A/B (no lights + white ambient == no pass) is
byte-identical on every size.

### C04 — Flow / Story / EventBus

EventBus: A removing B mid-dispatch keeps B from firing (1 / 0 / 1), a listener added during a
dispatch fires only from the next one, a 50-deep nested self-emit chain delivers exactly 50, `Clear`
inside a handler stops the remaining snapshot (1 / 0), null handlers refused; 10,000 listeners —
unique handles, exactly 5,000 fired, exact removal, **5.3 ms** per 5,000-listener emit. FlowMachine:
the A↔B ping-pong is bounded by the 100,000-iteration guard (3.3 ms, deterministic across two
machines); the **push cycle grows the stack to exactly 100,002 frames** per update (pinned as the
source limit); the double-emit self-loop took **4,508 ms per update in Release and exceeded the
300-s case deadline in Debug** before the fix (KI-48) and takes **9.5 ms** after; dangling target /
missing entry / `@pop` on a bare stack / `"timer:"` (= 0 s, fires the same update) / `timer:nan` /
`timer:-1` / `timer:1e999` / an undeclared signal all leave a running, consistent machine; Stop
releases the bus subscription (0 listeners) and Start re-takes exactly one. StoryRunner: 1,000
chosen self-loops, a once-option consumed exactly once, out-of-range choices ignored, a dangling
`next` ends, a missing start ends.

**Parser fuzz** (`wo09_fuzz.h`, structural + byte mutations, 2 s per-parse deadline; the input is
written to `<label>-current.txt` before every parse so a crash leaves the culprit behind):
`FlowAsset` seed `0x0904F10A` — **2,000 cases, 373 accepted / 1,627 rejected, max 1.2 ms**;
`StoryGraph` seed `0x09045708` — **2,000 cases, 410 / 1,590, max 1.6 ms**. Before the fix the flow
fuzz **crashed on its first case** (`"push": ""` → an escaped `type_error.302`, KI-43); the
minimized culprit is committed as `corrupt/fuzz-flow-0x0904F10A-0.cflow`. All 7 committed
`.cflow`/`.cstory` corrupt fixtures are handled. Nightly volume: `COSMIC_WO09_FUZZ_CASES=50000`
(the `-Env` argument of `Run-WO09Case.ps1`).

### C05 — scene / prefab / material / config

Headless: the F-CONTENT scene (10 entities) round-trips byte-stable with exact content; **truncation
at every one of its 11,321 byte offsets** is rejected (max 0.2 ms); bad magic / root types /
missing `entities` rejected, junk entity elements bounded (one entity per element); **`1e999` is a
JSON parse error** for the whole document (pinned — no nonfinite literal exists), `1e300` loads as
`±inf` and, like a runtime NaN, saves as `null` and reloads as 0 (pinned data laundering); a 4 MiB
tag and a 4 MiB unknown block are stored whole; the **100,000-deep nesting crashed the loader
(SIGSEGV in `json::dump`, KI-44)** and is now rejected in 0.2 ms by the 512-level gate; huge counts
(`GridW = 2^31−1`, `Frames = −2^31`, `FPS = 1e300`) load bounded. Malformed links: a `Children`
cycle is refused at load, a self-child dropped, a child claimed by two parents belongs to the last
claimant, a missing child skipped, every walker terminates, the scene re-saves. **Duplicate UUIDs:
before the fix destroying the twin erased the survivor's UUID mapping (KI-45)**; after it the second
block gets a fresh id and the survivor still resolves. Material `.cmat` and TOML config: valid
round-trips, `{}` / bare object / `5` (pinned: defaults) / wrong types / not-JSON / empty; TOML
typed getters and a type-mismatch fallback. **Fuzz**: scene seed `0x0905C4E5` 2,000 cases (1,047
accepted / 953 rejected, max 2.9 ms), prefab `0x0905B4EF` 2,000 (978 / 1,022, max 22 ms), material
`0x0905CA47` 2,000 (1,061 / 939, max 1.4 ms), config `0x090570A1` 2,000 (215 / 1,785, max 0.1 ms).
**The Debug config fuzz aborted at case 539** before the fix — a NUL spliced into a `[[motors]]`
header reached toml++'s `parse_key` assertion (KI-49); a plain `[!x]` does the same, i.e. any
one-character header typo in a `.toml` killed a Debug editor (and was a violated `__assume` in
Release). `Config::Parse` / `Load` now pre-check every table header with the parser's own
`is_bare_key_character` / `is_string_delimiter` (multi-line strings and comments excluded),
pinned by two committed fixtures and eleven accept/reject cases. The 7 committed
scene/prefab/material/config corrupt fixtures and the committed F-CONTENT scene load.

Preservation (C05-XB, `test_crossbuild_scene.cpp` extended): the 3D-authored scene through **two**
2D load → edit → save → load cycles — the four opaque blocks are byte-identical between every
pass, the authored values (`crate.obj`, `CastShadows:false`, `Direction:[-0.4,-1,-0.2]`,
`Resolution:257`, `Nested:{Deep:[1,2,3]}`) reappear, the 2D edits landed, pass 2 == pass 3, and a
prefab saved from the mesh holder instantiates with a fresh UUID carrying the `MeshRenderer` block
verbatim.

**C05-I (the real project, inside Starforge, Release + Debug through `wo09-editor`)**: `NewProjectAt`
scaffolds `C05Life` (the template's `Main.cscene` opens); `Commands::Create` / `AddComponent` /
`SetField` author a sprite (Position 3,4,0; ZOrder 7; FlipX), a tilemap painted through four
`TileEdit` strokes (one erased), a canvas with a button child (`c05_play`) and a light (radius 3.5)
— 5 entities, undo recorded, scene dirty; **import**: a 4×4 PNG written to `project://textures/
c05.png` and referenced by the sprite, and the F-CONTENT prefab through `InstantiatePrefab` +
`RecordSpawn` (2 entities; the `FutureThing` and 3D `MeshRenderer` blocks opaque and verbatim, the
child linked); `SaveScene`; **reopen** (`CloseProject` → `OpenProjectPath`): the same entity count,
every entity found by UUID with every field, the opaque blocks byte-identical, the button's parent
link, no stale link, undo history cleared; `PlayScene` → 30 frames of runtime drift on the sprite →
`StopScene`: the edit scene is the untouched original (position back at 3,4,0, not dirty);
`Commands::Destroy` on the sprite → `Undo` (back with the **same UUID** and fields) → `Redo` (gone)
→ `Undo` (back); `SetField` ZOrder 99 → `Undo` (7) → `Redo` (99) → `Undo`; `Commands::Destroy` on the
tilemap, final `SaveScene`, the file reloads in 0.1 ms. Steps (Release): project 37 ms, content
0.4 ms, import 1.6 ms, save 3.4 ms, reopen 3.2 ms, play 3.2 ms, stop 0.4 ms, undo/redo 0.1 ms,
delete+save 7.1 ms; **0 failed checks**. The wrapper's out-of-process oracle (PowerShell's JSON
reader over `c05-saved-Main.cscene`): 9 entities, no duplicate id, the tilemap absent, the sprite /
lamp / button / canvas-child / imported-root fields and the verbatim `FutureThing` /
`MeshRenderer` / child `DirectionalLight` + `Light2D` blocks all match — **PASS**.

### C06 — shared services

EnTT: 1,023 / 1,024 / 1,025 / 4,095 / 4,096 / 4,097 entities with a 64-byte component — every
component reachable with its value, the address handed out at emplace is still its address, in-page
neighbours are contiguous and the pair straddling the packed page is **not** (pinned), the last 6
deleted (straddling the boundary): exact survivors, no deleted serial iterates, refill reuses the
slots to an exact total. JobSystem: 1,000 jobs complete (`GetCompletedCount` +1,000, 0 queued /
active), the **drain-before-unload** rule keeps every callback inside the owner's lifetime (0 ran
after release), a second batch exact; **Shutdown → Initialize** — before the fix a job submitted
after re-init never ran (dead workers on the stale stop flag, KI-47), after it runs. ScriptHost:
50 entities (10 unknown classes inert) → 40 live; 6 scripted entities destroyed mid-play, `Tick`
after that safe; **before the fix `Destroy()` left 6 script objects alive with `OnDestroy` never
run (KI-46)**; after it `LiveCount` 0 / 40 destroyed / 34 on re-instantiate. Assets/VFS: a missing
texture is a degraded (0×0, no GL object) **cached** object (pinned), `Reload` evicts it, `Clear`
empties the library, `GetMaterial` / `LoadMaterialAsset` on a missing file are null / false, a
`project://` mount resolves, `NormalizeKey` collapses `..`. FileWatcher: watch → change delivered →
stop → poll empty → re-arm → destroyed with a change in flight; a missing directory fails cleanly.
**Audio (C06-AUDIO, `--no-skip`, requires `audio-device`)**: init on the real device, a generated
0.25-s WAV one-shot + loops, pitch / volume / group pause, `IsPlaying` across the clip length,
`Stop` / `StopAll`, handles never reused, **Shutdown with two voices live**, control after shutdown
harmless, the `Sound` outliving the engine, re-`Init`. Physics: the retained `test_physics_2d` /
`test_physics_backend` / determinism / scene / world / events suites are green in the retained run
(nothing "fixed" there — Jolt raycasts hit triggers and normalise direction as documented).

## Defects found (all registered before the fix; failing-before → passing-after)

| KI | What | Failing-before | Passing-after | Fix |
| --- | --- | --- | --- | --- |
| **KI-40** | `BuildSpriteDrawList` comparator not a strict weak order on NaN/inf keys — finite sprites misordered | `failing-before/c01u-Release/` (9,990+ positions off, hundreds of finite-pair inversions per seed) | `c01u-<cfg>/` (0 / 0, order == reference) | finite-first total order, handle tie-break |
| **KI-41** | `SelectFrame` int overflow: a one-shot restarts at 0 past 2^31 (or +inf) | `failing-before/c01u-Release/` (`0 == 3` at 3e8..3e38, +inf) | `c01u-<cfg>/` | double math: clamp / fmod / NaN → 0 |
| **KI-42** | Recursive hierarchy walkers overflow on legal deep chains; cycles never return | `failing-before/c03u-<cfg>/` (rung 4,096 CRASHED both configs; the cycle case CRASHED) | `c03u-<cfg>/` (all rungs to 8,192; cycles 2.6 ms) | iterative / guarded walkers, `kMaxHierarchyDepth` |
| **KI-43** | Flow/Story loaders let nlohmann `type_error` escape (terminate) | `failing-before/c04-<cfg>/` (fuzz case 0 THREW; fixture case THREW) | `c04-<cfg>/` (4,000 fuzz cases handled) | whole walk in `try` → "schema error", tolerant `pos` |
| **KI-44** | 100,000-deep JSON in an unknown block overflows `json::dump` | `failing-before/c05u-<cfg>/` (SIGSEGV) | `c05u-<cfg>/` (rejected in 0.2 ms) | 512-level nesting pre-scan on every loader |
| **KI-45** | Duplicate UUIDs leave the survivor unreachable after its twin is destroyed | `failing-before/c05u-Release/` (the id no longer resolves) | `c05u-<cfg>/` | fresh id for the duplicate + warning |
| **KI-46** | ScriptHost leaks / never `OnDestroy`s a mid-play-destroyed entity's script; `LiveCount` stale | `failing-before/c06u-<cfg>/` (6 live, 34 destroyed) | `c06u-<cfg>/` (0 / 40) | `on_destroy<NativeScriptComponent>` hook |
| **KI-47** | JobSystem Initialize after Shutdown = dead pool, `WaitIdle` would hang | `failing-before/c06u-<cfg>/` (`b == 1` fails) | `c06u-<cfg>/` | reset `m_Stopping` in `Initialize` |
| **KI-48** | FlowMachine O(n²) signal queue: 4.5 s (Release) / >300 s (Debug) per update | `failing-before/c04-<cfg>/` (Release 4,508 ms; Debug timed out at 300 s) | `c04-<cfg>/` (9.5 ms) | `std::deque` |
| **KI-49** | `Config::Parse` aborts a Debug build on a TOML table header that starts with a non-key character (toml++ 3.4 `TOML_ASSERT_ASSUME` in `parse_key`, `__assume`d — UB — in Release); found by the config fuzz (seed `0x090570A1` case 539: a NUL spliced into `[[motors]]`), reproduced with a plain `[!x]` | `failing-before/ki49-config-header/` (Debug SIGABRT + assertion text, the culprit input, the Debug runner's `C05-U` CRASHED log; the Release twin passes) | `c05u-<cfg>/` (both fixtures, eleven accept/reject cases, 2,000 fuzz cases) | header gate on the parser's own predicates, multi-line-string and comment aware |

Every counterfactual was captured on the working tree before the fix (the `failing-before/`
runner JSON/JUnit + per-case logs, both configurations) and re-run after it with rebuilt binaries;
no destructive reset was used, no golden was regenerated, no tolerance or deadline was loosened.

## Retained tests (regression safety, through the runner, both configurations)

- `retained-units` — `CosmicTests --test-case-exclude="WO-09 *"`: **401 passed / 0 failed** in
  Release (23,250,930 assertions) and **401 passed / 0 failed** in Debug (23,181,090) — the 400 WO-08 left (388 pre-WO-08 + 12
  WO-08) plus the `2D engine (WO-09 C05)` extension of `test_crossbuild_scene.cpp`, which carries a
  retained obligation and is deliberately not excluded. The full unfiltered suite is now 400 + 1 + 39
  WO-09 cases = 440 (the 10 native host cases and the audio case stay skipped by default).
- `retained-goldens` — the six 2D golden cases: all 17 PNGs unchanged (SHA-256 identical to WO-08's
  after-hashes) in both configs.
- `retained-wo08-gpu` — the 22 WO-08 R01–R06 cases pass (R07's two perf cases stay `skip(true)`;
  they are a `release`-profile qualification, not a retained correctness case), goldens unchanged.

## Notes, limits and honest caveats

- The tilemap oracle is deliberately not the engine's index arithmetic: it counts, per cell,
  whether the unit square overlaps the camera's own rectangle (positive area = *strict*) or whether
  the cell index lies in the rectangle snapped outward to whole cells (= the documented walk). Where
  a camera edge minus the map origin is an exact integer, the engine's inverse-projection can differ
  by one ulp and `ceil()` one row up; C02-G brackets that one case between the exact and a
  1e-3-widened oracle and says so in its message. Every other view has fractional edges and
  matches exactly.
- The depth ladder stops at 8,192: `SetParent` runs `IsAncestor` per link, so *building* a 16k-deep
  chain is O(n²) (≈ 60 s in Debug) — a construction cost, not a walker cost, and far beyond the
  ceiling. It is recorded, not gated.
- "Cycles terminate" is asserted on data authored by hand through the public
  `RelationshipComponent`; `SetParent` and both loaders refuse cycles, so the guard is defence in
  depth for scripts/plugins, not a path the editor can reach.
- The light ceiling is a *proposal* on the named machine (measurement-derived); Kaden ratifies.
  The depth ceiling is a code constant (spec-derived) and is ratified by this WO.
- The 2D `OnDestroy` ordering changed slightly with KI-42: `DestroyEntity` still tears children
  down before their parent (the recursive order), now from an explicit list; a script's `OnDestroy`
  reached through the KI-46 hook runs while the entity handle is valid but sibling components may
  already be gone (documented in `scripting.md`).
- `NativeScriptComponent` removal (`RemoveComponent`) on a live host now also releases the instance
  through the same hook.
- The audible C06 case ran on the reference machine's real output device (master volume 0); on a
  CI runner the `audio-device` probe makes it `ENVIRONMENT_BLOCKED`, never a pass.
- Not done, by scope: WO-10 clocks/numerics, WO-11 packaging, new content features, 3D content, and
  the KI-39 audit fixes in other work orders' files (still open; recipe in the register).
- Evidence hygiene: the wrappers' scratch dirs and the junction-linked editor tree are removed per
  child; the runner's `_temp` lived under `build/`; empty `captures/` dirs were pruned; the
  gitignored `*.log` files stay uncommitted (`results.json` / `results.junit.xml` / `children.json`
  / `*-excerpts.txt` / the C05 JSON + saved scene / the depth ladders / the captures are committed).

## Files

- Engine: `Cosmic/src/scene/{Scene.h,Scene.cpp,Components.h,SceneSerializer.h,SceneSerializer.cpp,
  FlowMachine.h,FlowMachine.cpp,StoryGraph.cpp,ui/UiSystem.cpp}`, `Cosmic/src/scripting/
  {ScriptHost.h,ScriptHost.cpp}`, `Cosmic/src/jobs/JobSystem.cpp`.
- Editor host: `Projects/Starforge/src/C05ProjectLifecycleSelfTest.cpp`, `StarforgeApp.{h,cpp}`.
- Tests: `tests/{test_wo09_c01_sprites,test_wo09_c02_tilemap,test_wo09_c03_ui,test_wo09_c04_graphs,
  test_wo09_c05_json,test_wo09_c06_services}.cpp`, `tests/{wo09_fuzz.h,wo09_tilemap_oracle.h}`,
  `tests/test_crossbuild_scene.cpp`, `tests/CMakeLists.txt`, `tests/render/{render_wo09_sprites,
  render_wo09_tilemap,render_wo09_ui_lights}.cpp`, `tests/render/CMakeLists.txt`,
  `tests/fixtures/wo09/**`.
- Runner: `tests/acceptance/AcceptanceRunner.psm1` (audio-device probe),
  `tests/acceptance/fixtures/{Run-WO09Case,Run-WO09Lifecycle}.ps1`,
  `tests/acceptance/manifests/wo09-{units,gpu,editor,retained}.manifest.json`.
- Docs: `docs/reference/ecs.md`, `docs/guide/{scenes-and-serialization,flow-and-story,scripting}.md`,
  `docs/plans/2d-stability-2026-09-16/contracts/{known-issues,numeric-bar-policy,
  retained-feature-register}.md`, this directory.

## Local commits (not pushed — Kaden pushes)

1. `acb3c0412f97486c9c89192429a18f68c6506f94` — Fix sprite order, animation, hierarchy, loader, serializer, script, job,
   flow and config defects KI-40..KI-49 (WO-09): the ten engine fixes, the KI-50 PDB fix, the
   register entries KI-40..KI-50, the two contract updates and the reference/guide chapters.
2. `9894f64c72c59d4efceb52d5aa2c2ae0b3873138` — Add C01–C06 content/services acceptance cases, F-CONTENT/F-CORRUPT fixtures, the
   C05 editor host and the wo09 runner manifests (WO-09): every test, fixture, wrapper and
   manifest, the test/render CMake lists, the editor hooks, the runner's audio-device probe.
3. The evidence commit that carries this report and `evidence/WO-09/**` (JSON/JUnit/excerpts/
   captures/depth ladders/C05 result + saved scene/KI counterfactuals; the gitignored `*.log`
   files are not committed).

Author and committer on all three: `kdadabhoy <kdadabhoy28@gmail.com>`, no trailers. `main` is
3 ahead of `origin/main` (`3ef6981`); Kaden's push = `git push origin main`.
