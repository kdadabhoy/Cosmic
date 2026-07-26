# Build System & Plugin Architecture — How It Works

> **STATUS: SKELETON** — to be filled by work order **D34** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** one CMake tree builds the engine DLL, a host exe, and every folder under
`Projects/` as a plugin DLL the engine loads at runtime — the same pipeline packages a
single app into a self-contained folder or an Inno Setup installer.
**Source:** root `CMakeLists.txt`, `Cosmic/CMakeLists.txt`, `CMakePresets.json`, `Runtime/Main.cpp`, `*.bat`, `installer/CosmicSetup.iss`
**API Reference:** README §1.5 (command reference — the CLI contract lives there) · **Guide:** root README §1.6, §40, [`../installer-guide.md`](../installer-guide.md)

> **Sibling document (written, do not duplicate):** [`build-2d-3d-split.md`](build-2d-3d-split.md)
> owns the **two engine configurations** in full — what `COSMIC_2D_ONLY` excludes, the
> classification rule for new files, the presets and worktree layout, the recorded build times, and
> the branch/carry-over workflow. This explainer covers the CMake tree, the plugin-DLL model and
> packaging; where the two meet, it **summarises and links** (the §67 rule for design docs applies
> here too).

## Section plan

1. **Overview** — why plugins: iterate project code without rebuilding the engine; everything under `Projects/` is auto-discovered. <!-- TODO(D34) -->
2. **Mental model** — diagram **DG-5** (DLL lifecycle sequence: Launcher scan → `LoadLibrary` → `InitializePluginContexts` → `CreatePluginLayer` → hooks → Safe-Zone teardown → `FreeLibrary`), plus the delete-before-FreeLibrary ordering rule and why violating it crashes. <!-- TODO(D34) -->
3. **Step-by-step** — `build.bat` → CMake targets → `build/Runtime/Debug/` layout; launcher project scan order (`projects/` next to exe, then exe dir); `--project` direct boot with fallback. **Mention that `build.bat` is now mode-*preserving*** (it reads `COSMIC_2D_ONLY` out of the cache and echoes `[MODE] …`, never forces it) and name the setters `build_2d.bat` / `build_3d.bat` / `build_all_2d.bat` — then link [`build-2d-3d-split.md`](build-2d-3d-split.md) §4.5 for the rest. <!-- TODO(D34) -->
4. **Technical implementation** — `COSMIC_API` export/import, the shared-`Cosmic.dll` allocator requirement (README §2 double-free warning), engine GLOB without `CONFIGURE_DEPENDS` (the "reconfigure after adding engine files" rule) vs project globs with it, `COSMIC_BUILD_ENGINE_ONLY` cache flipping, Release-is-distribution (console-less, launcher New-Project disabled, /O2), packaging: `cmake --install` staging → `dist/<Name>` prune → zip → Inno (`Version.h` read), `COSMIC_SDK` env var = build-time only, runtime paths exe-relative. <!-- TODO(D34) -->
   - **The two-configuration story, from this document's angle** (Phase 29; the *why* lives in [`build-2d-3d-split.md`](build-2d-3d-split.md), the CMake **mechanics** belong here): the engine source GLOB is filtered by a `list(FILTER … EXCLUDE REGEX …)` block, one line per row of the exclusion table; `add_subdirectory` for assimp and recastnavigation is **conditional**, so those 185 TUs are not merely skipped but never configured; `COSMIC_2D_ONLY` is the **only PUBLIC** engine compile definition (via `target_compile_definitions(Cosmic PUBLIC $<$<BOOL:…>:…>`) because public headers change shape under it, and a consumer that disagreed would silently build a different ABI; the project scanner's skip-list is **mode-derived with a staleness guard** (`COSMIC_SKIP_PROJECTS_APPLIED`) because the cache is sticky but the default is not; `Projects/Starforge/CMakeLists.txt` drops three TUs the same way. <!-- TODO(D34) -->
   - **`/MP` belongs in this chapter's parallelism story** — one `add_compile_options(/MP)` at MSVC scope in the root `CMakeLists.txt`, ahead of every `add_subdirectory`. State plainly that `cmake --build --parallel` gives MSBuild parallel *projects*, not parallel *files*, and quote the gotcha: **never** `-DCMAKE_CXX_FLAGS=/MP` (it replaces `/DWIN32 /D_WINDOWS /EHsc` and silently disables exceptions). Numbers live in [`build-2d-3d-split.md`](build-2d-3d-split.md) §4.6 — link, don't restate. <!-- TODO(D34) -->
   - **The build-output collision rule** — `COSMIC_SDK_DIR` is the *source* dir and every target writes to `${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>`, so a second binary dir in one source tree clobbers the first one's `Cosmic.dll`. **Both `CMakePresets.json` presets therefore share one `binaryDir`, and the mechanism for two live configurations is a git worktree.** This is a packaging-relevant fact, not just a preset detail. <!-- TODO(D34) -->
5. **Design decisions** — plugin DLLs over a monolithic exe; scripts as thin wrappers (and the AI/non-interactive cmake recipe in roadmap §"Working agreement" — link, don't duplicate). **Flag-after-partition over deleting 3D code on the 2D branch** (identical tracked files beat a permanent per-file merge conflict) — one paragraph, then link. <!-- TODO(D34) -->
6. **Limits & future work** — parked release items (CI release job, code signing, `--replay` association — archive 07). **Packaging is single-configuration:** `package.bat` / `package_installer.bat` stage whatever the tree is configured as, with no 2D/3D awareness and no way to produce both from one invocation; CI has no 2D leg (doc 28 decision 6). <!-- TODO(D34) -->

**Truth sources:** README §31/§40 (migrating here) + §1.5/§1.6 (stay), `installer-guide.md`,
root/`Cosmic/` CMakeLists, `CMakePresets.json`, `Runtime/Main.cpp`, doc 06 D2 source list,
[`build-2d-3d-split.md`](build-2d-3d-split.md) (authoritative for the split itself).
