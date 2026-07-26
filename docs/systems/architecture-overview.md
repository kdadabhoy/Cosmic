# Architecture Overview — How It Works

> **STATUS: SKELETON** — to be filled by work order **D25** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** Cosmic is a C++20/OpenGL engine that loads your project as a hot-swappable
DLL plugin and gives it layers, renderers (2D + 3D), an ECS, and simulation/telemetry tooling.
**Source:** `Cosmic/src/` (engine DLL) · `Runtime/Main.cpp` (host exe) · `Projects/*` (plugins)
**API Reference:** [../reference/README.md](../reference/README.md) · **Guide:** [../guide/getting-started.md](../guide/getting-started.md)

## Section plan *(the map document — keep each section short, link the territory)*

1. **Overview** — what Cosmic is, the three module tiers (host exe → engine DLL → project DLLs), what "engine ships generic verbs; apps own domain logic" means in practice. <!-- TODO(D25) -->
2. **Mental model** — diagram **DG-1** (module block diagram) + **DG-2** (core class diagram).
   **DG-1 was built by D46** in [`../guide/getting-started.md`](../guide/getting-started.md#dg-1--how-the-pieces-fit) — reuse that source, don't re-derive it. <!-- TODO(D25) -->
3. **One frame, end to end** — condensed walk: PollEvents → fixed pass → variable pass → 3D queue flush → SceneRenderer passes → post → ImGui → swap → Safe Zone; diagram **DG-3**. Every stop links its explainer. <!-- TODO(D25) -->
4. **The plugin boundary** — DLL lifecycle diagram **DG-5**, ownership rules, `HostContext`. <!-- TODO(D25) -->
5. **Directory tour** — table: every `Cosmic/src/` folder → one sentence → its explainer + reference chapter. <!-- TODO(D25) -->
6. **Design decisions** — OpenGL-stays decision (S13.3, link `../design/frame-lifecycle.md`), single-threaded GL, doctest/headless testing split. <!-- TODO(D25) -->

**Truth sources for the writer:** root README §30 (source file map) and §31 (DLL
architecture) — being superseded by this doc, mine them; `00-MASTER-ROADMAP.md` "one design
rule"; `Cosmic/src/Cosmic.h` include list (the public surface).
