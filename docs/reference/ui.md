# API Reference — UI & Theming

> **STATUS: SKELETON** — to be filled by work order **D18** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/layers/ImGuiLayer.h`,
`layers/WorkspaceLayer.h` *(client-reachable via `Application::GetWorkspaceLayer()`)*,
`ui/Fonts.h`, `ui/Overlay.h`, `ui/Theme.h`, `ui/ThemeManager.h`, `ui/IconsLucide.h`,
`ui/Widgets.h`, `ui/PlotStyle.h`, plus `Cosmic.h`'s `SetImGuiTheme` helpers.

**Read first:** the client guide chapter
[`../guide/editor-ui-and-theming.md`](../guide/editor-ui-and-theming.md) — `ImGuiLayer`, the docking
model and every `DockPort`, the **never-store-a-dock-node-id** rule, `SetBottomInsetPixels`,
viewport overlays, `ThemeManager` + the Theme Studio, fonts and Lucide icons, `Widgets`, `PlotStyle`
and `Overlay`. It **replaces root README §27, §28 and §28.5** and the docking half of §29 (whose
bodies are now overviews pointing there). The viewport/window half of §29 and all of §24 went to
[`../guide/windowing-and-viewport.md`](../guide/windowing-and-viewport.md). Systems explainer:
[ui-theming](../systems/ui-theming.md) *(still a skeleton — D34)*.

> **Don't re-derive the guide chapter.** D60 wrote the docking model, the port table, the theme data
> model, the `.ctheme` format, the font/icon pipeline and the whole widget catalogue from source.
> This chapter is the per-call lookup; link it rather than restating it.

**Two manifest gaps found by D60**, both routing to this chapter: `layers/ImGuiThemes.h` (reachable
through `layers/ImGuiLayer.h`, and the home of `enum class ImGuiTheme` — the parameter type of the
exported `ImGuiLayer::SetTheme` / `Cosmic::SetImGuiTheme` overloads — plus `GetBuiltInThemes()` and
`NameForTheme()`) has no row, and `utils/Branding.h` has none either (`COSMIC_API`, called from a
project DLL, but never included by `Cosmic.h`; it belongs with [assets-io.md](assets-io.md)). Line 10
below also names a `SetImGuiTheme` helper "in `Cosmic.h`" — that is correct: both overloads are
inline free functions at `Cosmic.h:220-231`.

> **Scope boundary.** This chapter is **ImGui editor/tool chrome only**. The engine's *other* UI
> system — in-game menus and HUDs built from scene entities (`CanvasComponent`,
> `RectTransformComponent`, `UiImage`/`UiText`/`UiButton`, `UiWorldAnchorComponent`,
> `scene/ui/UiSystem.h`) — is unrelated and belongs to `scene/ui/`, which has **no manifest row at
> all** (D52). Until D5 adds one, its client-facing source is the guide chapter
> [`../guide/game-ui.md`](../guide/game-ui.md). Do not document it here.

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
3. ImGui context rule box: `InitializePluginContexts` requirement across the DLL boundary (crash otherwise — [`../guide/getting-started.md`](../guide/getting-started.md)). <!-- TODO(D18) -->

---
*Changelog:*
