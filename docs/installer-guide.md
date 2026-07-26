# Installer Guide — Building, Shipping, and Installing a Cosmic App

> **Audience:** you (building a setup exe for an app) and anyone you hand the exe to (installing
> it). Covers the whole path: dev tree → `dist/` folder → `<App>-Setup-<version>.exe` → installed
> desktop app. How the pieces were designed is in
> [`plans/archive/07-installer-packaging-plan.md`](plans/archive/07-installer-packaging-plan.md);
> the build side — every CMake option, what each script does, the install rules and what a staged
> folder actually contains — is [`guide/building-and-shipping.md`](guide/building-and-shipping.md).

---

## 1. One-time prerequisites (build machine only)

| Tool | Why | Get it |
| --- | --- | --- |
| Visual Studio 2022 (C++ workload) + CMake ≥ 3.21 | builds the engine | already set up if you build Cosmic |
| **Inno Setup 6** | compiles the installer | https://jrsoftware.org/isinfo.php — default install path is auto-detected (`%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe`) |

End users need **nothing** — the package bundles the VC++ runtime DLLs, and the installer is
per-user (no admin prompt).

## 2. Build an installer — the one command

From the repo root:

```bat
package_installer.bat SF_Telem
```

What it does, in order:
1. Reads the version from `Cosmic/src/core/Version.h` (single source of truth).
2. Runs `package.bat SF_Telem`: clean **Release** build → `cmake --install` into
   `dist\SF_Telem\` → prunes every project DLL and asset folder except `SF_Telem`'s → zips.
3. Compiles `installer\CosmicSetup.iss` with ISCC.

**Output:** `dist\SF_Telem-Setup-<version>.exe` — a self-contained setup for any Windows 10/11
x64 machine. No SDK, no environment variables, no VC++ redist needed on the target.

Substitute any project name that lives under `Projects/` (the DLL name must match the folder
name, which is true for all current projects).

### Related commands

| Command | Produces |
| --- | --- |
| `package.bat` | full SDK dist (`dist\Cosmic\` + zip, every project + launcher) — for unzip-and-run distribution without an installer |
| `package.bat <AppName>` | single-app dist folder + zip, no installer compile |
| `package_installer.bat <AppName>` | single-app dist **and** the setup exe |

## 3. What the installer does on the target machine

- **Per-user install** (`PrivilegesRequired=lowest`): no UAC/admin; files land under
  `%LOCALAPPDATA%\Programs\<App>` (Inno's `{autopf}` for a per-user install).
- Creates a **desktop icon** and Start-menu entry that launch
  `CosmicApp.exe --project <AppName>` — the app boots directly, no launcher screen.
- Registers a normal **uninstaller** (Settings → Apps, or the uninstall entry in the app folder).

## 4. Where user data lives

The engine's `user://` mount decides once at boot, from two inputs: whether an **app identity** was
set, and whether the **exe directory is writable** (probed by creating and deleting
`.cosmic_write_probe` next to the exe). Only a `boot.cfg` sets the identity — `--project` does not.

| Identity | Exe dir writable | `user://` resolves to |
| --- | --- | --- |
| set (`boot.cfg`) | yes | `<exe dir>\user\` |
| set (`boot.cfg`) | no | `%LOCALAPPDATA%\<AppName>\` |
| not set (`--project`, Launcher) | yes | `<exe dir>` itself |
| not set | no | `%LOCALAPPDATA%\Cosmic\` |

**Two consequences worth knowing, because the script comments claim otherwise.** A
`package_installer.bat` app is launched by its shortcut with `--project <App>`, so it gets **no
per-app isolation** — its `user://` is the shared `Cosmic` root, not `%LOCALAPPDATA%\Cosmic\<App>`.
And a per-user install directory (`%LOCALAPPDATA%\Programs\<App>`) **is writable by the installing
user**, so in practice the probe succeeds and an installed app runs in *portable* mode with its data
next to the exe. `CosmicSetup.iss`'s header comment ("goes to `%LOCALAPPDATA%\Cosmic` … NOT into
`{app}`") describes the intent, not the behaviour.

Don't reason about the table — **read the boot log**. The engine prints `user:// root -> <path>`
immediately at startup. (In a Release build that line is in the log file; there is no console.)

Uninstalling leaves any `%LOCALAPPDATA%` folder behind on purpose — recordings are user data. Files
the app wrote *inside* the install folder were not installed by Inno, so they can survive the
uninstall too; delete the folder manually for a truly clean removal.

## 5. Updating an installed app

Build a new setup exe (bump `Version.h` first) and run it — it installs over the existing copy.
User data is untouched. There is no auto-update mechanism (deliberately — see the archived plan's
step 6 for the parked CI-release idea).

## 6. Things a recipient might hit

| Symptom | Explanation / fix |
| --- | --- |
| Blue **"Windows protected your PC"** SmartScreen dialog | The exe is unsigned. Click *More info → Run anyway*. Code signing is parked until distribution matters (archived plan, step 6). |
| Antivirus quarantines the download | Same root cause (unsigned, low-reputation binary). Whitelist or sign. |
| App opens the *launcher* instead of the app | The shortcut's `--project <Name>` didn't resolve — the project DLL is missing from `projects\`. Rebuild the installer; check `package.bat`'s prune stage output. |
| Missing-DLL error on a clean machine | Should not happen (VC++ runtime is bundled by `InstallRequiredSystemLibraries`). If it does, verify `dist\<App>\` contains the `vcruntime*.dll`/`msvcp*.dll` set before the ISCC stage. |
| Old version still listed after reinstall | Two installs with different `AppName` casing create separate entries — keep the name exact. |

## 7. Build-machine troubleshooting

| Symptom | Fix |
| --- | --- |
| `[ERROR] Inno Setup 6 not found` | Install it, or put `ISCC.exe` on `PATH`. The staged `dist\<App>\` folder is still valid without it. |
| `[ERROR] Project DLL "<App>.dll" was not produced` | Project folder name ≠ CMake target name, or the project failed to build — check the Release build log above the error. |
| Version reads `0.0.0` | `COSMIC_VERSION_STRING` line in `Cosmic/src/core/Version.h` was reformatted — keep the `#define COSMIC_VERSION_STRING "x.y.z"` shape. |
| Zip step warning | Cosmetic — `Compress-Archive` failed but `dist\<App>\` is complete; the ISCC stage doesn't use the zip. |

## 8. Acceptance check for a release (5 minutes, do it on a machine/VM without the SDK)

1. Run the setup exe → desktop icon appears.
2. Double-click → boots **straight into the app** (no launcher).
3. Exercise one real workflow (e.g. SF_Telem: connect / record / stop).
4. Read the `user:// root -> …` line from the log, confirm files appeared there, and check for
   **zero** write errors.
5. Uninstall from Settings → app folder gone, user data still present.
