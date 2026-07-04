# API Reference — UI & Theming

> **STATUS: SKELETON** — to be filled by work order **D18** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/layers/ImGuiLayer.h`,
`layers/WorkspaceLayer.h` *(client-reachable via `Application::GetWorkspaceLayer()`)*,
`ui/Fonts.h`, `ui/Overlay.h`, `ui/Theme.h`, `ui/ThemeManager.h`, `ui/IconsLucide.h`,
`ui/Widgets.h`, `ui/PlotStyle.h`, plus `Cosmic.h`'s `SetImGuiTheme` helpers.

**Read first:** root README §28 (ImGui overlay & image helpers), §29 (viewport visibility),
§24 (window system / dock slots); systems explainer [ui-theming](../systems/ui-theming.md).

## Coverage checklist *(starting point — headers are authoritative)*

- [ ] `ImGuiLayer` — `SetTheme(enum)` / `SetTheme(name)`, `BlockEvents` behavior (viewport-hover pass-through), image helpers
- [ ] `WorkspaceLayer` client surface — **`DockWindow(name, DockPort::…)` port mode** (preferred; legacy magic names `"Project Inspector Top/Mid/Bottom"` work without bindings), `RequestExtraDockedPanel`, **`BeginViewportOverlay` / `IsViewportHovered`** (screen-px mouse contract), dock reset behavior — do **not** document captured dock-node ids (explicitly unsupported)
- [ ] `ThemeManager` — `All()` enumeration, register client themes, data-driven theme model, Theme Studio interplay
- [ ] `Theme.h` — the theme data struct fields
- [ ] `Fonts` — registry: default Roboto, size/weight variants, `Get` API, Lucide icon font merge
- [ ] `IconsLucide.h` — usage pattern (`ICON_LC_*` in strings), a "how to find an icon" pointer
- [ ] `Overlay.h` — viewport-space text/draw helpers (header-only, needs ImGui context)
- [ ] `Widgets` — every custom widget (enumerate `Widgets.h`: toggles, headers, value displays…)
- [ ] `PlotStyle` — ImPlot styling helpers, themed plot colors

## Sections to write

1. Docking model intro + `DockPort` table (which port = which screen region), with the one-screenshot-worth Mermaid sketch. <!-- TODO(D18) -->
2. Entries per checklist. <!-- TODO(D18) -->
3. ImGui context rule box: `InitializePluginContexts` requirement across the DLL boundary (crash otherwise — README §1). <!-- TODO(D18) -->

---
*Changelog:*
