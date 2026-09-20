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

> **2026-09-20 (App Platform AP-D1):** seven proposals whose work shipped, was superseded or describes 3D systems no longer on `main` moved to [`../archive/design/`](../archive/design/README.md) (`forge-isle`, `example-images-gap-analysis`, `starforge-acceptance-demo`, `app-platform-acceptance`, `ui-flow-2d-acceptance`, `water-rendering-notes`, `starforge-ui`). The three below stay live; `frame-lifecycle` and `modularity-audit` carry a dated note where they describe 3D paths that now live only on `engine-3d`.

## Index

| Proposal | Status | What it covers |
| -------- | ------ | -------------- |
| [responsive-rendering-and-pause.md](responsive-rendering-and-pause.md) | Proposed | Keep rendering while the window is dragged/resized (client-toggleable, default on) via a `WM_TIMER` pump during the Win32 modal loop; plus a first-class `Pause()`/`Resume()` that freezes the sim while the UI/render stay live. |
| [frame-lifecycle.md](frame-lifecycle.md) | Accepted | S13.2 internals spec: GPU resource creation/destruction rules, binding registry, render-state contract, the pass-by-pass frame, S12 queue semantics, and the S12.6 texture/sRGB/BCn policy — what a second backend must implement. |
| [modularity-audit.md](modularity-audit.md) | Accepted (2026-07-04) | Seam-by-seam swappability audit of the 3D systems: what's already modular, the gaps (each filed as a phase work order), and the "how to swap X" cookbook. |
