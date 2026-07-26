# Cosmic Documentation Index

> **Start here if you're new.** This page maps the whole documentation tree — what each
> folder is for and which document answers which kind of question.

Cosmic's documentation has **four tiers**, each answering a different question:

| Tier | Question it answers | Where | Format |
| --- | --- | --- | --- |
| **Overview** | *"What is this, and where do I look?"* | [`README.md`](../README.md) (repo root) | Subsystem tour + documentation map; **§1.5 command reference** and **§1.6 the two engine configurations** live here in full |
| **Developer Guide** | *"How do I do X?"* — task-oriented, learn by doing | [`docs/guide/`](guide/README.md) | One chapter per topic: quick start → task sections → patterns → pitfalls |
| **API Reference** | *"What exactly does this call do?"* — formal per-command lookup | [`docs/reference/`](reference/README.md) | OpenGL-style entries: signature, description, example, why-you'd-use-it, pitfalls |
| **System Explainers** | *"How does this subsystem actually work?"* — narrative deep dives | [`docs/systems/`](systems/README.md) | Plain-English overview first, then technical implementation |

> **The guide tier is complete (2026-07-26).** The root README was a 4,875-line monolith. All of its
> Part I topics now live in `docs/guide/` — **29 chapters written from scratch against the source**,
> not extracted, because sixteen phases had landed after that text was written. Every §1–§29 heading
> in the README is an overview plus a chapter link; only §1.5 (commands), §1.6 (the two engine
> configurations) and §21.5 (the multi-screen homescreen shape) stay there in full. Tracked as doc 12
> Phase C (D46–D61). **Part II (§30–§43) is still in the README** and moves to `docs/systems/` in
> Phase D; README [§42.5](../README.md#425-where-the-rest-of-part-ii-lives--the-systems-directory)
> lists what is there today.

## Folder map

| Path | Contents |
| --- | --- |
| [`../README.md`](../README.md) | The **overview** — what Cosmic is, how to build it, a tour of every subsystem, and the documentation map. §1.5 is the canonical **command-line reference** (scripts, exe flags, CMake options, hotkeys); §1.6 explains the **two engine configurations**. |
| [`guide/`](guide/README.md) | **Developer Guide** — 29 task-oriented chapters with worked examples: *how do I draw a sprite sheet, persist settings, wire a flow graph, ship a build*. Complete since 2026-07-26. |
| [`reference/`](reference/README.md) | **API Reference** — every public class/function a project can call, chaptered by domain (3D rendering, events, audio, …). The upkeep contract lives in its index: implementation PRs that touch public API must update the matching chapter. |
| [`systems/`](systems/README.md) | **Subsystem explainers** — one document per engine system, written so a newcomer can follow it: high-level overview → mental model → implementation walkthrough. |
| [`plans/`](plans/00-MASTER-ROADMAP.md) | Live work plans. Start at [`00-MASTER-ROADMAP.md`](plans/00-MASTER-ROADMAP.md) for phase order; each numbered doc holds PR-sized work orders with acceptance checks. [`FEATURE-MATRIX.md`](plans/FEATURE-MATRIX.md) indexes every missing/parked feature → its phase home → its unlock. `plans/archive/` keeps completed plans as records (live docs contain only unimplemented work — the v3 rule). |
| [`design/`](design/) | Accepted design documents (e.g. [`frame-lifecycle.md`](design/frame-lifecycle.md) — the renderer's backend-portability spec, [`water-rendering-notes.md`](design/water-rendering-notes.md)). These are decision records, not tutorials. |
| [`engineering-notes/`](engineering-notes/) | Postmortems and investigation write-ups (why a bug happened, what fixed it). |
| [`archive/`](archive/) | Historical analyses kept for the record. |
| [`installer-guide.md`](installer-guide.md) | User-facing walkthrough: build, package, and ship a setup `.exe`. |

## Status

| Tier | State |
| --- | --- |
| [`guide/`](guide/README.md) | **Complete** — 29 of 29 chapters written (doc 12 Phase C, D46–D61, closed 2026-07-26) |
| [`reference/`](reference/README.md) | In progress — most chapters are still skeletons (D6–D18) |
| [`systems/`](systems/README.md) | In progress — 2 of 21 written (D25–D34) |
| [`../README.md`](../README.md) | Part I retired to the guide; Part II (§30–§43) still live, moves to `systems/` in Phase D |

Skeleton chapters carry a `STATUS: SKELETON` banner until their work order lands; anything without a
banner is live documentation and subject to the upkeep contract in
[`reference/README.md`](reference/README.md). **Where a reference or systems chapter is still a
skeleton, the matching guide chapter is the client-facing source** and says so in its header block —
check the Status column in each tier's index rather than assuming a link is populated. Progress is
tracked in [`plans/12-documentation-plan.md`](plans/12-documentation-plan.md).
