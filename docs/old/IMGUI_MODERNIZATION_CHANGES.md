# ImGui Modernization — Change Map

A focused index of every file created/modified during the ImGui visual
modernization effort (2026-06). Use this to know which files to read first when
analyzing or extending the UI/theming/windowing system.

Paths are relative to the repo root (`C:\dev\Cosmic`).

---

## 1. Theme system (data-driven, engine-owned, runtime-selectable)

**New**
- `Cosmic/src/ui/Theme.h` — `struct Theme { name, accent, ImVec4 colors[ImGuiCol_COUNT], ThemeStyle style, builtIn }` and `struct ThemeStyle` (rounding/padding/border/spacing knobs). Plain data; a theme carries a FULL colour table so switching is deterministic.
- `Cosmic/src/ui/ThemeManager.h` / `.cpp` — engine DLL singleton registry (storage in `.cpp`). API: `Init`, `Register`, `Apply(name)`, `ApplyTheme(const Theme&)` (live preview, no register), `CurrentName`, `Accent`, `All`, `Find`, `CaptureCurrentStyle(name)`, `SaveToFile` / `LoadFromFile` / `LoadFolder` (`.ctheme` text format). `Apply` also calls `UI::ApplyPlotStyle`.

**Modified**
- `Cosmic/src/layers/ImGuiThemes.h` — REWRITTEN. Was enum + `std::map<enum, fn>` registry; now `BuildXxx()` data builders returning `Theme`. `SeedDark/SeedLight` fill the full table, then overrides. Keeps the 8 original themes; adds 3 new (`BuildSleekPro`, `BuildNeonHUD`, `BuildCleanFlat`). `GetBuiltInThemes()` (Sleek Pro first = default) + `NameForTheme(ImGuiTheme)` legacy-enum→name map. Enum `ImGuiTheme` retained for back-compat.
- `Cosmic/src/layers/ImGuiLayer.h` / `.cpp` — `SetTheme(enum)` now maps to name; added `SetTheme(const std::string&)`. `OnAttach` calls `ThemeManager::Init()` then `SetTheme("Sleek Pro")`.
- `Cosmic/src/layers/LauncherLayer.cpp` — default theme call changed to `"Sleek Pro"`.
- `Cosmic/src/Cosmic.h` — includes new UI headers; adds `SetImGuiTheme(const std::string&)`.

---

## 2. Fonts + icons

**New**
- `Cosmic/src/ui/IconsLucide.h` — GENERATED (do not hand-edit). 1986 `ICON_LC_*` UTF-8 macros + `ICON_MIN_LC`/`ICON_MAX_LC` (0xe038–0xe715), from `lucide-static` `font/info.json` via a Node script.
- `Cosmic/assets/fonts/lucide.ttf` — Lucide icon font (ISC), auto-copied to runtime by CMake.

**Modified**
- `Cosmic/src/ui/Fonts.cpp` — `Init()` now sets `io.FontDefault = Roboto-Regular` (was ImGui's bitmap ProggyClean — the main "looks dated" cause). Merges `lucide.ttf` into each text face via `ImFontConfig{MergeMode}` + glyph range; icon font excluded from the selectable face list. Added `s_IconPath`/`s_HasIcons`, `IsIconFontStem`, `MergeIconsInto`.
- `Cosmic/src/ui/Fonts.h` — added `HasIcons()` and size constants `SizeSmall/SizeBody/SizeHeading/SizeBig`.

---

## 3. Reusable widgets + ImPlot styling

**New**
- `Cosmic/src/ui/Widgets.h` / `.cpp` — COSMIC_API widgets, accent from `ThemeManager::Accent()`:
  `StatCard`, `ToggleSwitch` (animated), `SectionHeader`, `IconButton`, `AccentButton`, `ThemeSelector` (engine theme picker list), `WindowControls` (min/max/close wired to the Window — used by custom title bars).
- `Cosmic/src/ui/PlotStyle.h` / `.cpp` — `ApplyPlotStyle(const Theme&)` syncs ImPlot frame/grid/axis/legend colours to the theme. NOTE: vendored ImPlot is **v1.0** — `ImPlotStyle` has NO `LineWeight`/`FillAlpha`, and `SetNextLineStyle`/`SetNextFillStyle` are obsoleted (per-item via `ImPlotSpec`). Item/line colours come from the colormap.

**Modified**
- `Projects/SF_Telem/src/StatBox.h` — reimplemented on `UI::StatCard`; `BigValue` now uses a real bold face at size (fixes the blurry `SetWindowFontScale` numbers).

---

## 4. Docking + engine theme selector (WorkspaceLayer)

**Modified**
- `Cosmic/src/layers/WorkspaceLayer.h` / `.cpp`
  - `ShowThemeSelector(bool, DockPort, name)` — engine hosts a dockable "Themes" window rendering `UI::ThemeSelector()`; client places it in one call.
  - View menu: "Show Viewport" + "Theme Selector" toggles.
  - Custom title bar: `RenderMenuBar` now ends with `UI::WindowControls()` (replaced the old Exit button; ✕ = quit) and computes `m_TitlebarDrag` (top band && `!IsAnyItemHovered/Active`).
  - `OnAttach`/`OnDetach` register/clear the window's titlebar hit-test callback.
  - Docking note: `DockWindow(name, DockPort)` accepts ARBITRARY names (the "Project Inspector Top/Mid/Bottom" magic names are legacy-mode only; any DockWindow binding switches to port mode). `SetViewportVisible(false)` hides the empty Viewport tab.

---

## 5. Borderless custom window chrome + fullscreen rework (Win32)

**Modified**
- `Cosmic/src/core/Window.h` / `.cpp`
  - File-static `CosmicWndProc` + `CosmicHitTest` (in `Window.cpp`, `#ifdef _WIN32`): subclass keeps `WS_OVERLAPPEDWINDOW`; `WM_NCCALCSIZE` removes the visual frame (insets when maximized so the taskbar shows); `WM_NCHITTEST` does resize borders + `HTCAPTION` drag. Window* stored via `SetPropW(L"CosmicWindowPtr")` (NOT `GWLP_USERDATA` — GLFW owns that); chains GLFW's proc via `CallWindowProcW`. `DwmExtendFrameIntoClientArea` for the shadow.
  - New API: `Minimize/Maximize/Restore/ToggleMaximize`, `IsWindowMaximized()` (named so to avoid the `<windows.h>` `IsMaximized` macro), `Close()`, `SetCustomChrome(bool)` (ON by default on Windows; `false` = escape hatch to the OS frame), `HasCustomChrome`, `SetTitlebarHitTestCallback`/`ClearTitlebarHitTestCallback`, accessors `TitlebarHitTest`/`NativeOrigWndProc`. New members `m_CustomChrome`, `m_TitlebarHit`, `m_OrigWndProc`.
  - Fullscreen reworked: with chrome on, fullscreen just resizes to the monitor (no style-bit stripping / flicker). F11 hotkey unchanged.
  - **Crash fix (instant-load crash):** `m_Data.EventCallback` defaults to a no-op lambda in the constructor. Enabling chrome fires a `WM_SIZE` during construction → GLFW size callback → `EventCallback`, which was empty until `Application` set it afterwards (threw `bad_function_call`). The no-op default makes early callbacks safe.
- `Cosmic/src/layers/LauncherLayer.h` / `.cpp` — added a custom borderless title bar (app name + `UI::WindowControls()`) and `m_TitlebarDrag`; registers/clears the titlebar hit-test callback. (Both hosts need controls since there's no OS frame.)

**Build**
- `Cosmic/CMakeLists.txt` — links `dwmapi` on Windows (for `DwmExtendFrameIntoClientArea`).

---

## 6. Template project (showcase + docking demo)

**New**
- `Cosmic/templates/ExampleProject/src/TemplateThemeShowcaseLayer.h` / `.cpp` — "Theme Studio" mode: theme picker, live editor (`ColorEdit` swatch+label rows; "Save as new theme" → `CaptureCurrentStyle` + `SaveToFile(project://themes)` + `Register`), and a widget/plot preview gallery. Defines `THEME_STUDIO_WINDOW` macro (icon + name) used by both `Begin` and `DockWindow`.

**Modified**
- `Cosmic/templates/ExampleProject/src/TemplateProject.cpp` — registers the showcase as a mode; in `OnAttach` uses port-mode `DockWindow` (arbitrary names) incl. Theme Studio → `DockPort::Center`, and `ShowThemeSelector(true, RightTop, "Themes")`. (Both engine and template CMake glob sources — no CMake edits needed for new files.)

---

## Quick reference — key entry points

- Apply a theme: `Cosmic::ThemeManager::Apply("Sleek Pro")` or `Cosmic::SetImGuiTheme("...")` or `ImGuiLayer::SetTheme(...)`.
- List themes: `Cosmic::ThemeManager::All()`. Accent: `Cosmic::ThemeManager::Accent()`.
- Icons: `ICON_LC_*` (see `IconsLucide.h`); render inline with any text face. `Fonts::HasIcons()` gates availability.
- Dockable theme picker: `Application::Get().GetWorkspaceLayer()->ShowThemeSelector(true, DockPort::RightTop)`.
- Dock a window: `ws->DockWindow("MyWindow", Cosmic::DockPort::LeftTop)` (names arbitrary).
- Disable borderless chrome (fallback): `Application::Get().GetWindow().SetCustomChrome(false)`.

## 7. Post-integration fixes (after first runs)

- **Instant-load crash fix** — `Cosmic/src/core/Window.cpp`: `m_Data.EventCallback` defaults to a no-op lambda in the ctor. Enabling chrome fires a `WM_SIZE` during construction → GLFW size callback → `EventCallback`, which `Application` only sets *after* construction (was throwing `bad_function_call`).
- **`IsMaximized` rename** — `<windows.h>` defines `IsMaximized` as a macro (→`IsZoomed`); the engine method is now `Window::IsWindowMaximized()` (Window.h/.cpp, Widgets.cpp).
- **Fullscreen taskbar + title bar** — `Window.cpp` fullscreen now always uses the style-strip path (removes `WS_OVERLAPPEDWINDOW`) so the shell hides the taskbar; `WorkspaceLayer.cpp` hides the menu/title bar when `IsFullscreen()`.
- **Generated projects are copies** — `Projects/MyProject/` (and any generated project) is a *copy* of the template with the root renamed (`TemplateProject`→`MyProject`). Engine/UI-library changes flow via the DLL, but template *layer* code (showcase, root `OnAttach`) does NOT auto-update. MyProject's `TemplateThemeShowcaseLayer.*` + `MyProject.cpp` were patched directly (color-name `NoInputs` editor, `THEME_STUDIO_WINDOW` macro, port-mode `DockWindow` + `ShowThemeSelector`).
- **SF_Telem** (`Projects/SF_Telem/`):
  - `TelemHub.cpp` `PinInput` and `SF_Telem.cpp` `PolesInput` — widened the `InputInt` item width (computed from `GetFrameHeight()`), because `InputInt`'s -/+ step buttons sit inside the item width and the modern themes' larger `FramePadding` had squeezed the digits to ~1 visible character.
  - `SF_Telem.cpp` `ApplyDockLayout` — added `ShowThemeSelector(true, port, ICON_LC_PALETTE "  Themes")` per screen (re-registered after `ClearDockWindows`).
  - `SF_Telem.cpp` `DrawTopPanel` — screen-selector buttons now carry Lucide icons (Gauge / Car / Swords).

## Known risk / watch areas
- **Win32 chrome** (`Window.cpp`): shadow, edge-resize, Aero Snap, and maximized-vs-taskbar are the untested edge cases. `SetCustomChrome(false)` reverts to the OS frame.
- **ImPlot v1.0** API differs from common docs (no `LineWeight`/`FillAlpha` on `ImPlotStyle`; no `SetNextLineStyle`). Style series via colormap / `ImPlotSpec`.
- **DLL boundary**: `ThemeManager`/`Widgets`/`PlotStyle`/`Fonts` are `COSMIC_API` and live in the engine DLL; clients call across the boundary — keep registry storage in the engine `.cpp`, not header-inline statics.
