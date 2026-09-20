# Cosmic — 2D Trunk Consolidation & Acceptance Plan

> **ARCHIVED 2026-09-20** — completed/superseded; kept as the record of what was built and why. Do not execute. Origin: the original 2D-trunk consolidation brief (v1, Kaden, 2026-09, kept untracked at the repo root while the campaigns ran; an identical copy is `../2d-stability-2026-09-16/Original-Consolidation-Plan.md`). Landed by: the two campaign packets. Replacement: **superseded by** [`../2d-stability-2026-09-16/00-Start-Here.md`](../2d-stability-2026-09-16/00-Start-Here.md) (WO-00..WO-10) and [`../app-platform-2026-09-18/00-Start-Here.md`](../app-platform-2026-09-18/00-Start-Here.md) (D-PURGE replaced its "gate, don't delete" with deletion; WO-11/12/13 became AP-P1/AP-D1+D2/AP-Q1).

**Status:** requirements brief (v1) · **Owner:** Kaden · **Audience:** AI assistants working with the Cosmic repo, and Kaden.

---

## 0. How to use this document

This is a **self-contained brief**. It is meant to be fed to an AI assistant *together with the Cosmic repository*. It states the goal, the branch strategy, the 2D feature surface that must work, the additions to build, and the acceptance/stress-test bar. It intentionally stops short of per-file steps.

**A follow-on AI with repo + git access is expected to:**
1. Catalogue the branches and confirm the current-state facts below (treat every repo claim here as "verify in the repo," not as ground truth).
2. Recommend the 2D **base branch** and the exact set of changes to port into it (§3).
3. Refine the requirements (§5–§7) into concrete, per-feature test cases with fixtures and thresholds.
4. Produce a step-by-step git migration runbook and walk Kaden through it (§10).

Nothing here should be implemented blindly — where this doc and the repo disagree, the repo wins; raise the conflict.

---

## 1. Goal

Establish **one stable, feature-complete 2D trunk** for Cosmic that reliably backs Kaden's project slate, while **preserving the existing 3D engine** as a parked starting point for future 3D work. **No functionality or bug fix already achieved may be lost** in the transition.

Cosmic is the shared **animation + telemetry backend** for the downstream projects. Going forward, only the 2D engine is actively developed.

**Downstream consumers (who this trunk must serve):**
- **SF_Telem** (Shear Force telemetry app) — already built; the reference workload. Its serial/telemetry logic is the known-good baseline (see §2).
- **to-9km-and-beyond** — trajectory animation + plots.
- **Aether** — airplane-sizing visualization (constraint diagrams, geometry sketch, parameter sweeps).
- **Equatorium** — orbital viewer (planet positions over time, JPL vs. R2BP overlay, mission trajectories, satellite-pass sub-module).
- **Eos** (optional) — high-power-rocketry flight-reconstruction plots + live/replay telemetry, *if* Cosmic is the best path for it.

---

## 2. Branch strategy (required end state)

- **Keep all current branches.** Nothing is deleted.
- **A dedicated 3D branch preserves the full 3D engine.** *Before* `main` changes, branch today's `main` **as-is** to a new branch (e.g. `engine-3d`) — a snapshot of the current full-3D engine, kept as the starting point for later 3D work. Parked and **not tested** under this plan (may be unstable; acceptable).
- **`main` becomes the go-forward 2D trunk, gated to 2D-only.** On `main`, gate the entire 3D feature set off with the existing `COSMIC_2D_ONLY` build flag (**decided path — no source deletion**), so `main` builds and ships the 2D engine only. It then carries the best-available 2D functionality + bug fixes, including `SF-Stable`'s known-good telemetry logic (§3).
- **Preserve SF_Telem's known-good logic.** `SF-Stable` is the **bug-free reference** for the serial/telemetry path. `main` must use `SF-Stable`'s telemetry logic for SF_Telem and must **not regress it**.
- **Maximize retained value.** `main` / `engine-2d` and `SF-Stable` may each hold 2D features or fixes the others lack. Catalogue and consolidate them onto `main` (§3) so the result has maximum functionality + fixes + stability.

**Current branches (per the Cosmic project record — confirm in the repo):**
| Branch | Role today | Intended role after this work |
| --- | --- | --- |
| `main` | Full 3D engine (2D + 3D from one tree) | **Go-forward 2D trunk** (2D-only build) |
| `engine-2d` | 2D-only build (historically byte-identical tracked files to `main`, selected by the `COSMIC_2D_ONLY` CMake flag) | 2D source/reference; fold anything unique into `main`, then keep as history |
| `SF-Stable` | Stable Shear Force telemetry build | **Bug-free telemetry baseline**; its telemetry logic is ported into `main` |
| *(new)* `engine-3d` | — | **Preserved full 3D engine**, branched from today's `main`; parked, gated, untested |

> **Decided:** gate 3D off `main` with `COSMIC_2D_ONLY` — the 3D source stays in the tree but is never built into `main`'s DLL (no source deletion). **Still to pick:** the name for the new 3D branch (e.g. `engine-3d`).

---

## 3. First required task — preserve 3D, then catalogue & consolidate onto `main`

Before converting `main`, produce a catalogue so nothing is lost:

1. **Preserve 3D first.** Branch today's `main` (full 3D) to the new 3D branch (§2) so all current 3D functionality is safe *before* any change to `main`.
2. **Enumerate 2D features + bug fixes across `main`, `engine-2d`, and `SF-Stable`** — which are present / absent / partial on each. Sources: `git log` / `git diff` across the branches, `docs/plans/FEATURE-MATRIX.md`, and the phase docs under `docs/plans/`.
3. **Identify where `main`'s telemetry (and other) logic diverges from `SF-Stable`'s known-good version**, and reconcile — prefer `SF-Stable` for the telemetry path, keep `main`'s broader 2D feature set elsewhere.
4. Build a **branch comparison matrix** — feature/fix × {`main`, `engine-2d`, `SF-Stable`} × present / absent / partial — and produce the **exact ordered list of ports** (cherry-picks / merges) to land on `main`, calling out conflicts.
5. Deliver the catalogue + reconciliation plan for Kaden to approve before code moves.

**Guiding constraint:** the resulting `main` (2D trunk) must be ≥ every branch in 2D functionality *and* ≥ `SF-Stable` in stability. If those pull in different directions, prefer stability for the telemetry path and port features on top.

---

## 4. Scope

- **In scope:** the 2D engine only — the trunk consolidation (§2–§3), the must-work 2D surface (§5), the five additions (§6), and the 2D acceptance/stress tests (§7).
- **Out of scope:** the entire 3D feature set. It is not built into the 2D trunk and **is not tested** under this plan. It remains on the frozen `main`.
- No cross-branch "byte-identical" guarantee is required once `main` is frozen; the 2D trunk is free to diverge.

---

## 5. Must-work 2D feature surface (M1–M12)

The critical support surface: if any of these breaks, a downstream project cannot proceed. Bold values are **default pass bars** — the repo AI should refine them into concrete test cases. All items are current engine capabilities (per the record/docs); the task is **prove they work on the 2D trunk and do not regress**.

| # | Feature | Depends on it | How to stress-test | Pass bar |
| --- | --- | --- | --- | --- |
| **M1** | Plugin-DLL load + hot reload | every project | Load a sample project DLL in the host(s); run **50** rapid rebuild/hot-reload cycles | 50 cycles, **no crash, no leaked GL/handles**, documented state behavior preserved |
| **M2** | Renderer2D primitives (quad / rotated / textured, SDF circle→ring, line, rect, SDF world-space text) | to-9km, Equatorium, Aether, Eos | Scene exercising each primitive; golden-image compare | Matches **2D goldens**; text crisp under zoom |
| **M3** | Instanced 2D (`DrawInstancedCircles` / `DrawInstancedQuads`) | Equatorium (many bodies + trails) | Render **10,000** animated instances | **≥60 fps**, correct batching, one draw call per batch |
| **M4** | ImPlot charting in-DLL (time-series, XY, scatter, shaded band, multi-series legend, log/linear axes), theme-synced | to-9km, Aether, Equatorium, Eos | Sample DLL draws a live time-series and an XY scatter in each host exe | Renders in every host; **survives hot reload** (context adopted, no crash) |
| **M5** | Live serial / UART telemetry (`TelemetryPanel`, per-channel rings) — **must match `SF-Stable` logic** | Eos, SF_Telem | Feed a simulated / loopback serial stream (incl. KISS) for **30 min** | **0 desync**, correct values, no dropped frames; behavior identical to `SF-Stable` |
| **M6** | Telemetry replay (`DataPlayer`, transport: play / pause / scrub / speed) | Eos, to-9km, SF_Telem | Record a session, reload, scrub + vary speed | Replay **matches recorded data**; scrub frame-accurate |
| **M7** | CSV data I/O (`DataExport::LoadCSV` / `WriteCSV`) | ingest sim / MATLAB / JPL Horizons output | Round-trip a CSV with headers + ragged rows + edge cases | **Lossless round-trip**; header auto-detect + ragged rows handled |
| **M8** | 2D camera (pan / zoom) + multi-camera RenderPass + render-to-texture | all viz; Equatorium/Transit map inset | Pan/zoom across a wide range; render-to-texture into a UiImage | No artifacts across the target zoom range; **RTT correct** |
| **M9** | Animation / Timeline playback (dual-rate clock, reusable Timeline widget scrub) | to-9km, Equatorium, Eos | Data-drive a marker along a path via the clock; scrub | **Deterministic** for identical data; scrub accurate |
| **M10** | Still-image capture (`ImageIO` / `FrameBuffer::ReadPixels` → PNG) | static figures for the website | Capture a framebuffer to PNG | Correct pixels, top-left origin, headless-safe |
| **M11** | Sim / math toolkit (fixed-step integrators, filters, lookup tables, deterministic PCG32 RNG) | R2BP propagation, Monte-Carlo dispersion, numerics | Integrator vs. analytic solution; **2** RNG runs from one seed | Integrator error **< tolerance**; RNG **bit-match** across runs |
| **M12** | 2D-only trunk builds, boots, authors | the whole trunk decision | Build the 2D config; run `CosmicTests`; author a 2D scene in the editor | Builds clean; **2D test suite green + 2D goldens green**; boots + authors 2D. *No 3D targets configured/tested.* |

---

## 6. Additions to implement (A1–A5)

Five capabilities to add on top of the consolidated 2D trunk. Implement after the baseline passes §5.

| # | Addition | For | How to stress-test | Pass bar |
| --- | --- | --- | --- | --- |
| **A1** | **Animated export** → GIF / MP4 / frame-sequence *(still-PNG capture already exists; motion export does not)* | to-9km, Equatorium, Aether website artifacts | Export a **10 s** animation headless | Valid MP4 **and** GIF at **1080p / 30 fps**; frame count + timing correct; runs headless |
| **A2** | **UDP transport** for telemetry *(parked feature "C1"; serial-only today)* | Eos wireless, drone links | Loopback UDP stream into `TelemetryPanel`; inject packet loss / reorder | **Parity with the serial path** over 30 min; graceful under loss/reorder |
| **A3** | **Filled / arbitrary polygon** primitive in Renderer2D | Aether wing planforms; Aether/Eos feasible-region + landing-ellipse fills | Draw a filled trapezoid + filled ellipse; golden compare | Correct fill; **batches with existing 2D calls**; new golden added |
| **A4** | **2D-native particles** (sprite / Renderer2D emitter path) *(parked)* | exhaust-plume / spark polish on animations | Emit a plume in the 2D config | Renders in the 2D build; **deterministic seed**; **≥60 fps** at target count |
| **A5** | **Reusable analysis widgets** on ImPlot / Renderer2D: `TrajectoryView` (path + moving marker + scrubber), `ConstraintDiagram` (lines + shaded feasible region), `DispersionView` (scatter + landing ellipse + histograms), `ModelCompareView` (two series + error subplot) | to-9km, Aether, Eos, Equatorium | A demo project instantiates all four with sample data | Each renders correctly; **reusable, documented API**; the demo uses all four |

---

## 7. Acceptance & stress tests (S1–S5)

| # | Test | Pass bar |
| --- | --- | --- |
| **S1** | Soak: run SF_Telem + a 2D analysis sample for **2 h** | No crash; **no unbounded memory growth** (< ~5% drift) |
| **S2** | Hot-reload storm (ties to M1) | **50** cycles, no crash / leak |
| **S3** | Determinism: repeat runs where determinism is expected (RNG, replay, any bake) | **Bit-match** |
| **S4** | Packaging: package a 2D app + installer; run on a clean machine | Installs + **runs clean** |
| **S5** | CI: `CosmicTests` on the 2D config (consider activating the parked 2D CI leg) | **Green**; goldens updated for new features |

---

## 8. Order of work

1. **Snapshot 3D.** Branch today's `main` **as-is** → new `engine-3d` (the full-3D snapshot); park it.
2. **Gate `main` to 2D-only.** Turn on `COSMIC_2D_ONLY` so all 3D is gated out of `main`'s build.
3. **Catalogue + reconcile telemetry.** Catalogue 2D features/fixes across `main` / `engine-2d` / `SF-Stable` (§3); reconcile `main`'s telemetry logic against `SF-Stable` (the SF_Telem baseline) and land the ports. Kaden approves.
4. **Stress-test the baseline** — M1–M12 + S1–S5; prove parity with `SF-Stable` and no regressions.
5. **Implement the additions** — A1–A5.
6. **Extended stress-testing** — re-run the full bar including the additions (the heavier soak/regression pass).

---

## 9. Definition of Done

- [ ] All current branches retained; **current 3D preserved on a new 3D branch** (e.g. `engine-3d`; parked, gated, untested here).
- [ ] The **branch-comparison catalogue + reconciliation/port list** is delivered and approved (§3).
- [ ] **`main` is the consolidated 2D trunk** (2D-only build) — it has ≥ the 2D functionality of every branch and ≥ `SF-Stable`'s stability; SF_Telem uses `SF-Stable` logic and does not regress.
- [ ] **M1–M12** pass their bars on the 2D trunk.
- [ ] **A1–A5** implemented and passing.
- [ ] **S1–S5** green.
- [ ] `CosmicTests`: 2D suite green **+ new tests for A1–A5**; goldens updated.
- [ ] Both reference workloads (SF_Telem + a 2D analysis sample) run clean.

---

## 10. What the follow-on (repo) AI should produce

- The **branch catalogue + comparison matrix** and the **exact reconciliation/port order** onto `main` (§3).
- The **name of the new 3D branch** (e.g. `engine-3d`). *(Remove-vs-gate is already decided: gate with `COSMIC_2D_ONLY`.)*
- **Per-feature test cases** refining §5–§7 into concrete inputs, fixtures, and thresholds (e.g., exact soak duration, instance counts, export resolution/fps, memory-drift limit).
- A **step-by-step git migration runbook** that walks Kaden through the branch moves safely (no history loss, all branches preserved).
- A short list of **any assumptions it had to make** and **questions for Kaden**.

---

## 11. Reference — engine capabilities that already exist (do not rebuild)

Grounded in the Cosmic source + `docs/`. The 2D trunk should retain all of these; most of §5 is *verification*, not new work.

- **Charting:** ImPlot fully integrated across the plugin-DLL boundary (`InitializePluginContexts`), theme-synced (`UI::ApplyPlotStyle`); `TelemetryPanel` does live + replay per-channel charts with 512-sample rings.
- **Data I/O:** `DataExport::LoadCSV` / `WriteCSV` (header auto-detect, ragged rows); `DataRecorder` / `DataPlayer` columnar record + replay (`scene.bin` + `.csv`).
- **Connectivity:** serial / UART service (wired). *UDP is parked — see A2.*
- **Renderer2D:** `DrawQuad` / `DrawRotatedQuad` (textured), `DrawCircle` (SDF, thickness/fade → rings), `DrawString` (SDF world-space text), `DrawLine`, `DrawRect`, `DrawInstancedCircles` / `DrawInstancedQuads` (tens of thousands). *No filled arbitrary polygon — see A3.*
- **View / scene:** `Camera2DController` (pan/zoom), multi-camera RenderPass, render-to-texture, 2D lighting, sprites, tilemaps, world-anchored UI.
- **Animation:** dual-rate clock (fixed sim + variable render), reusable Timeline widget (scrub/transport), flipbook/sequencer.
- **Capture:** `ImageIO` → PNG screenshots/thumbnails, `FrameBuffer::ReadPixels`. *Motion export does not exist — see A1.*
- **Sim/math:** fixed-step integrators, filters, lookup tables, deterministic PCG32 RNG.
- **Shell:** CosmicApp host + Starforge editor, plugin-DLL hot reload, TOML config, ImGui docking/widgets, job system, packaging + Inno Setup installer, GitHub Actions CI.
