# Cosmic 2D — numeric-bar policy

Status: WO-00 decision record, 2026-09-16. Gate G0.
Revalidated at HEAD `72b47771c869666f3a645a47bfbfae917d3167f2`.

## Headline rule

**No numeric bar in [`../03-Acceptance-Test-Catalog.md`](../03-Acceptance-Test-Catalog.md) gates a
merge until WO-02 measures a baseline for it on a named reference machine.** Until then every
threshold is a *provisional target*, not a pass/fail line. A change that misses a provisional bar is
a signal to investigate and to arm the bar in WO-02 — it is **not** grounds to loosen the bar, and a
bar is never loosened merely because a later change fails it.

D-9km corollary: **no consumer-derived bar is required for this milestone.** The synthetic
`F-TRAJECTORY` / `F-SERIES-LARGE` fixtures are spec-derived (below), so X01 still runs; the real
to-9km precision/units/volume bars are deferred until real to-9km data exists.

## Three classes of bar

- **Measurement-derived** — a threshold that only means something once measured on named hardware:
  timing, latency, memory, throughput, drift-vs-wall-clock. These are **armed by WO-02** (baseline)
  and enforced from WO-13 (release) on the pinned candidate + qualified machine. Performance figures
  are machine qualifications, **not** guarantees for every supported CPU/GPU.
- **Consumer-derived** — a threshold that can only come from the real downstream consumer's schema
  and workflow (to-9km-and-beyond): required precision/units/tolerance, real max series size, real
  session duration, "qualified downstream consumer" throughput. **Deferred by D-9km. None required
  this milestone.**
- **Definition / spec-derived** — a threshold fixed by a mathematical definition, an existing code
  constant, or a policy choice — independent of hardware. These are validated by a **deterministic
  run** on the candidate (correct/incorrect), not by a machine baseline; they still do not "gate"
  until the candidate is pinned, but WO-02 does not need to *measure* them.

## Classification of every numeric bar in doc 03

### Measurement-derived (WO-02 arms them on a named machine)

| Bar (doc 03) | Provisional value | Reference / notes |
| --- | --- | --- |
| Normal close deadline | ≤ 2 s | vs live SPP `CreateFileA` 10–20 s stall; see [`contracts.md`](contracts.md) §1 |
| Delayed-connect cancellation | bounded completion/cleanup | calibrated to the close contract |
| Close with pending recording export | ≤ 30 s for the 2-h fixture | or explicit failure |
| UI service heartbeat during connection ops | no gap > 250 ms; record p95/p99 | reference hardware |
| Clock drift over 2-h run | ≤ one 60-Hz fixed step vs integer-tick/double reference | proposed target, not a claim about the current float accumulator |
| 10,000-instance frame time | p95 ≤ 16.67 ms, p99 ≤ 33.33 ms @1080p Release vsync-off, 10 s warmup + 60 s sample | **named qualified machine only** |
| Non-recording memory plateau | final-5-min median − initial-5-min median ≤ max(32 MiB, 5% baseline); trend ≤ 1 MiB / 10 min | private bytes preferred |
| SF two-hour recording peak | private-bytes ceiling 2 GiB | capacity/snapshot/CSV growth accounted separately |
| Resource-lifetime "quiescence" comparison | zero net owned threads/handles/callbacks/GPU objects | *value* is an invariant (spec), but the warmed-baseline comparison is measured |

### Definition / spec-derived (validated by a deterministic run, not a machine baseline)

| Bar (doc 03) | Value | Why it is not measurement-derived |
| --- | --- | --- |
| Default float comparison | `abs(error) ≤ 1e-6 + 1e-5·abs(expected)` | policy constant; never applied to bit-exact storage asserts |
| Existing golden policy | per-channel 2/255; ≤ 0.1% pixels beyond | matches the current `GoldenImage.h` constant |
| Camera projection round-trip | ≤ 0.5 screen px within the declared local envelope | geometric bound within a declared envelope |
| Instancing chunking | `ceil(N/20,000)` instance draws; 10,000 ⇒ 1 | code contract (R03) |
| Batch limit sweep | limit = 10,000 (0/1/limit-1/limit/limit+1/2·limit+1) | declared code constant (R02) |
| Analytic integrator bounds | RK4 `1e-4` rel; oscillator `2e-3`; RK4 halving-step ratio 10..24; semi-implicit energy < 1.10·initial | existing analytic references (N03) |
| RNG / determinism | canonical PCG vector; 1,000,000 same-seed outputs; same-build reproducibility | deterministic, same-toolchain scope (N04) |
| Trajectory fixture | `F-TRAJECTORY` 1,201 samples, `x=30t`, `y=50t−0.5·9.80665·t²`, `t=i/120` | equation-defined synthetic fixture (X01) — **spec-derived, so X01 runs despite D-9km** |
| Large-series fixture | `F-SERIES-LARGE` 100,000 samples/channel, 8 finite channels | equation/seed-defined synthetic fixture |
| Tilemap ceiling | 1,048,576 cells max; 1025 invalid-boundary (clamped to 1,024, never a 1,025-wide map) | declared code contract (C02) — validated by WO-09 (EnsureCells + serializer + the full 1M-cell map on the GPU) |
| Hierarchy depth ceiling | 4,096 nodes per path (`Scene::kMaxHierarchyDepth`); deeper data is truncated, never a crash | declared code contract (C03) — ratified by WO-09, see below |
| JSON nesting limit | 512 levels (`SceneSerializer::kMaxJsonNestingDepth`); deeper scene / prefab / material documents are rejected before parsing | declared code contract (C05, KI-44) |
| Flow cascade guard | 100,000 signal iterations per `FlowMachine::OnUpdate`, then the queue is dropped with a warning; a push cycle reaches a stack depth of 100,002 | existing code constant (C04), pinned by WO-09 |
| Fuzz volumes | 2,000 cases/parser (PR), 50,000 (nightly); every failure minimized to a fixture | policy choice |

### Deadline limits (policy limits, not performance bars)

Standard deadlines are *limits, not sleeps*, extended only through a reviewed contract change backed
by measurement: U case 10 s; small W/G/I scenario 60 s; 50-rebuild campaign 30 min; serial 35 min for
a 30-min run; soak 140 min for a 120-min session. A timeout is FAILED, never a graceful pass.

### Ratified / to-be-ratified ceilings (decision, measurement-informed — set in WO-00/02)

- **Ratified (Kaden 2026-09-17):** supported maximum recording **session length = 2 h**, over-limit
  policy = **stop-and-finalize** (tunable; [`contracts.md`](contracts.md) §2).
- **Ratified (WO-09, 2026-09-18) — supported hierarchy DEPTH ceiling = 4,096 nodes per path**
  (`Scene::kMaxHierarchyDepth`: self + 4,095 ancestors / descendants). It applies to the UI canvas
  walk (`UiSystem::CollectElements` / `Render` / `HitTest`), the parent chain
  (`GetWorldTransform`, `IsAncestor`, `IsActiveInHierarchy`), the subtree walks
  (`DestroyEntity`, `SceneSerializer::SavePrefab`) and, through them, every editor and player
  path. Measurement behind it (C03 depth ladder, `evidence/WO-09/`): before WO-09 a chain built
  with the public `SetParent` overflowed the stack in `CollectElements` between 2,500 and 2,750
  levels (Release; Debug lower), `GetWorldTransform` between 3,000 and 4,096, `DestroyEntity`
  between 4,096 and 8,192, `SavePrefab` between 8,192 and 16,384, and a hand-authored cycle never
  returned (KI-42). After WO-09 every walker is iterative or guarded at the ceiling: 4,095 / 4,096
  / 4,097 / 8,192-deep chains and A<->B / self cycles all terminate in both configurations, with
  the documented truncation beyond the ceiling (UI elements / prefab entities below it omitted with
  one warning; `DestroyEntity` orphans what it did not reach). The number is the existing
  `IsActiveInHierarchy` guard — nothing an editor or the serializer produces comes within two
  orders of magnitude of it (a real UI nests ~6 deep). Class: definition/spec-derived (a code
  constant), validated by a deterministic run.
- **Proposed (WO-09, 2026-09-18, measurement-informed) — supported 2D LIGHT ceiling = 100
  simultaneous `Light2DComponent`s per frame at 1920x1080.** Measured on the reference machine
  (C03 GPU ladder, Release, RTX 5070 Ti, radius 120 px lights, sprites + composite + `FinishGpu`):
  1 / 10 / 100 / 1,000 lights at 320x180 = 0.16 / 0.17 / 0.19 / 0.49 ms per frame; 100 / 1,000
  lights at 1920x1080 = 2.0 / 2.1 ms per frame. Correctness holds at 1,000 (no pixel below
  ambient, every light additive), so 100 is a generous *support* line, not a cliff — one light is
  one additive quad into the half-res buffer, so the cost is fill-bound in the light radius, not
  the count. Under 100 lights the pass stays below 15 % of a 60-Hz frame on the reference GPU; a
  scene that wants more is a performance question (WO-13 qualification), not a correctness one.
  Class: measurement-derived (qualifies the named machine only). Ratification of the number is
  Kaden's call; the `known-limitation` flag in the retained-feature register now carries it.

### Consumer-derived (deferred by D-9km — none required this milestone)

| Deferred bar | Why deferred |
| --- | --- |
| Real to-9km required precision / units / conversion tolerance | needs the real schema; float→double contract deferred ([`contracts.md`](contracts.md) §5) |
| Real to-9km max series size / session duration | needs the real workload |
| "Qualified downstream consumer" throughput/latency | X01 is a *compatibility specimen*, not consumer qualification |
| Native large-coordinate precision throughout Cosmic | explicitly **not** promised; X01's `1e11` offset is subtracted in `double` before float display and tests only the sample's conversion |

## What "arming" a measurement-derived bar requires (WO-02)

For each measurement-derived bar, WO-02 records: the **named reference machine** (CPU model + SSE4.2
confirmation, GPU model + driver + GL renderer/version string, OS build, RAM), the exact command +
config (`COSMIC_2D_ONLY=ON`, Debug/Release), the measured baseline distribution (median / p95 / p99
where relevant), and the seed/fixture hash. Only after a bar is armed against that machine does it
gate on the pinned candidate (WO-13, S04). A bar with no armed baseline yields "not yet gating,"
never a silent pass and never a phantom failure.

## Cross-references

- CPU floor (SSE4.2) that underlies every performance bar: [`support-matrix.md`](support-matrix.md),
  D-CPU.
- The provisional close/recording/precision numbers these bars calibrate:
  [`contracts.md`](contracts.md).
- Enforcement + evidence rules: `work-orders/README.md` global rules 7–8 (honest gates, evidence).
