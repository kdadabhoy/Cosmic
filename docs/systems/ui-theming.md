# UI & Theming — How It Works

> **STATUS: SKELETON** — to be filled by work order **D34** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** Dear ImGui renders every panel; the engine adds a workspace shell with
dockable ports, a data-driven theme system (with a live Theme Studio), a font/icon registry
(Roboto + Lucide), and reusable widgets — all shared across the DLL boundary via synced
contexts.
**Source:** `Cosmic/src/layers/ImGuiLayer.*`, `layers/WorkspaceLayer.*`, `layers/LauncherLayer.*`, `Cosmic/src/ui/*`
**API Reference:** [../reference/ui.md](../reference/ui.md) · **Guide:** root README §28, §29, §24

## Section plan

1. **Overview** — immediate-mode UI in one paragraph (no widget objects — you redeclare the UI every frame), and what the engine layers on top. <!-- TODO(D34) -->
2. **Mental model** — the shell: Launcher screen vs Workspace (viewport + inspector sidebar + docked ports); `DockPort` region map sketch. <!-- TODO(D34) -->
3. **Step-by-step** — a project panel from `ImGui::Begin("…")` to pixels, incl. port-mode `DockWindow(name, DockPort::…)` vs legacy magic names, and the viewport-hover event pass-through. <!-- TODO(D34) -->
4. **Technical implementation** — ImGui/ImPlot context sync across DLLs (`InitializePluginContexts` — why omitting it crashes), dock-layout build/reset (never persist captured dock-node ids — the documented rule), `ThemeManager` data model + registration + Theme Studio flow, font pipeline (Roboto default, Lucide merge, DPI scaling), `BeginViewportOverlay`/`IsViewportHovered` screen-px mouse contract, `Widgets`/`PlotStyle` catalog. <!-- TODO(D34) -->
5. **Design decisions** — WS-series modernization record (data-driven themes, port docking, borderless chrome — link memory/plan records in `docs/plans/archive/` if present). <!-- TODO(D34) -->
6. **Limits & future work.** <!-- TODO(D34) -->

**Truth sources:** `WorkspaceLayer.cpp` (dock building), `ThemeManager.cpp`, README §24/§28/§29
(client-facing bits stay in README; internals migrate here).
