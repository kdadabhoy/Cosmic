# Build System & Plugin Architecture — How It Works

> **STATUS: SKELETON** — to be filled by work order **D34** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** one CMake tree builds the engine DLL, a host exe, and every folder under
`Projects/` as a plugin DLL the engine loads at runtime — the same pipeline packages a
single app into a self-contained folder or an Inno Setup installer.
**Source:** root `CMakeLists.txt`, `Cosmic/CMakeLists.txt`, `Runtime/Main.cpp`, `*.bat`, `installer/CosmicSetup.iss`
**API Reference:** README §1.5 (command reference — the CLI contract lives there) · **Guide:** root README §40, [`../installer-guide.md`](../installer-guide.md)

## Section plan

1. **Overview** — why plugins: iterate project code without rebuilding the engine; everything under `Projects/` is auto-discovered. <!-- TODO(D34) -->
2. **Mental model** — diagram **DG-5** (DLL lifecycle sequence: Launcher scan → `LoadLibrary` → `InitializePluginContexts` → `CreatePluginLayer` → hooks → Safe-Zone teardown → `FreeLibrary`), plus the delete-before-FreeLibrary ordering rule and why violating it crashes. <!-- TODO(D34) -->
3. **Step-by-step** — `build.bat` → CMake targets → `build/Runtime/Debug/` layout; launcher project scan order (`projects/` next to exe, then exe dir); `--project` direct boot with fallback. <!-- TODO(D34) -->
4. **Technical implementation** — `COSMIC_API` export/import, the shared-`Cosmic.dll` allocator requirement (README §2 double-free warning), engine GLOB without `CONFIGURE_DEPENDS` (the "reconfigure after adding engine files" rule) vs project globs with it, `COSMIC_BUILD_ENGINE_ONLY` cache flipping, Release-is-distribution (console-less, launcher New-Project disabled, /O2), packaging: `cmake --install` staging → `dist/<Name>` prune → zip → Inno (`Version.h` read), `COSMIC_SDK` env var = build-time only, runtime paths exe-relative. <!-- TODO(D34) -->
5. **Design decisions** — plugin DLLs over a monolithic exe; scripts as thin wrappers (and the AI/non-interactive cmake recipe in roadmap §"Working agreement" — link, don't duplicate). <!-- TODO(D34) -->
6. **Limits & future work** — parked release items (CI release job, code signing, `--replay` association — archive 07). <!-- TODO(D34) -->

**Truth sources:** README §31/§40 (migrating here) + §1.5 (stays), `installer-guide.md`,
root/`Cosmic/` CMakeLists, `Runtime/Main.cpp`, doc 06 D2 source list.
