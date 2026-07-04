# Cosmic Documentation Index

> **Start here if you're new.** This page maps the whole documentation tree — what each
> folder is for and which document answers which kind of question.

Cosmic's documentation has **three tiers**, each answering a different question:

| Tier | Question it answers | Where | Format |
| --- | --- | --- | --- |
| **Developer Guide** | *"How do I do X?"* — task-oriented, learn by doing | [`README.md`](../README.md) (repo root) | Long-form guide: Part I client guide, Part II engine internals |
| **API Reference** | *"What exactly does this call do?"* — formal per-command lookup | [`docs/reference/`](reference/README.md) | OpenGL-style entries: signature, description, example, why-you'd-use-it, pitfalls |
| **System Explainers** | *"How does this subsystem actually work?"* — narrative deep dives | [`docs/systems/`](systems/README.md) | Plain-English overview first, then technical implementation |

## Folder map

| Path | Contents |
| --- | --- |
| [`../README.md`](../README.md) | The root Developer Guide — getting started, every client API topic, engine internals. §1.5 is the canonical **command-line reference** (scripts, exe flags, CMake options, hotkeys). |
| [`reference/`](reference/README.md) | **API Reference** — every public class/function a project can call, chaptered by domain (3D rendering, events, audio, …). The upkeep contract lives in its index: implementation PRs that touch public API must update the matching chapter. |
| [`systems/`](systems/README.md) | **Subsystem explainers** — one document per engine system, written so a newcomer can follow it: high-level overview → mental model → implementation walkthrough. |
| [`plans/`](plans/00-MASTER-ROADMAP.md) | Live work plans. Start at [`00-MASTER-ROADMAP.md`](plans/00-MASTER-ROADMAP.md) for phase order; each numbered doc holds PR-sized work orders with acceptance checks. [`FEATURE-MATRIX.md`](plans/FEATURE-MATRIX.md) indexes every missing/parked feature → its phase home → its unlock. `plans/archive/` keeps completed plans as records (live docs contain only unimplemented work — the v3 rule). |
| [`design/`](design/) | Accepted design documents (e.g. [`frame-lifecycle.md`](design/frame-lifecycle.md) — the renderer's backend-portability spec, [`water-rendering-notes.md`](design/water-rendering-notes.md)). These are decision records, not tutorials. |
| [`engineering-notes/`](engineering-notes/) | Postmortems and investigation write-ups (why a bug happened, what fixed it). |
| [`archive/`](archive/) | Historical analyses kept for the record. |
| [`installer-guide.md`](installer-guide.md) | User-facing walkthrough: build, package, and ship a setup `.exe`. |

## Status

The API Reference and System Explainers are being built out per
[`plans/12-documentation-plan.md`](plans/12-documentation-plan.md) (work orders D5+). Skeleton
chapters carry a `STATUS: SKELETON` banner until their work order lands; anything without a
banner is live documentation and subject to the upkeep contract in
[`reference/README.md`](reference/README.md).
