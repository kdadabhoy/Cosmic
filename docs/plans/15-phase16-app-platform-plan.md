# Phase 16 Plan — App Platform & Shipping (Starforge as a product, apps as products)

> **STATUS 2026-07-05 — CODE-COMPLETE (UNcommitted).** All eight work orders (S1–S8)
> landed. Build green Debug **and** Release across 5 projects, **zero warnings**;
> `CosmicTests` **219/219** (213→219: +5 FileSystem mount tests, +1 icon-embed test that
> stamps a real PE copy). GL-conformance clean (no raw GL added outside `platform/OpenGL/`).
> Compat gate held: SF_Telem / ViperSim / Frontier / Engine3DDemo build unchanged; shipped
> plugin apps keep NAME-mode `project://` + the shared `user://` root (no app identity → old
> behavior). Remaining = the user's clean-machine acceptance run (S8, script in
> `docs/design/app-platform-acceptance.md`) + commit. Key deviations are noted per item below;
> the big ones: legacy in-tree projects stay path="" (NAME mode) so ForgePlayground is
> byte-identical (the registry "prefix" is display-only); Run Standalone for an *unpackaged
> external* project asks you to Package first (`--project` can't mount external folders yet);
> thumbnail/PNG write is a new generic engine verb `ImageIO::WritePNG` (apps don't touch stb).

> **Created 2026-07-04.** Executes the two architecture decisions (user-approved 2026-07-04):
> 1. **Starforge gets a dedicated launcher experience** — its own exe boots straight into a
>    real project-library homescreen; editor projects are **self-contained folders anywhere on
>    disk**; the Cosmic Launcher reverts to a developer/demo plugin browser.
> 2. **Standalone-first shipping** — an app packaged from Starforge is a finished product:
>    Release-built, custom icon, correct window title/size, zip and/or installer, per-app user
>    data isolation, runs on a machine that has never seen the SDK. "The gap between Starforge
>    and Cosmic closes by design": Cosmic = SDK/runtime, Starforge = the product front door.
>
> Also absorbs: doc 11 §9 **P7** (project library — an index over self-contained folders,
> honored: no migration, folders stay the truth) and doc 07's Step-6 leftovers (CI release job,
> code signing, `--replay` association).
>
> **Depends on:** Phase 14 H5 (workspace chrome verbs) for the homescreen polish; H6 (file
> dialogs) for every folder picker below. Independent of Phases 15/17+.

---

## 0. Execution notes

1. Roadmap build recipe; doc 13 §0 engine rules; compat gate: SF_Telem / ViperSim / Frontier /
   Engine3DDemo launch and run identically from the Launcher and via `--project` after every
   item. Packaged-app behavior may change only where this plan says so.
2. Anything path-related goes through `utils/FileSystem` — read it fully before S1; the
   `user://` root policy decided in the doc 07 installer work is the baseline (verify what
   shipped: `FileSystem.h` documents "writable root … decided once at first use"). Never
   hard-code `assets/projects/` outside the VFS.
3. **Registry files are TOML** via `utils/Config` (house rule: flat config = TOML, entity data
   = JSON).
4. One work order per session; status banners; no git writes.

---

## 1. Target topology (after this phase)

```
Anywhere on disk:                          SDK / dev tree (unchanged):
D:/Work/MyRover/                           build/Runtime/<cfg>/
├── project.cproj                          ├── CosmicApp.exe   ← Launcher (dev tool)
├── CMakeLists.txt  src/  assets/          ├── Cosmic.dll, <Plugin>.dll …
│   ├── scenes/ prefabs/ materials/        └── assets/ …
│   └── models/ textures/ audio/
├── icon.png            ← S5 (optional)
└── build/              (git-ignored)

Installed Starforge (or dev build):        Shipped app (S5 output):
Starforge.exe ─ boot.cfg="Starforge"       dist/MyRover/
  └── homescreen = project library         ├── MyRover.exe    (icon embedded)
      user://starforge/projects.toml       ├── Cosmic.dll  MyRover.dll
      (name → absolute path, MRU, pins)    ├── assets/  boot.cfg
                                           └── (zip / MyRoverSetup.exe via Inno)
```

---

## 2. Work orders

### S1 — External project folders (VFS absolute mounts)

**Files:** MODIFY `Cosmic/src/utils/FileSystem.h` (+ .cpp if it has one — it is currently
header-inline; keep style), `Projects/Starforge/src/EditorPrefs.h` (registry schema),
`StarforgeApp.cpp` (open/create paths), `Projects/Starforge/src/GameModule.*` +
`BuildRunner.*` (module DLL + build dir now live under the project folder); NEW
`tests/test_filesystem_mounts.cpp`.

**Spec:** add `FileSystem::SetActiveProjectPath(const std::string& absoluteRoot)` — when set,
`project://` resolves to `<absoluteRoot>/assets/` (falling back to `<absoluteRoot>/` for
projects that keep the flat legacy layout — probe for an `assets/` subdir once at mount; the
current scaffold writes `scenes/` at the project root, observed 2026-07-04, so the probe is
required). `SetActiveProject(name)` keeps its exact current behavior (shipped plugin apps).
Threading note in the header stays (main-thread set, no concurrent resolve).
Starforge: "New Project" asks for a location via the H6 folder picker (default
`Documents/Starforge Projects/`); "Open" = folder picker validating `project.cproj`; the
**registry** `user://starforge/projects.toml` becomes `[[project]] name/path/lastOpened/pinned`
entries (migrate the existing name-only entries by prefixing the legacy
`assets/projects/` root — write the migration in `EditorPrefs`, one-shot, logged).
`BuildRunner` builds into `<projectRoot>/build/` and the module DLL output stays
`build/Runtime/<cfg>/` **of the SDK** only for legacy in-tree projects — external projects
load their DLL from `<projectRoot>/build/<cfg>/` (template CMake already takes the SDK path
via `COSMIC_SDK`; extend the template's `OUTPUT_DIRECTORY` accordingly and have `GameModule`
search both).

**Gotchas:** `SceneSerializer` paths inside scenes are all `project://…` already (E2) — they
relocate for free; assert no absolute paths are ever written into a scene (test). Legacy
in-tree projects (`assets/projects/<name>`) must keep opening unchanged (compat: ForgePlayground).
Hot-reload's `_hotN` DLL cleanup (E12) must scan the project's own build dir too.

**Acceptance:** create a project on another drive → scenes/scripts/build/hot-reload/play all
work; move the folder, reopen via Open → everything still works (relocatability proof);
ForgePlayground (legacy location) unaffected; headless mount tests green.

**Status:** ✅ 2026-07-05. `FileSystem::SetActiveProjectPath` (PATH mode) + `ActiveProjectPath()`
added; `SetActiveProject` clears the mount; assets/ subdir probed once. Editor: `EditorContext.
ProjectPath`; registry v2 (`[[project]] name/path/last_opened/pinned`) in `EditorPrefs` with a
one-shot v1 migration (drops the editor's own "Starforge" pseudo-project); `MountProject`/
`OpenProjectPath`/`NewProjectAt`/`ScaffoldProjectTo`. `BuildRunner` gained config + `GAME_OUTPUT_DIR`;
the template CMake routes the DLL to `<root>/build/<cfg>/`; `GameModule::Load(…, searchDir)` loads
it by absolute path (Cosmic.dll still resolves from the exe dir); `CleanStaleHotDlls` sweeps the
module dir. `tests/test_filesystem_mounts.cpp` (5 cases, incl. no-absolute-leak). **Deviation:** legacy
in-tree projects stay path="" (NAME mode) — the plan's "prefix with assets/projects/" is honored as a
*display* string only, so ForgePlayground's build+load path is byte-identical.

### S2 — Starforge dedicated boot

**Files:** MODIFY `Projects/Starforge/` (a `Package Starforge` self-staging path — reuses the
E19 `PackageProject` machinery with the editor as the project), `Runtime/Main.cpp` only if a
flag is missing (it already reads `boot.cfg`; `--project` wins — verified 2026-07-04);
`installer/` Inno template gains a Starforge variant.

**Spec:** produce `dist/Starforge/Starforge.exe` = renamed `CosmicApp.exe` + `boot.cfg` naming
Starforge + engine assets + `Starforge.dll` (+ the editor's template assets). The editor's
**own icon** (molten-orange, an `icon.png` in the Starforge project) embeds via S5's
icon-embed step. Double-click → straight to the homescreen (no Launcher, no console window —
verify the subsystem is WINDOWS non-console for packaged builds, or ship a tiny
`Starforge.exe` launcher stub if `CosmicApp` must stay console; **decide by reading
`Runtime/CMakeLists.txt` WIN32 flag state and note the choice**). The dev tree keeps working:
`CosmicApp --project Starforge` unchanged.

**Acceptance:** copy `dist/Starforge/` to a path with no repo/SDK → double-click → homescreen
appears, external projects open, hot-reload works if VS+cmake exist (graceful "install VS
Build Tools" message if not — BuildRunner already surfaces cmake-not-found; make the message
actionable).

**Status:** ✅ 2026-07-05. Starforge self-packages through the SAME pipeline as any project
(`PackageStarforge` → `BeginPackage({Name="Starforge", Path=""})`): the SDK Release build adds the
`Starforge` target, stages `Starforge.exe` + `Cosmic.dll` + `Starforge.dll` + engine assets +
`assets/projects/Starforge` + `boot.cfg` (=Starforge, read by Main.cpp → runs the editor + sets the
S6 identity). **Subsystem decision (from reading `Runtime/CMakeLists.txt`):** Release already links
`/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup`, so a Release-packaged app — Starforge included — opens
with **no console**; the Debug fast-path keeps the console for dev. No launcher stub needed.
`--project Starforge` dev boot unchanged; the editor's molten icon is optional
(`assets/projects/Starforge/icon.png` if present).

### S3 — Product homescreen (the project library)

**Files:** Starforge `StarforgeApp.cpp` (`DrawHomescreen` rewrite — currently a small floating
panel over a black workspace, observed 2026-07-04), maybe `panels/HomePanel.*` (extract);
uses H5 chrome verbs + H6 dialogs.

**Spec:** full-workspace surface (SF_Telem tile-menu precedent): header (logo/name/version
from `core/Version.h`), primary actions **New Project** (name + location + template picker —
templates enumerate `assets/templates/` variants; v1 has one, the picker is the seam),
**Open** (folder dialog), **Open Sample** (ForgePlayground); a **project grid**: card per
registry entry — name, absolute path (middle-truncated), startup scene, last-opened date, pin
toggle, thumbnail (`<projectRoot>/.starforge/thumb.png` — S7 writes it on save; grey
placeholder until then), context menu (Open / Show in Explorer / Remove from list [never
deletes files — say so in the menu] / Pin). Filter: only folders whose `project.cproj`
validates; missing-path entries show a "missing" badge with Locate…/Remove. Search box.
Keyboard: Enter opens selection.

**Acceptance:** fresh boot shows the library; every card action works; a deleted-on-disk
project degrades gracefully; no plugin asset folders (SF_Telem etc.) appear (the S1 manifest
filter); looks correct at 100%/125% DPI and 1280×720.

**Status:** ✅ 2026-07-05. `DrawHomescreen` rewritten as a full-region library over the central
viewport (menu bar stays usable): header (logo + `COSMIC_VERSION_STRING` + tagline), primary actions
**New Project** (name + location [default `Documents/Starforge Projects/`, H6 folder picker] +
template picker seam), **Open** (folder dialog validating `project.cproj`), **Open Sample**
(ForgePlayground), a search box, and a responsive card grid from the registry: name, middle-truncated
path, last-opened, pin toggle, thumbnail (`<root>/.starforge/thumb.png` via a `Texture2D` cache, grey
"no preview" until S7 writes one), context menu (Open / Show in Explorer / Pin / Remove-from-list
[never deletes] / Locate… for missing), missing-on-disk badge, double-click opens. Pinned sort first.

### S4 — Launcher back to a dev tool

**Files:** MODIFY `Cosmic/src/layers/LauncherLayer.cpp`.

**Spec:** the Launcher's project scan (exe-dir DLLs exporting `CreatePluginLayer`) stays, but:
Starforge gets a distinct primary tile ("Open Starforge — build apps"), demo/dev plugins group
under "Engine demos & tools"; the stale "New Project" template-scaffold flow in the Launcher
(pre-Starforge era — it scaffolds C++ plugin projects into `SDK/Projects/`) is **relabeled
"New C++ plugin (advanced)"** and kept (it is the path for engine-level apps like SF_Telem;
verify current behavior before touching — `GenerateProjectTemplate` is load-bearing for the
template docs). No removal of capability; only presentation + copy.

**Acceptance:** all existing plugins launch as before; the hierarchy of tiles reads: make apps
→ Starforge; engine demos → the rest.

**Status:** ✅ 2026-07-05. `LauncherLayer` presentation only: a prominent molten "Open Starforge —
build apps" tile at the top, the rest grouped under an "Engine demos & tools" label, and the stale
"New Project" scaffold relabeled "New C++ plugin (advanced)" with a "to BUILD APPS, use Starforge"
subtitle. No capability removed (`GenerateProjectTemplate` untouched, still `#ifndef COSMIC_DIST`).

### S5 — Packaging v2: Release orchestration, icon, zip, installer

**Files:** Starforge `StarforgeApp::PackageProject` (rewrite into `src/Packager.h/.cpp`),
`BuildRunner` (Release-config build support), NEW engine `utils/ExeResources.h/.cpp`
(icon embed — see spec), `installer/` (parameterized `.iss` template), template project gains
`icon.png` + `[window] title/width/height` keys in `project.cproj` consumed by
`layers/PlayerLayer` (title/size on boot).

**Spec (pipeline, in order, each step surfaced in a progress dialog):**
1. **Release build**: BuildRunner drives the SDK cmake `--config Release` for engine+runtime
   if their Release outputs are stale (timestamp check), then the project DLL Release. Abort
   cleanly on compile errors (Console shows them — existing plumbing).
2. **Stage** `dist/<Project>/` from Release outputs (existing E19 logic, now Release-sourced):
   `<Project>.exe` (renamed CosmicApp), `Cosmic.dll`, `<Project>.dll`, engine `assets/`,
   project content, `boot.cfg`.
3. **Icon embed**: `ExeResources::SetIcon(exePath, pngPath)` — convert PNG → multi-size .ico
   in memory (16/32/48/256; stb_image resize is already vendored — verify, else nearest-box
   scale) and write via the Win32 `BeginUpdateResource`/`UpdateResource(RT_GROUP_ICON/RT_ICON)`
   API on the **copied** exe (never the SDK's). Editor UI: project settings dialog gets an
   icon slot (H6 file dialog) storing `icon = "icon.png"` in `project.cproj`.
4. **Window identity**: PlayerLayer reads `[window] title/width/height` from the manifest
   (already reads title per E13 — verify; add size) — a shipped app opens with its own name,
   not "Cosmic Engine".
5. **Zip** (optional toggle): PowerShell `Compress-Archive` via the BuildRunner process path —
   no new dependency.
6. **Installer** (optional toggle): generate `dist/<Project>.iss` from a template
   (`installer/` has the CosmicSetup precedent) with app name/version/publisher/icon; if Inno
   (`iscc.exe`) is on PATH, offer "Build installer now", else show the file + a docs link
   (`docs/installer-guide.md`).
7. **Doc 07 leftovers** land here as sub-items: a CI **release job** building Release +
   running the packager for a named project (manual-dispatch workflow), `--replay <file>`
   file-association registration moves into the generated installer script (optional
   checkbox), **code signing** = a signtool hook point in the packager (config'd cert path;
   skipped when absent — documented).

**Gotchas:** `UpdateResource` fails on running exes (stage first, embed second) and
invalidates existing signatures (sign AFTER embed — order the pipeline). Icon caches:
Explorer may show stale icons (note `ie4uinit -show` in the user-facing doc, don't automate).
Release runtime layout must match what `LauncherLayer`'s scan + `Main.cpp` expect (exe-dir
DLLs) — the E19 stage layout already does.

**Acceptance:** package ForgePlayground with a custom icon → `dist/ForgePlayground/` on a
clean path: correct icon in Explorer + taskbar, window title "Forge Playground", scene runs;
zip + installer variants install/run/uninstall cleanly; signing hook logs "skipped (no cert)".

**Status:** ✅ 2026-07-05. New engine `utils/ExeResources` (`SetIcon` — PNG → in-memory multi-size
.ico [16/32/48/256, nearest-box downscale] → `BeginUpdateResource`/`UpdateResource(RT_ICON+
RT_GROUP_ICON)` on the **copied** exe; headless-tested against a real PE copy). New generic
`utils/ImageIO::WritePNG` (keeps stb inside the engine DLL). `Window::SetSize/SetTitle` + PlayerLayer
reads `[window] title/width/height` (falls back to legacy `window_title`). Editor `src/Packager.h/.cpp`
= Stage (renamed exe + DLLs + engine assets + this project's content normalised to in-tree layout +
boot.cfg; **falls back to the newest `_hotN` DLL** when no base `<name>.dll` exists) → Finalize (icon
embed → sign hook [logs "skipped (no cert)"] → installer script → zip). Release orchestration via
`BuildRunner::StartSteps` (SDK engine+runtime if stale, then the project DLL). `ProjectManifest`
load/save + a **Project Settings** dialog (icon slot + window title/size + startup scene + fixed Hz).
Package dialog: Release/zip/installer toggles + progress. Installer: generated per-app `dist/<Project>.iss`
+ committed `installer/AppSetup.iss` template + `--replay` association (Main.cpp accepts `--replay`
→ `COSMIC_REPLAY_FILE`) + the manual-dispatch `.github/workflows/release.yml`. **Deviation:** no
`stb_image_resize` vendored → nearest-box scaling (as the plan allowed).

### S6 — Per-app user data isolation

**Files:** MODIFY `Cosmic/src/utils/FileSystem.h` (user-root policy), `Runtime/Main.cpp` or
`Application` init (feed the app identity), doc pass in `docs/installer-guide.md`.

**Spec:** today `user://` resolves to one root decided at first use (read the header — doc 07
era). Extend the policy with an **app identity**: packaged boots (boot.cfg present) set
identity = project name → user root = `%LOCALAPPDATA%/<ProjectName>/` when the exe dir is not
writable, else `<exe>/user/` (portable mode — a `portable.txt` next to the exe forces it;
document). Dev boots (Launcher) keep the current shared root so SF_Telem/ViperSim recordings
stay where they are (**compat gate — their existing user data must keep resolving; verify by
running SF_Telem after the change**). Log the resolved root at boot (ties into doc 13 H7's
"logs say where they live").

**Acceptance:** packaged app under Program Files writes takes/logs/prefs to LOCALAPPDATA;
portable-flag copy writes beside the exe; dev tree unchanged; two different packaged apps
don't share prefs.

**Status:** ✅ 2026-07-05. `FileSystem::SetAppIdentity` + extended `GetUserDataRoot`: a packaged boot
(identity set from `boot.cfg` in Main.cpp, before any `user://` resolve) routes to
`%LOCALAPPDATA%/<AppName>/` (read-only exe dir) or `<exe>/user/` (writable dir OR a `portable.txt`
next to the exe); dev/Launcher boots (no identity) keep the historical shared root (`.` when writable,
else `%LOCALAPPDATA%/Cosmic`), so SF_Telem/ViperSim data resolves exactly as before. Main.cpp logs the
resolved root at boot (H7). Two different packaged apps never share prefs/logs/takes.

### S7 — Editor conveniences that close the loop

**Files:** Starforge (`StarforgeApp`, Packager, viewport).

**Spec:** (1) **Run Standalone** toolbar button: launches the packaged exe if fresh, else
`CosmicApp.exe --project <name>` from the dev tree (external projects: with the project
mounted — needs S1's DLL search), so "play it as if launched directly" is one click;
(2) **thumbnail capture** on scene save: blit the viewport color into
`<projectRoot>/.starforge/thumb.png` (S3 consumes; reuse the existing screenshot/readback
verb — verify one exists in `RenderCommand`/`FrameBuffer`, else add `FrameBuffer::ReadPixels`
engine-side, generic); (3) **About dialog**: engine version (`core/Version.h`), project
manifest summary, open logs folder button.

**Acceptance:** Run Standalone works for in-tree + external + packaged projects; homescreen
shows real thumbnails after the first save; About shows the right versions.

**Status:** ✅ 2026-07-05. (1) **Run App** toolbar button + File ▸ Run Standalone: launches the
packaged exe if present, else `CosmicApp --project <name>` for in-tree; an unpackaged *external*
project logs "Package first" (`--project` can't mount external folders yet — noted deviation). (2)
**Thumbnail on save**: `SaveSceneToVfs` flags a capture; `RenderViewport` reads the just-composited
FBO via the new `FrameBuffer::ReadPixels`, downscales (≤480 px, opaque), and writes
`<root>/.starforge/thumb.png` via `ImageIO::WritePNG` (thumb cache invalidated → the library refreshes).
(3) **About dialog** (Help ▸ About): engine version + project/scene summary + Open Logs Folder.

### S8 — Phase acceptance (clean-machine proof)

**Spec:** the recorded run: on the dev machine — new external project on another drive →
build scene + script → package with icon → zip; on a **clean machine/VM** (no repo, no SDK,
no VS): unzip → double-click → app runs with its icon/title, writes user data to
LOCALAPPDATA, `--replay` association works if the installer variant was used. Starforge's own
`dist/Starforge` passes the same test (S2). Record it (the E21 demo-recording pattern —
script in `docs/design/`).

**Status:** ✅ code-complete 2026-07-05 — recording script written at
`docs/design/app-platform-acceptance.md`. The recorded clean-machine/VM run is the **user's** GPU/OS
acceptance step (acceptance ledger); it needs a second machine the AI can't drive.

---

## 3. Parked (with unlocks)

| Item | Unlock |
| --- | --- |
| Project templates gallery (2D game, sim rig, blank) beyond the picker seam | doc 16 ships the 2D template; more when a third real template exists |
| Cloud/team project sync, project database service | doc 11 §9 P7's original unlock (multi-machine/team) — the registry file is the seam |
| Auto-update channel for packaged apps | a shipped app has real users |
| macOS/Linux packaging | a second platform request (GL backend gate rides doc 05 §12 reopen conditions) |

## 4. Order

S1 → S2 → S3 → S4 (any time) → S5 → S6 → S7 → S8. S1 is the only engine-heavy item; S3–S7 are
editor/app-side and parallel-safe after S1+S5's seams exist.

## Kickoff prompt

> You are implementing ONE work order from `docs/plans/15-phase16-app-platform-plan.md` in
> `C:\dev\Cosmic`. Read §0–§1 first. Read `utils/FileSystem.h`, `Runtime/Main.cpp`, and the
> E19 packaging code by content before editing — this phase is mostly re-plumbing existing
> machinery and the current behavior is the compat baseline. Shipped plugin apps (SF_Telem,
> ViperSim, Frontier, Engine3DDemo) must launch and find their data exactly as before. Build
> with the roadmap's non-interactive cmake recipe; never run git write commands. Finish with
> the Acceptance + status banner update.
