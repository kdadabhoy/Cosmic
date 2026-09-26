# Cosmic UX & Shipping — planning packet

Prepared 2026-09-24 against `main` at `0c2edd8` (origin/main == main). Successor of
[`../app-platform-2026-09-18/`](../app-platform-2026-09-18/00-Start-Here.md), whose priority scope is done
(qualified `fa1223a`, tag `cosmic-app-platform-g5-2026-09-20`) and whose close-out prompt
(`work-orders/FOLLOW-UP.md`) is absorbed here (AP-D2 → UX-D1/D2/D3; KI-63 → UX-04; tidy + AP-H1 → UX-H1;
soaks → UX-Q1, then postponed to [`../TESTING-PLAN.md`](../TESTING-PLAN.md) by D-SOAKS).

**Goal.** Kaden used the App Platform for real (the PendulumLab walkthrough) and came back with 21 items.
This packet turns them into work: the editor defects he hit (flow editor, gizmo, visibility toggle, Editors
tab), the things he could not find (which build script, where his code lives, what a service is, what a
button does, every scene of the project), the hygiene he noticed (test fixtures in the launcher, ForgePong
still the welcome, no autosave control), the two shipping questions (**a downloadable Starforge + SDK
release** so nobody navigates `build\Runtime\Release`, and **an app in its own repo that consumes Cosmic
like a library**), the documentation he actually wants (**guides with pictures first**, an API matrix by
feature, a real services chapter), and the forward-looking one (a later **2D game / world mode** that must
not force migration of app projects made now). Then not touch the engine for a long while.

## Read in this order

1. **This file** — decisions, the item → work-order map, waves, the session guide.
2. [`01-Contracts.md`](01-Contracts.md) — every name, field, key, path and rule the lanes must agree on,
   the 2D world contract, the file-ownership matrix, the acceptance index.
3. [`02-Work-Orders.md`](02-Work-Orders.md) — dependency graph, landing order, ID cross-check.
4. [`03-Acceptance-Catalog.md`](03-Acceptance-Catalog.md) — the acceptance IDs, procedures and oracles.
5. [`work-orders/README.md`](work-orders/README.md) — the global and lane rules, then the one
   `work-orders/UX-xx.md` whose prompt you are running. The orchestrator uses
   [`work-orders/ORCHESTRATOR.md`](work-orders/ORCHESTRATOR.md).

Nothing in this packet is executed by reading it. Engine behaviour changes only when a work order runs.

## Locked decisions (Kaden, 2026-09-24) — a work order may record evidence, never reopen these

- **D-SHIP** — ship a prebuilt **Cosmic SDK release**: `Cosmic-SDK-<ver>-win64.zip` whose tree *mirrors the
  checkout paths the consumer CMake already uses* (headers, the dependency headers + the imgui/implot sources
  the templates compile in, `build/Runtime/{Debug,Release}/` with `Cosmic.dll/.lib`, `CosmicApp.exe`,
  `Starforge.exe`, `Starforge.dll`, assets and templates, the CRT DLLs, `installer/` with every licence the
  packager's manifest names, PendulumLab's source tree so the homescreen has its featured sample, `sdk.toml`,
  `VERSION`), so
  `COSMIC_SDK=<unzipped folder>` and the editor's existing "three directories up" fallback both work with
  **zero template changes**; plus `Starforge-Setup-<ver>.exe` (per-user, shortcut to the editor, optional
  `COSMIC_SDK` task). Users still need Visual Studio "Desktop development with C++" for their own project
  DLL, never the engine build. A `find_package(Cosmic)` config was rejected: more surface, breaks the
  one-layout rule.
- **D-LIBRARY** — an app lives in its own repo and consumes Cosmic **as an SDK, never as a CMake
  subproject**. The app repo holds Cosmic as a git submodule (`extern/Cosmic`, pinned to a release tag) *or*
  the unzipped SDK; the engine is built by **Cosmic's own top-level build** (a new `sdk` preset = today's
  configure with tests off and the three sample projects skipped, which yields exactly the SDK tree above);
  the app's `CMakeLists.txt` is the App template's, unchanged (`COSMIC_SDK` → imported `Cosmic.lib`). This
  is the path PendulumLab is already qualified on (Y01/K01). `add_subdirectory(extern/Cosmic)` from an app
  was rejected as too risky for the value: it would move the `${COSMIC_SDK_DIR}/build/Runtime` convention
  the editor, packager, acceptance runner and CI all rely on and reopen KI-26/34/50.
- **D-SPECIMEN** — `Projects/SF_Telem` stays in this repo as a **frozen test specimen** (three stability
  fixtures compile its sources; the S01 soak drives it) with a README banner; the SF_Telem *product* moves to
  Kaden's own repo built per D-LIBRARY. UX-05 proves that path with a dry run; Kaden creates the repo.
- **D-GUIDES** — documentation gets a **user tier**: `docs/guides/` (numbered, screenshot-driven, every
  control boxed in red) is the front door from the README and `docs/README.md`; the existing developer
  chapters `docs/guide/` are renamed **`docs/developer/`** by script (link rewrite, checker-enforced).
  Reference and systems tiers unchanged. Every guide picture is a capture of the real editor produced by the
  capture driver or by a computer-use screenshot annotated through `tools/guide_shots.py` — never a
  hand-drawn box, never a mock-up.
- **D-SAMPLES** — **PendulumLab is the featured sample**; FlowDemo and ForgePong stay as "Game samples".
  The homescreen lists `templates/samples/*` **plus** the SDK's `Projects/*` that carry a `project.cproj`,
  grouped by kind; the one-time welcome offers PendulumLab; New Project defaults to the **App** template.
- **D-WORLD** — the 2D game/world mode is **designed now, built later**. The assessment (item 11) found the
  engine already renders a world + HUD scene and already supports user-coded fixed-step physics; this
  campaign lands only five small reserve-now items (contracts §6) so the later work is purely additive:
  the project manifest preserves unknown keys; a written 2D world contract; a scene-version warning; the
  physics-backend selection seam (physics initialised after services); one clear colour across player and
  editor with post effects previewed in editor 2D Play. The world features themselves (parallax, camera
  follow/zoom, `Particle2D`, sorting layers, the to-9km sample) are a future packet.
- **D-LAUNCHER** — test fixtures are hidden from the Cosmic Launcher **by structure**: every fixture DLL
  exports a marker symbol (`CosmicTestFixture`, via `CS_TEST_FIXTURE()`), the launcher skips DLLs that export
  it. No name blacklist, no test-path churn.
- **D-SOAKS** (Kaden, 2026-09-24, during wave 1) — "do tests and quick things": the multi-hour soaks
  (S01-native, Y03, S02, N02-drift-2h, T05) and their drivers are **postponed out of this campaign** to
  [`../TESTING-PLAN.md`](../TESTING-PLAN.md). UX-Q1 runs the quick legs (S01 fake-clock, S03 ×5, K02) and reports
  the soaks as not run — deferred, never as a pass.
- **D-TOOLCHAIN** (Kaden, 2026-09-25, before wave 2) — **MSVC stays the only supported toolchain** for the engine, the
  editor and user projects in this campaign: Visual Studio Community 2026 or the Build Tools for Visual Studio 2026,
  workload "Desktop development with C++". A bundled llvm-mingw "zero-install" SDK flavour is **designed in
  [`../TOOLCHAIN-PLAN.md`](../TOOLCHAIN-PLAN.md) and built later** (the `Cosmic.dll` ↔ project-DLL boundary exports
  C++ classes, so a second toolchain means a second SDK flavour, never mixing). This campaign adds only the friction
  fixes, each in a not-yet-started WO: **UX-04** — the VC++ runtime in every app package, the SDK zip and
  Starforge-Setup (a real defect: today's packages carry none; a KI registered first) and `sdk.toml` gains
  `toolchain = "msvc"` (SD05); **UX-H1** — Starforge's compiler check (one clear message with the winget one-liner
  instead of a raw build failure) and the acceptance fixture's cmake path via vswhere (H1-E, H1-F); **UX-D1** — guide
  00's "Install the C++ compiler" step and Build Tools in the README prerequisites (DG03); **UX-Q1** — qualifies the
  new IDs and states D-TOOLCHAIN in its report.
- Carried over unchanged: **D-commit** (author `kdadabhoy <kdadabhoy28@gmail.com>`, no AI trailer, the AI
  never pushes, never publishes a release), **D-LANES** (one worktree per parallel lane under
  `build\_lanes\`, at most three sessions at once, disjoint files, rebase + retained suites before landing),
  **D-CPU/D-GPU/D-9km**, honest gates, KI-before-fix, the three checkers exit 0 before a commit lands.

## The 21 items → work orders

| # | Item (Kaden, 2026-09-24) | WO |
| --- | --- | --- |
| 1 | Which build script; guide from clone; ship Starforge as an exe | UX-D1 (guide 00) · UX-04 (release) |
| 2 | Guide: where the code I edit lives | UX-D1 (guide 01) |
| 3 | SF_Telem in its own repo referencing Cosmic | UX-05 (+ UX-04 zip) · guide 06 |
| 4 | ForgePong default; FlowDemo on the homescreen | UX-03 |
| 5 | Flow graph opens tiny | UX-01 |
| 6 | Backward transitions drawn through the box | UX-01 |
| 7 | `when (guard only)` cannot be turned off | UX-01 (KI) |
| 8 | "Editors" tab ✕ does nothing | UX-01 (KI) |
| 9 | Do screen scripts survive a rebuild (yes) | UX-D1 (guide 01/02) |
| 10 | What is a service | UX-D1 (guide 03) · UX-D2 (`app-services.md`) · UX-D3 |
| 11 | Future 2D game/world mode | UX-G0 |
| 12 | Guides first-class, separate, easiest to find | UX-D1 |
| 13 | Scenes / flow / "your code into the engine" / `.cscene` | UX-D1 (guides 02, 04) · UX-D3 |
| 14 | Public API matrix by feature | UX-D2 |
| 15 | AppService minimum; link it to a plot | UX-D1 (guide 03) |
| 16 | Sprites do not move with the gizmo | UX-02 (KI) · guide 05 |
| 17 | Eyeball on a Plot does nothing; toolbar tools on UI elements | UX-02 (KI) · guide 05 |
| 18 | What does this button do (Inspector) | UX-02 · guide 04 |
| 19 | A list of every screen/scene, quick navigation | UX-02 |
| 20 | Test fixtures in "Engine demos & tools" | UX-03 |
| 21 | Autosave, toggleable, interval | UX-02 · guide 07 |
| — | Carry-over: KI-63, dead-3D tidy, AP-H1 hardening, soaks | UX-04 · UX-H1 · [TESTING-PLAN](../TESTING-PLAN.md) (D-SOAKS) |
| — | Added 2026-09-25 (D-TOOLCHAIN): the VC++ runtime missing from shipped packages; a clear "no C++ compiler" message; the install step in guide 00 | UX-04 (SD05) · UX-H1 (H1-E, H1-F) · UX-D1 (DG03) · UX-Q1 (re-run) · the bundled toolchain → [TOOLCHAIN-PLAN](../TOOLCHAIN-PLAN.md) |

## Waves (12 sessions; never more than three at once)

```
Wave 0  UX-00   packet                                               done in the planning session, on main
Wave 1  UX-01   flow editor + Editors host                           worktree ux/01 ┐ from the UX-00 commit;
        UX-02   viewport, hierarchy, inspector, scenes, autosave     worktree ux/02 ├ disjoint files;
        UX-03   launcher hygiene, samples, welcome, default template worktree ux/03 ┘ land 01 → 02 → 03
Wave 2  UX-04   SDK zip + Starforge installer + KI-63                worktree ux/04 ┐ from post-wave-1 main;
        UX-G0   world-mode design + reserve-now contract changes     worktree ux/g0 ├ land 04 → G0 → D2
        UX-D2   API matrix + app-services reference + checker        worktree ux/d2 ┘
Wave 3  UX-D1   guides tier + rename + 8 guides with pictures        worktree ux/d1 ┐ from post-wave-2 main;
        UX-05   Cosmic as an SDK: preset, app-repo kit, CI, dry run  worktree ux/05 ├ land D1 → 05 → H1
        UX-H1   hardening carry-over + tidy remainder                worktree ux/h1 ┘
Wave 4  UX-D3   developer-tier updates (post-rename)                 worktree ux/d3, alone
Wave 5  UX-Q1   qualify, DOC02 from the zip, release report          main, alone (soaks: D-SOAKS)
```

Landing order into `main`: UX-00 → UX-01 → UX-02 → UX-03 → UX-04 → UX-G0 → UX-D2 → UX-D1 → UX-05 → UX-H1 →
UX-D3 → UX-Q1.

## Session guide

| # | WO | Prompt | Runs where | Concurrent with | Model | Effort |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | UX-00 | this packet | `main` | — | Fable 5.1 — **done** | high |
| 2 | UX-01 | `work-orders/UX-01.md` | worktree `ux/01` | UX-02, UX-03 | Opus 5.5 (`claude-opus-5-5`) | high |
| 3 | UX-02 | `work-orders/UX-02.md` | worktree `ux/02` | UX-01, UX-03 | Opus 5.5 | high |
| 4 | UX-03 | `work-orders/UX-03.md` | worktree `ux/03` | UX-01, UX-02 | Opus 5.5 | high |
| 5 | UX-04 | `work-orders/UX-04.md` | worktree `ux/04` | UX-G0, UX-D2 | Opus 5.5 | high |
| 6 | UX-G0 | `work-orders/UX-G0.md` | worktree `ux/g0` | UX-04, UX-D2 | Opus 5.5 | max |
| 7 | UX-D2 | `work-orders/UX-D2.md` | worktree `ux/d2` | UX-04, UX-G0 | Opus 5.5 | high |
| 8 | UX-D1 | `work-orders/UX-D1.md` | worktree `ux/d1` | UX-05, UX-H1 | Opus 5.5 (Sonnet 5 for prose only) | high |
| 9 | UX-05 | `work-orders/UX-05.md` | worktree `ux/05` | UX-D1, UX-H1 | Opus 5.5 | high |
| 10 | UX-H1 | `work-orders/UX-H1.md` | worktree `ux/h1` | UX-D1, UX-05 | Opus 5.5 | high |
| 11 | UX-D3 | `work-orders/UX-D3.md` | worktree `ux/d3`, alone | — | Opus 5.5 | high |
| 12 | UX-Q1 | `work-orders/UX-Q1.md` | `main`, alone | — | Opus 5.5 | xhigh |

Effort is the Claude Code effort setting: **max** for the world-mode contract work, **xhigh** for final
qualification, **high** for everything else. Never below high on this campaign.

How to run: the orchestrator (`work-orders/ORCHESTRATOR.md`, Opus 5.5, effort high, in `C:\dev\Cosmic`)
spawns one background agent per WO with that WO's `~~~text` block, verifies on disk, lands lanes in the
order above, and reports to Kaden after each landing. It may drive the real editor through computer-use for
guide pictures. Kaden pushes `main` when he wants CI, and pushes the `cosmic-sdk-v<ver>` tag when he wants
the release workflow to build the zip and the installer.

## What is deliberately deferred (not in any work order; each names what would unlock it)

The world-mode features themselves (parallax, camera follow/zoom controller, `Particle2D`, sorting layers,
polygon fills, sequencer, GIF/MP4 export, `GetFixedAlpha`, a "New World Scene" scaffold, the to-9km sample)
— a future packet once the app-platform work is quiet, with UX-G0's design doc as its brief. Also worth
raising with Kaden, unscheduled: `Config::Save` for app settings; a Problems panel (flow Validate + build
errors + missing channels in one list); a generic `--selftest` flag on packaged apps (PendulumLab's Y02
pattern); crash dumps in shipped apps (UX-H1 does the editor); the UDP/CAN link + COBS codec (stability
backlog A2) before to-9km; a serial-port simulator surfaced in the editor; a Starforge version check against
the GitHub Releases feed once the SDK zip exists; animated (GIF) strips for the guides (stability A1).

The bundled llvm-mingw toolchain — a second, "zero-install" SDK flavour so a user can build a project DLL with only
Starforge installed — is designed in [`../TOOLCHAIN-PLAN.md`](../TOOLCHAIN-PLAN.md) (steps T0..T10, acceptance
TC01..TC11) and built later (D-TOOLCHAIN): it unlocks once this campaign has closed and the App Platform is quiet,
and Kaden decides whether zero-install is worth the doubled build and qualification matrix.
