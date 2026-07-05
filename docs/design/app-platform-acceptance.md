# Phase 16 acceptance — app platform & shipping (S8)

The recorded proof that a Starforge project becomes a finished product that runs on a
machine that has never seen the SDK. This is a **user-run** step (it needs a second,
clean machine or VM the AI can't drive). Record it as a screen capture + keep the
`dist/` artifact.

> Prereqs on the dev machine: VS 2022 + the bundled CMake (the roadmap build recipe),
> and — for the installer legs — Inno Setup (`iscc.exe`) on PATH. Everything else is in
> the repo.

## Part A — author + ship (dev machine)

1. **Build Starforge and launch it.** From the Launcher, click the molten **“Open
   Starforge — build apps”** tile (S4). Starforge boots straight into the **project
   library homescreen** (S2/S3), not a sandbox.
2. **New external project on another drive.** Homescreen ▸ **New Project** →
   name `MyRover`, location `D:\Work` (or any non-repo path) → **Create**. Confirm on
   disk: `D:\Work\MyRover\project.cproj`, `CMakeLists.txt`, `src/`, `scenes/` (S1).
3. **Author + build.** Add a ground plane + a primitive + the sample `HoverController`
   script; **Ctrl+B** builds the module into `D:\Work\MyRover\build\Debug\` (S1) and
   hot-loads it. **Ctrl+S** saves — the homescreen card now shows a **thumbnail** (S7).
4. **Relocatability.** Close the project, move `D:\Work\MyRover` → `D:\Games\MyRover`,
   reopen via **Open…** → scenes/scripts/build all still work (no absolute paths in the
   scene — see `test_filesystem_mounts`).
5. **Project settings + icon.** File ▸ **Project Settings** → set the window title
   (“My Rover”), size (1280×720), and pick an **icon.png** (copied into the project
   root). Save.
6. **Package.** File ▸ **Package** → tick **Build Release first**, **Zip**, and
   **Generate installer script** → **Package**. Watch the Console stream the Release
   build, then the stage/icon/zip/installer steps. Output: `dist/MyRover/` +
   `dist/MyRover-0.9.0.zip` (+ `dist/MyRover.iss`, compiled to `MyRover-Setup-0.9.0.exe`
   if `iscc` is on PATH). Confirm `MyRover.exe` shows the **custom icon** in Explorer.
7. **Run Standalone.** Toolbar ▸ **Run App** launches `dist/MyRover/MyRover.exe` — it
   opens titled **“My Rover”** at 1280×720 with no console, no Launcher (S5/S7).

## Part B — clean machine / VM (no repo, no SDK, no VS)

8. Copy `dist/MyRover-0.9.0.zip` (or the setup exe) to a clean Windows VM.
9. **Zip leg:** unzip → double-click `MyRover.exe` → the app runs with its icon/title
   and scene. Confirm user data lands in `%LOCALAPPDATA%\MyRover\` (logs/prefs), **not**
   under the app folder (S6). Drop a `portable.txt` next to the exe → data now writes to
   `MyRover\user\` instead (portable mode).
10. **Installer leg:** run `MyRover-Setup-0.9.0.exe` → per-user install (no UAC) →
    Start-menu + desktop shortcuts launch it → a double-clicked `.cham` recording opens
    the app via the `--replay` association → uninstall leaves `%LOCALAPPDATA%\MyRover`
    intact.

## Part C — Starforge itself is a product (S2)

11. Back on the dev machine: File ▸ **Package Starforge (self-host)** with Release. Copy
    `dist/Starforge/` to the clean VM → double-click `Starforge.exe` → the homescreen
    appears with no repo/SDK present; opening/creating projects works; hot-reload works
    only if VS + CMake are installed (otherwise the Build button surfaces an actionable
    “install VS Build Tools” message).

## Pass criteria

- External project builds/plays/relocates; ForgePlayground (legacy in-tree) unchanged.
- Packaged app on a clean machine: correct icon + title, runs, isolates user data,
  `--replay` works via the installer.
- `dist/Starforge` passes the same clean-machine test.
- Compat gate: SF_Telem / ViperSim / Frontier / Engine3DDemo launch and find their data
  exactly as before.
