# Cosmic Engine — Design Proposals

> **Purpose:** forward-looking specs for features **before** they're built — goal, objective, design, and
> implementation details, written down so they can be reviewed and refined ahead of the code.
>
> This is the **proposed-design** counterpart to [`../engineering-notes/`](../engineering-notes/) (which is
> *postmortems* of bugs already fixed) and is distinct from [`../IMPROVEMENTS.md`](../IMPROVEMENTS.md) (a
> prioritized roadmap) and [`../engine_analysis.md`](../engine_analysis.md) (subsystem reference).

## How to add a proposal

Create `docs/design/<short-kebab-title>.md` and add a row below. Each proposal should carry a **Status**
(`Proposed` → `Accepted` → `Implemented` / `Rejected`), a **Targets commit** line so the code traces can be
re-checked, and concrete sections: goal/objective, design + implementation details (with `file:line`
references), planned README/doc updates, verification, and open questions.

## Index

| Proposal | Status | What it covers |
| -------- | ------ | -------------- |
| [responsive-rendering-and-pause.md](responsive-rendering-and-pause.md) | Proposed | Keep rendering while the window is dragged/resized (client-toggleable, default on) via a `WM_TIMER` pump during the Win32 modal loop; plus a first-class `Pause()`/`Resume()` that freezes the sim while the UI/render stay live. |
