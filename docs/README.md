# Cosmic Documentation Index

> **Start here if you're new.** This page maps the whole documentation tree — what each
> folder is for and which document answers which kind of question.

Cosmic's documentation has **four tiers**, each answering a different question:

| Tier | Question it answers | Where | Format |
| --- | --- | --- | --- |
| **Overview** | *"What is this, and where do I look?"* | [`README.md`](../README.md) (repo root) | Subsystem tour + documentation map; **§1.5 command reference** and **§1.6 the one engine configuration (2D trunk)** live here in full |
| **Developer Guide** | *"How do I do X?"* — task-oriented, learn by doing | [`docs/guide/`](guide/README.md) | One chapter per topic: quick start → task sections → patterns → pitfalls |
| **API Reference** | *"What exactly does this call do?"* — formal per-command lookup | [`docs/reference/`](reference/README.md) | OpenGL-style entries: signature, description, example, why-you'd-use-it, pitfalls |
| **System Explainers** | *"How does this subsystem actually work?"* — narrative deep dives | [`docs/systems/`](systems/README.md) | Plain-English overview first, then technical implementation |

> **The guide tier is complete (2026-07-26).** The root README was a 4,875-line monolith. All of its
> Part I topics now live in `docs/guide/` — **29 chapters written from scratch against the source**,
> not extracted, because sixteen phases had landed after that text was written. Every §1–§29 heading
> in the README is an overview plus a chapter link; only §1.5 (commands), §1.6 (the engine
> configuration — one, 2D-only, since 2026-09-18) and §21.5 (the multi-screen homescreen shape) stay there in full. Tracked as doc 12
> Phase C (D46–D61). **Part II (§30–§43) is still in the README** and moves to `docs/systems/` in
> Phase D; README [§42.5](../README.md#425-where-the-rest-of-part-ii-lives--the-systems-directory)
> lists what is there today.

## Folder map

| Path | Contents |
| --- | --- |
| [`../README.md`](../README.md) | The **overview** — what Cosmic is, how to build it, a tour of every subsystem, and the documentation map. §1.5 is the canonical **command-line reference** (scripts, exe flags, CMake options, hotkeys); §1.6 explains the **two engine configurations**. |
| [`guide/`](guide/README.md) | **Developer Guide** — task-oriented chapters with worked examples: *how do I draw a sprite sheet, persist settings, wire a flow graph, ship a build*; the [PendulumLab walkthrough](guide/pendulumlab-walkthrough.md) builds an app from scratch in Starforge. 24 live chapters (the 3D chapters are parked). |
| [`reference/`](reference/README.md) | **API Reference** — every public class/function a project can call, chaptered by domain (2D rendering, ECS, events, audio, …). The upkeep contract lives in its index: implementation PRs that touch public API must update the matching chapter. |
| [`systems/`](systems/README.md) | **Subsystem explainers** — one document per engine system, written so a newcomer can follow it: high-level overview → mental model → implementation walkthrough. |
| [`plans/`](plans/00-MASTER-ROADMAP.md) | Live work plans. Start at [`00-MASTER-ROADMAP.md`](plans/00-MASTER-ROADMAP.md) (v5). The two campaign packets are [`plans/2d-stability-2026-09-16/`](plans/2d-stability-2026-09-16/00-Start-Here.md) (closed; WO-00..10 done) and [`plans/app-platform-2026-09-18/`](plans/app-platform-2026-09-18/00-Start-Here.md) (current; work orders, contracts, acceptance catalog, evidence, the [known-issues register](plans/2d-stability-2026-09-16/contracts/known-issues.md)). Completed plans are in [`plans/archive/`](plans/archive/README.md). [`FEATURE-MATRIX.md`](plans/FEATURE-MATRIX.md) indexes every missing/parked feature → its phase home → its unlock. `plans/archive/` keeps completed plans as records (live docs contain only unimplemented work — the v3 rule). |
| [`design/`](design/) | Accepted design documents (e.g. [`frame-lifecycle.md`](design/frame-lifecycle.md) — the renderer's backend-portability spec, [`water-rendering-notes.md`](archive/design/water-rendering-notes.md)). These are decision records, not tutorials. |
| [`engineering-notes/`](engineering-notes/) | Postmortems and investigation write-ups (why a bug happened, what fixed it). |
| [`archive/`](archive/) | Historical analyses kept for the record; [`archive/design/`](archive/design/README.md) holds superseded design proposals and acceptance scripts. |
| [`parked-3d/`](parked-3d/README.md) | **Parked 3D documentation** (parked 3D) — every chapter about code that now lives only on the `engine-3d` branch (`0e8894b`, tag `cosmic-pre-2d-2026-09-16`), with a PARKED banner at line 3 and how to resume 3D. |
| [`showcase/`](showcase/README.md) | Twelve captures of the editor and PendulumLab, the feature list and the blurb (AP-Q1). |
| [`installer-guide.md`](installer-guide.md) | User-facing walkthrough: build, package, and ship a setup `.exe`. |

## Status

| Tier | State |
| --- | --- |
| [`guide/`](guide/README.md) | **Complete** — 24 live chapters (29 written D46–D61; 6 parked as 3D, `lighting-2d.md` split out 2026-09-20); AP-D2 adds the app-authoring chapter |
| [`reference/`](reference/README.md) | In progress — most chapters are still skeletons (D6–D18) |
| [`systems/`](systems/README.md) | In progress — 1 of 15 live explainers written (D25–D34); 8 parked as 3D |
| [`../README.md`](../README.md) | Quickstart + Part I overviews + Part II internals (2D only since 2026-09-20; the 3D Part II material is parked) |

Skeleton chapters carry a `STATUS: SKELETON` banner until their work order lands; anything without a
banner is live documentation and subject to the upkeep contract in
[`reference/README.md`](reference/README.md). **Where a reference or systems chapter is still a
skeleton, the matching guide chapter is the client-facing source** and says so in its header block —
check the Status column in each tier's index rather than assuming a link is populated. The original docs plan is archived at [`plans/archive/12-documentation-plan.md`](plans/archive/12-documentation-plan.md); live docs work is tracked in the [App Platform packet](plans/app-platform-2026-09-18/00-Start-Here.md) (AP-D1, AP-D2). Links are checked by `tests/check_docs_links.ps1`.
