# Archived Plans — Completed Work, Kept as Records

Everything here **shipped** (first sweep verified 2026-07-01; second sweep 2026-07-04) and is
retained as the record of *what was changed and why*. Do not execute items from these docs — the
live plans are one level up in [`docs/plans/`](../). Relative links inside archived docs may be
stale (files were written from `docs/plans/`); git history has the originals.

**The 2026-07-04 rule (roadmap v3):** a live plan doc contains only *unimplemented* work orders.
When a doc's numbered items are all ✅ (or its leftovers have been carried into a live phase doc),
the whole doc moves here. The "Carried forward" column says exactly where each doc's outstanding
items went — nothing was dropped.

| Doc | What it was | Outcome | Carried forward |
| --- | --- | --- | --- |
| [`01-bug-audit.md`](01-bug-audit.md) | Hand-verified engine bug audit (2026-07-01) | All §1 bugs fixed via doc 02's work orders | — |
| [`02-bugfix-ai-gameplan.md`](02-bugfix-ai-gameplan.md) | Paste-ready work orders WO-1…16 | All landed: fixes + `tests/` harness + CI + clang-tidy | — |
| [`03-simulation-engine-plan.md`](03-simulation-engine-plan.md) | Generic sim toolkit (E-series: config, integrators, filters, tables, noise, RNG, gamepad) | E5–E15 + E7 shipped 2026-07-02 with doctest coverage | E4 (UDP sockets, never started) → doc 20 §C1 |
| [`04-viper-sim-plan.md`](04-viper-sim-plan.md) | ViperSim app + portable `viper-fc` (P0–P8, gates G1–G3) | P0–P7 software complete 2026-07-02; app-specific work is out of roadmap scope by user decision 2026-07-04 | Gate runs G1–G3 + HIL/rig hardware acceptance → roadmap **acceptance ledger**; P8 MAVLink stays app-side (unplanned); engine leftover E4 → doc 20 §C1 |
| [`05-3d-engine-plan.md`](05-3d-engine-plan.md) | The full 3D roadmap S1–S14 (foundations → CAD nav → PBR/IBL → terrain/water/particles → demos → perf → RHI gate) | S1–S13 all code-complete by 2026-07-03 across Phases 7–12; S13.3 verdict = **stay on OpenGL** (provisional-closed, reopen conditions recorded in §12) | S14 backlog + tier deviations dispersed: Jolt → doc 14 · skeletal animation/decals → doc 19/doc 18 · CSM/SSAO-ambient/progressive-bloom/froxels/FFT/tessellation/particle-sort/BCn → doc 18 · positional audio → doc 20 · every row cross-indexed in [`FEATURE-MATRIX.md`](../FEATURE-MATRIX.md) |
| [`06-docs-plan.md`](06-docs-plan.md) | First docs plan (D1–D4) | Superseded 2026-07-03 by [`12-documentation-plan.md`](../12-documentation-plan.md); D1 contract + D2 content absorbed there; D4 shipped | — |
| [`06-readme-update-plan.md`](06-readme-update-plan.md) | README corrections + missing sections | A1–A4, B1–B5 done. Leftovers (B6, C split) carried into doc 06, then doc 12 | — |
| [`07-installer-packaging-plan.md`](07-installer-packaging-plan.md) | `--project` flag, versioning, `user://`, Inno installer | Steps 1–5 shipped. Usage: [`docs/installer-guide.md`](../../installer-guide.md) | Step 6 leftovers (CI release job, signing, `--replay` assoc) → doc 15 §S5 |
| [`08-audio-plan.md`](08-audio-plan.md) | Audio subsystem A1–A3 (miniaudio) | A1 (core playback) + A2 (loops/groups) shipped 2026-07-02 | A3 (positional/streaming) → doc 20 §C2 |
| [`09-windowing-plan.md`](09-windowing-plan.md) | Fullscreen/snip/DPI/responsive-rendering hardening W1–W6 | W1–W6 implemented + log-verified 2026-07-02 | W3 DWM decision + interactive repro matrix (user manual pass) → roadmap **acceptance ledger** |
| [`10-phase11-frontier-plan.md`](10-phase11-frontier-plan.md) | `Projects/Frontier` showcase + engine work orders F1–F17 | All 17 shipped 2026-07-03, committed `c601a8c` | F17 on-GPU ≥60 fps pass w/ screenshots → roadmap **acceptance ledger** |
| [`11-phase13-starforge-plan.md`](11-phase13-starforge-plan.md) | The Starforge editor E1–E21 (reflection, serializer, shell, undo, scripts, hot reload, play, import, materials, world systems, packaging, telemetry) | E1–E21 all code-complete 2026-07-04 (CosmicTests 189/189) | §9 parked table dispersed: P1 STEP + P2 CSG + P5 brushes + P6 pak → doc 19 · P3 Jolt → doc 14 · P4 sequencer → doc 20 · P7 project library → doc 15 · L1–L3 Lua → doc 20 · E-item deviations (SceneRenderer viewport, file dialogs, preview rig, material undo, Release packaging, prefab overrides…) → docs 13/15/19 (each work order cites its E-item origin) · recorded acceptance demo → roadmap **acceptance ledger** |

Superseded (deleted, not archived — replaced by full rewrites; see git history):
`03-uav-sim-engine-features.md` → `03-simulation-engine-plan.md` (now archived here) ·
`04-uav-sim-app-plan.md` → `04-viper-sim-plan.md` (now archived here) ·
the pre-2026-07-01 versions of `00-MASTER-ROADMAP.md` and `05-3d-engine-plan.md`.
