# Cosmic Engine — Design Proposals

> **Purpose:** forward-looking specs for features **before** they're built — goal, objective, design, and
> implementation details, written down so they can be reviewed and refined ahead of the code.
>
> This is the **proposed-design** counterpart to [`../engineering-notes/`](../engineering-notes/) (which is
> *postmortems* of bugs already fixed) and is distinct from [`../plans/`](../plans/) (the live roadmap and
> plan docs) and [`../archive/`](../archive/) (historical analyses).

## How to add a proposal

Create `docs/design/<short-kebab-title>.md` and add a row below. Each proposal should carry a **Status**
(`Proposed` → `Accepted` → `Implemented` / `Rejected`), a **Targets commit** line so the code traces can be
re-checked, and concrete sections: goal/objective, design + implementation details (with `file:line`
references), planned README/doc updates, verification, and open questions.

## Index

| Proposal | Status | What it covers |
| -------- | ------ | -------------- |
| [responsive-rendering-and-pause.md](responsive-rendering-and-pause.md) | Proposed | Keep rendering while the window is dragged/resized (client-toggleable, default on) via a `WM_TIMER` pump during the Win32 modal loop; plus a first-class `Pause()`/`Resume()` that freezes the sim while the UI/render stay live. |
| [water-rendering-notes.md](water-rendering-notes.md) | Accepted | Phase 11 water rendering design + rationale (Subnautica-style layers, GLAD-stays decision, Vulkan = S13 gate). |
| [frame-lifecycle.md](frame-lifecycle.md) | Accepted | S13.2 internals spec: GPU resource creation/destruction rules, binding registry, render-state contract, the pass-by-pass frame, S12 queue semantics, and the S12.6 texture/sRGB/BCn policy — what a second backend must implement. |
| [modularity-audit.md](modularity-audit.md) | Accepted (2026-07-04) | Seam-by-seam swappability audit of the 3D systems: what's already modular, the gaps (each filed as a phase work order), and the "how to swap X" cookbook. |
| [starforge-ui.md](starforge-ui.md) | Implemented (absorbed by the Starforge manual when doc 12 D39 lands) | Stage-D quick guide to the Starforge editor UI/workflows. |
| [starforge-acceptance-demo.md](starforge-acceptance-demo.md) | Accepted | Turnkey script for the recorded Phase 13 acceptance demo (roadmap acceptance-ledger item). |
| [example-images-gap-analysis.md](example-images-gap-analysis.md) | **Accepted (2026-07-11)** — spec of record for roadmap v4 Phases 22–28 (plans docs 21–27) | Gap analysis of the `ExampleImages/` reference editors: every functionality/feel delta (editor shell, viewport, browser, inspector, skeletal-anim editor, node graphs, navmesh, particles, 2D, services) with Today/Change/file references and sizing; every K/T/M/Q/N/X/Z work order cites its §. |
