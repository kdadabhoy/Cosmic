# Installer & Packaging Plan — "one shortcut on the desktop"

> **Today (verified):** `package.bat` already produces a fully self-contained folder —
> Release build → `cmake --install` to `dist/Cosmic/` (`CosmicApp.exe`, `Cosmic.dll`,
> `projects/*.dll`, `assets/`, VC++ runtime DLLs) → `dist/Cosmic.zip`. No env vars needed at
> runtime (`Runtime/Main.cpp` sets CWD to the exe dir; `engine://`/`project://` resolve relative to
> it; the launcher scans `exeDir/projects/` — `LauncherLayer.cpp:544`). An icon exists
> (`Runtime/app.ico` via `CosmicApp.rc`).
> **Missing:** boot-straight-into-one-app, version identity, DPI manifest, writable-data handling,
> an actual installer, desktop shortcut. That's this plan, in order.

---

## Step 1 — `--project` boot flag (engine, small) — the keystone ✅ *(done 2026-07-01)*

The desktop shortcut must open **your app**, not the launcher.

- `Runtime/Main.cpp`: parse args (keep it dependency-free: a 20-line loop). Recognize
  `--project <NameOrDll>` (and optionally `--fullscreen`).
- `Application`: add `SetStartupProject(const std::string&)` called before `Run()`; in
  `Initialize()`, if a startup project is set, **skip pushing `LauncherLayer`** and instead set
  `m_PendingProjectDLL` so the existing Safe-Zone path (`Application.cpp:193–221`) performs the
  load on the first frame — reusing the proven transition machinery rather than adding a second
  load path. Resolve bare names via the same candidates `LoadProjectDLL` already tries
  (`projects/<name>.dll`, `<name>.dll`, absolute).
- Failure behavior: log error + fall back to the launcher (never a dead exe).
- Acceptance: `CosmicApp.exe --project SF_Telem` boots directly into SF_Telem; no args = launcher
  exactly as today.

## Step 2 — Version identity + DPI manifest (engine/Runtime, small) ✅ *(done 2026-07-01)*

1. `Cosmic/src/core/Version.h`: `COSMIC_VERSION_MAJOR/MINOR/PATCH` + `COSMIC_VERSION_STRING`
   (start at `0.9.0`). Log it at startup banner; show in launcher corner and workspace title bar.
2. `Runtime/CosmicApp.rc`: add a `VERSIONINFO` block (FileVersion/ProductVersion from the same
   numbers — either hardcode-and-sync or generate via `configure_file`). Explorer's Properties →
   Details then shows real version info.
3. **App manifest** (`Runtime/CosmicApp.manifest`, linked via CMake `target_sources` or
   `/MANIFESTINPUT`): declare `<dpiAwareness>PerMonitorV2</dpiAwareness>` and `longPathAware`.
   Today DPI awareness comes from GLFW at runtime; declaring it in the manifest is the
   Windows-correct way and removes a class of first-frame scaling weirdness the 2026-06-26 DPI
   fix danced around.
4. Optional polish: `ProductName "Cosmic Engine"`, per-app override later (Step 4).

## Step 3 — Writable data location (engine, IMPORTANT before any installer) ✅ *(done 2026-07-01)*

Installed apps live in `Program Files` = **read-only for standard users**. Today would break:
- `Log::Init("logs")` (`Application.cpp:41`) — CWD-relative,
- `DataRecorder` autosave/recordings (`recordings/...` — CWD-relative),
- theme/user settings persistence (wherever ThemeManager writes; verify at implementation).

Fix in the VFS (single choke point, `Cosmic/src/utils/FileSystem.h`):
- Add mount `user://` → `%LOCALAPPDATA%/Cosmic/<ActiveProject>/` (via `SHGetKnownFolderPath(FOLDERID_LocalAppData)`), created on demand.
- **Portable mode rule:** if the exe dir is writable (dev tree, unzipped folder), `user://` maps to
  the exe dir instead — dev workflow unchanged, recordings stay next to the app. Probe once at boot
  (attempt to create/delete a temp file next to the exe).
- Point `Log::Init`, recorder default paths, and settings writes through `user://`.
- Acceptance: run from `Program Files` as a standard user → logs + recordings land under
  `%LOCALAPPDATA%\Cosmic\...`, zero write errors; run from the dev tree → identical to today.

## Step 4 — App-branded package profile (build script, small) ✅ *(done 2026-07-01)*

`package.bat` parameterized: `package.bat SF_Telem` →
- installs only that project's DLL + its assets (skip other projects),
- names the staging dir/zip after the app,
- passes `-DCOSMIC_DIST=ON` as today (hides New Project UI).
Keep plain `package.bat` producing the full SDK zip. (Implementation: an `--app` install component
or a post-install prune script — component-based `cmake --install --component` is the clean route.)

## Step 5 — Inno Setup installer (new, the deliverable) ✅ *(done 2026-07-01 — `installer/CosmicSetup.iss` + `package_installer.bat`; run the fresh-VM acceptance when convenient)*

**Choice: Inno Setup** — free, one `.iss` text file (reviewable/AI-editable), pervasive for Win32
tools, trivially makes shortcuts with arguments, per-user install without admin. WiX/MSIX rejected:
MSIX wants signing + store-style identity; WiX is heavyweight for a solo project. NSIS viable but
Inno's syntax is saner.

`installer/CosmicSetup.iss` (checked into the repo):

```ini
[Setup]
AppName=SF Telem            ; parameterized per app profile (Step 4), or "Cosmic Engine" for the SDK
AppVersion={#AppVersion}    ; passed from Version.h via /D on the compile command line
DefaultDirName={autopf}\SFTelem
PrivilegesRequired=lowest   ; per-user install → {localappdata}\Programs, no UAC prompt
[Files]
Source: "..\dist\Cosmic\*"; DestDir: "{app}"; Flags: recursesubdirs
[Icons]
Name: "{autodesktop}\SF Telem";  Filename: "{app}\CosmicApp.exe"; Parameters: "--project SF_Telem"; IconFilename: "{app}\CosmicApp.exe"
Name: "{autoprograms}\SF Telem"; Filename: "{app}\CosmicApp.exe"; Parameters: "--project SF_Telem"
```

- Wrap in `package_installer.bat`: run `package.bat <app>` → compile `.iss` with `ISCC.exe` →
  `dist/SFTelem-Setup-<version>.exe`.
- Uninstaller: automatic with Inno. Leave `%LOCALAPPDATA%\Cosmic` data behind by default (recordings
  are user data).
- Acceptance (the real test): **fresh Windows VM / second PC** → run the setup exe → desktop icon
  appears → double-click → boots straight into SF_Telem → connect/record works → recordings appear
  under `%LOCALAPPDATA%` → uninstall cleans `Program Files` entry.

## Step 6 — Optional follow-ons

- **CI release job** (after WO-15, doc 02): tag push → build Release → run tests → package →
  Inno compile → upload the setup exe as a GitHub Release artifact.
- **Code signing:** unsigned exes trip SmartScreen ("Windows protected your PC → More info → Run
  anyway"). Fine for personal use. If distributing later: cheapest real option is an OV code-signing
  cert (~$100+/yr) or Azure Trusted Signing (~$10/mo); park until it matters.
- **File association**: associate `.bin` recordings → `CosmicApp.exe --project SF_Telem --replay "%1"`
  (needs a `--replay` arg first; park).

## Order & size

| Step | Size | Notes |
| --- | --- | --- |
| 1 `--project` flag | S | do first — also handy in dev (`build\Runtime\Debug\CosmicApp.exe --project SF_Telem` skips clicking through the launcher every run) |
| 2 version + manifest | S | independent |
| 3 `user://` writable data | M | REQUIRED before step 5; touches Log/recorder/theme paths |
| 4 app package profile | S | after 1 |
| 5 Inno installer | M | after 3+4; the visible payoff |
| 6 CI release / signing / assoc | opt | later |

Steps 1, 2, 4, 5 are excellent lower-tier-AI tasks (mechanical, verifiable); step 3 deserves the
better model or careful review — it changes path behavior everywhere.
