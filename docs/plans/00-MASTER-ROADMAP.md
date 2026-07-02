# Cosmic — Master Roadmap (2026-07-01)

> **Why this exists:** one place that says *what to do in what order* across six workstreams:
> bug fixes, AI-driven fixing, UAV-simulator engine features, the ViperSim app, 3D, docs, and
> packaging. Each workstream has its own plan document; this file only sequences them.
>
> | Doc | Covers |
> | --- | --- |
> | [`01-bug-audit.md`](01-bug-audit.md) | Verified bugs & hardening list (audited 2026-07-01, claims hand-checked) |
> | [`02-bugfix-ai-gameplan.md`](02-bugfix-ai-gameplan.md) | Paste-ready work orders (WO-1…16) for a smaller AI |
> | [`03-uav-sim-engine-features.md`](03-uav-sim-engine-features.md) | Engine gaps for the simulator (E1–E8) |
> | [`04-uav-sim-app-plan.md`](04-uav-sim-app-plan.md) | ViperSim app + portable `viper-fc` flight code (P0–P8) |
> | [`05-3d-engine-plan.md`](05-3d-engine-plan.md) | 3D: sim viewport now (S1–S3) → full 3D engine tier (S4–S5); forward-compatibility contract |
> | [`06-readme-update-plan.md`](06-readme-update-plan.md) | README corrections, new sections, split (A/B/C) |
> | [`07-installer-packaging-plan.md`](07-installer-packaging-plan.md) | `--project` flag, version, user data, Inno installer (steps 1–6) |
> | [`08-audio-plan.md`](08-audio-plan.md) | Audio subsystem (miniaudio): one-shots → loops/groups → positional (A1–A3) |

## The one design rule that spans everything

**The engine ships generic verbs; apps own domain logic.** UDP socket, quaternion math, `DrawMesh`,
COBS framing, `user://` paths → engine. Tailsitter mixers, MAVLink, aero polars, orbit-on-ROI,
energy budget → ViperSim / `viper-fc`. When an app needs something the engine doesn't have, the
engine grows a *general* verb, never a Viper-shaped one. Every plan doc applies this rule.

---

## Phase order

### Phase 1 — Trusted core ✅ *(complete 2026-07-01)*
Fix what's broken before building on it.
- **Do:** doc 02, WO-1 … WO-13 (grouped into the 4 PRs listed there). Then WO-14/15 (tests + CI) —
  strongly recommended before the engine starts growing sim features.
- **You:** review PRs, run `build.bat`, merge. **AI tier:** low — the orders are written for it.
- **Done when:** all WOs merged, CI green on `main`.
- ✅ WO-1…13 landed; WO-14 (`tests/` doctest harness), WO-15 (`.github/workflows/ci.yml`),
  WO-16 (`.clang-tidy` + static-analysis note) in place.

### Phase 2 — Ship one app to the desktop ✅ *(complete 2026-07-01)*
- **Do:** doc 07 steps 1 → 2 → 3 → 4 → 5. Step 1 (`--project` flag) also speeds up daily dev
  immediately. Step 3 (`user://` writable data) is the only one needing care/review.
- **Done when:** a setup exe installs SF_Telem, desktop icon boots straight into it on a machine
  that never saw the SDK.
- ✅ `--project` flag, `Version.h` + DPI manifest, `user://` data root, branded `package.bat`,
  Inno installer (`installer/CosmicSetup.iss` + `package_installer.bat`).

### Phase 3 — Docs honesty pass ✅ *(complete 2026-07-01)*
- **Do:** doc 06 items A1–A4 (corrections) and B1–B5 (missing sections). Leave B6 + the C split
  for Phase 7.
- **AI tier:** low/medium — each item is a self-contained prompt.
- ✅ A1–A4 + B1–B5 done (B3 was already covered by README §24; also added §20.6 framing docs).

### Phase 4 — Sim foundations in the engine ✅ *(complete 2026-07-01)*
Order matters here; it's dependency-driven:
1. ✅ **E1** configurable fixed timestep (doc 03) — `Application::SetFixedTimestepHz`.
2. ✅ **E3** quaternion/frame math header — `Cosmic/src/math/Spatial.h` (+ unit tests).
3. ✅ **S1** 3D viewport: `PerspectiveCamera` + `OrbitCameraController` + `Renderer3D`
   lines/grid/axes (doc 05).
4. ✅ **E5** COBS+CRC framing header — `Cosmic/src/serial/Framing.h` (+ unit tests).
5. ✅ **S2** meshes + primitives + Lambert shading — `graphics/Mesh.h`, `Mesh3D.glsl`.
- **AI tier:** E1/E5 low; E3 medium (get the conventions right once); S1/S2 medium-high — spec each
  file from doc 05 as its own task and review shader/GL code yourself.
- **Done when:** demo layer flies an orbit camera around a shaded placeholder aircraft over a grid.
- ✅ Acceptance app: `Projects/Engine3DDemo` (orbit camera, primitive-built aircraft flying a
  banked NED circle over a grid, E1 rate readout, 2D overlay coexistence).

### Phase 5 — ViperSim to a tuned hover (~3–4 weeks)
- **Do:** doc 04 P0 → P1 → P2 → P3, plus engine E7 (gamepad) when P3 starts.
- The moment P2 exists, adopt the habit: every controller change re-runs the recorded regression
  replays. **Milestone with real-world value:** P3's Energy screen produces the hover-vs-cruise
  power numbers the Viper design doc's "reality check" asks for — before buying parts.

### Phase 6 — The hard flying: transition & orbit (~3–4 weeks)
- **Do:** P4 (full-envelope aero + transition state machine), P5 (orbit-on-ROI + failsafes),
  engine S3 (FPV inset, trajectory ribbon) as P4/P5 need them.
- This phase is the project's core research risk — budget for iteration, keep every attempt
  recorded/replayable.

### Phase 7 — Hardware in the loop + polish
- **Do:** P6 (Teensy HIL over the E5 framing), P7 (gimbal rig). Optionally E4 UDP + P8 (MAVLink →
  QGroundControl). Meanwhile: doc 06 B6 + C (README split), E6 asset cache, IMPROVEMENTS §5
  leftovers (sprite animation, shader hot-reload) as filler.
- **Audio** (doc 08): A1 core playback is a filler task any time after Phase 1; A2 (loop handles,
  alert groups) when ViperSim's failsafe work (P3/P5) wants audible warnings.

### Phase 8 — Full 3D engine tier (doc 05, S4–S5)
When the sim viewport is proven in use: unified camera hierarchy → material-driven meshes → 3D scene
components (`Scale` → vec3, `MeshRendererComponent`) → asset cache + glTF → lighting v1 → MRT
framebuffers, then the S5 tier (shadows, PBR/IBL, post-processing, culling/sorting, instancing).
Everything shipped in Phase 4 was built under doc 05's forward-compatibility contract, so this phase
extends — it does not rewrite.

---

## Dependency snapshot

```
Phase 1 (bugs+CI) ──► everything
Phase 2 (packaging) ──► independent (only Step 1 touches Application; rebase after WO-13)
E1 ─► E3 ─► S1 ─► S2 ─► S3          (engine sim track)
         E5 ────────────► P6         (framing → HIL)
P0 ─► P1 ─► P2 ─► P3 ─► P4 ─► P5 ─► P6 ─► P7   (app track; P1 needs S1, P3 needs E7, P5 needs S3)
E4 ─► P8   (optional GCS track)
Docs A/B: any time.  Docs C (split): last.
```

## Working agreement (how these plans get executed)

- **Branch per work order / feature**, PR into `main`, following the existing SF-Improvements
  pattern. You compile and run — the AI writes code (per your standing preference).
- **One work order per AI prompt.** Docs 02, 06, and 07 (steps 1/2/4/5) are written for a
  lower-tier model; docs 03/04/05 items marked medium-high deserve the stronger model or your review.
- **Re-verify before edit:** every order tells the model to confirm quoted code still exists —
  respect that; the line numbers were true on 2026-07-01 and will drift.
- **Definition of done** is in each doc's acceptance lines; a phase isn't done until its acceptance
  demo runs (and, from Phase 5 on, is saved as a replayable recording).
