# UX & Shipping — work orders

Status: execution index, 2026-09-24. Twelve sessions (UX-00 done in the planning session). Prompts live in
[`work-orders/`](work-orders/); this file is the map: waves, dependencies, landing order, and the ID
cross-check. Model recommendations and the run table are in
[`00-Start-Here.md`](00-Start-Here.md#session-guide).

## Dependency graph

```
UX-00  packet ............................................ done (planning session, on main)
  │
  ├── UX-01 flow editor + Editors host (ux/01) ──────────┐
  ├── UX-02 viewport/hierarchy/inspector/scenes/autosave ├─ wave 1, land in this order
  └── UX-03 launcher hygiene + samples + welcome (ux/03) ┘
        │
  ├── UX-04 SDK zip + installer + KI-63 (ux/04) ─────────┐
  ├── UX-G0 world-mode design + reservations (ux/g0)     ├─ wave 2, land 04 → G0 → D2
  └── UX-D2 API matrix + app-services ref (ux/d2) ───────┘
        │
  ├── UX-D1 guides tier + rename + 8 guides (ux/d1) ─────┐   (needs the fixed editor from wave 1 for its pictures,
  ├── UX-05 Cosmic as an SDK: preset, kit, CI (ux/05)    ├─   the zip from UX-04 for guide 00; wave 3, land D1 → 05 → H1)
  └── UX-H1 hardening carry-over + tidy (ux/h1) ─────────┘
        │
      UX-D3 developer-tier updates (ux/d3, alone; needs D1's rename)
        │
      UX-Q1 qualify + DOC02 from the zip + release report (main, alone; soaks → TESTING-PLAN, D-SOAKS)
```

The graph is dependency **and** the permitted concurrency: only the three lanes of one wave run at the
same time, and their "Owns" sets are pairwise disjoint
([`01-Contracts.md §10`](01-Contracts.md#10-parallel-lanes-d-lanes--file-ownership-matrix)).

## Landing order

`UX-00 → UX-01 → UX-02 → UX-03 → UX-04 → UX-G0 → UX-D2 → UX-D1 → UX-05 → UX-H1 → UX-D3 → UX-Q1`

## The work orders

| WO | Gate | Title | Items | Depends on | Acceptance | Prompt |
| --- | --- | --- | --- | --- | --- | --- |
| UX-00 | G0 | Packet | — | — | checkers exit 0 | this packet |
| UX-01 | G1 | Flow editor: size/dock/focus, backward-link routing, trigger-kind picker, Editors ✕ | 5, 6, 7, 8 | UX-00 | FE01–FE05 | [UX-01.md](work-orders/UX-01.md) |
| UX-02 | G1 | One gizmo per selection, UI Active semantics, flow-usage in the Inspector, Scenes list, Preferences + unsaved prompt | 16, 17, 18, 19, 21 | UX-00 | ED01–ED05 | [UX-02.md](work-orders/UX-02.md) |
| UX-03 | G1 | Fixture marker + launcher skip, samples from disk incl. PendulumLab, welcome, App default | 20, 4 | UX-00 | LH01–LH02 | [UX-03.md](work-orders/UX-03.md) |
| UX-04 | G2 | `Cosmic-SDK-<ver>-win64.zip`, `Starforge-Setup-<ver>.exe`, release workflow, GLFW install leak, KI-63, the VC++ runtime in every package (D-TOOLCHAIN) | 1, 3 | wave 1 landed | SD01–SD05, K02-bak | [UX-04.md](work-orders/UX-04.md) |
| UX-G0 | G2 | World-mode design doc + the five reserve-now contract items | 11 | wave 1 landed | WM01–WM03 | [UX-G0.md](work-orders/UX-G0.md) |
| UX-D2 | G2 | API matrix by feature, `app-services.md`, matrix checker in CI | 14 | wave 1 landed | DM01–DM02, DOC01 | [UX-D2.md](work-orders/UX-D2.md) |
| UX-D1 | G3 | Guides tier, `docs/guide/` → `docs/developer/`, eight guides with captured pictures, guide 00's "Install the C++ compiler" step (D-TOOLCHAIN) | 1, 2, 9, 10, 12, 13, 15, 18, 21 | wave 2 landed | DOC01, DOC03, DG01, DG02 (executable), DG03 | [UX-D1.md](work-orders/UX-D1.md) |
| UX-05 | G3 | `sdk` preset, `Build-Sdk.ps1`, `New-AppRepo.ps1`, CI `consumer` job, SF_Telem dry run, specimen banner | 3 | wave 2 landed (zip from UX-04) | EX01–EX05 | [UX-05.md](work-orders/UX-05.md) |
| UX-H1 | G3 | Crash dumps, ground-control dry run, scale profile, build-once-after-scaffold, compiler check + vswhere in the AP04 fixture (D-TOOLCHAIN), dead-3D tidy remainder | carry-over | wave 2 landed | H1-A–H1-F, B06 | [UX-H1.md](work-orders/UX-H1.md) |
| UX-D3 | G4 | Developer-tier chapter updates, `systems/app-platform.md` (new), the stale 3D mentions (~318 in 44 files), `lighting-2d.md` rewrite, `ecs.md` count | 10, 13 | UX-D1 landed | DOC01, DOC03 | [UX-D3.md](work-orders/UX-D3.md) |
| UX-Q1 | G5 | Integrate, qualify, DOC02 from the zip, showcase refresh, release report (states D-TOOLCHAIN), staged push/tag/release | all | all | everything rerun (incl. SD05, H1-E, H1-F, DG03); S01 fake-clock leg, S03, K02, DG02 (Y03, S01-native, S02, N02, T05 → [TESTING-PLAN](../TESTING-PLAN.md)) | [UX-Q1.md](work-orders/UX-Q1.md) |

Gates: G0 planning · G1 the editor defects Kaden hit are fixed and proven · G2 shippable SDK + world
contract reserved + reference matrix · G3 guides with pictures, the consumer path proven, hardening ·
G4 developer docs consistent · G5 qualified, released state staged for Kaden.

## Cross-check: every acceptance ID has exactly one owner

| ID | Owner | ID | Owner | ID | Owner |
| --- | --- | --- | --- | --- | --- |
| FE01 | UX-01 | SD01 | UX-04 | DG01 | UX-D1 |
| FE02 | UX-01 | SD02 | UX-04 | DG02 | UX-D1 (executable) / UX-Q1 (executed) |
| FE03 | UX-01 | SD03 | UX-04 | DM01 | UX-D2 |
| FE04 | UX-01 | SD04 | UX-04 (ENV-BLOCKED without a VM) | DM02 | UX-D2 |
| FE05 | UX-01 | K02-bak | UX-04 | DOC01 | each docs lane, rerun by UX-Q1 |
| ED01 | UX-02 | EX01 | UX-05 | DOC03 | each docs lane, rerun by UX-Q1 |
| ED02 | UX-02 | EX02 | UX-05 | H1-A | UX-H1 |
| ED03 | UX-02 | EX03 | UX-05 | H1-B | UX-H1 |
| ED04 | UX-02 | EX04 | UX-05 | H1-C | UX-H1 |
| ED05 | UX-02 | EX05 | UX-05 (CI runs on Kaden's push) | H1-D | UX-H1 |
| LH01 | UX-03 | WM01 | UX-G0 | B06 (per tidy commit) | UX-H1 |
| LH02 | UX-03 | WM02 | UX-G0 | Y03, S01, S02, N02-drift-2h, T05 | deferred → [TESTING-PLAN](../TESTING-PLAN.md) (D-SOAKS; UX-Q1 runs the S01 fake-clock leg) |
| — | — | WM03 | UX-G0 | S03, K02 (rerun), retained suites | UX-Q1 |
| SD05 | UX-04 (D-TOOLCHAIN) | DG03 | UX-D1 (D-TOOLCHAIN) | H1-E, H1-F | UX-H1 (D-TOOLCHAIN) |

## Per-WO gate / done-when summary

- **UX-01** — FE01–FE05 green Debug+Release; `.cflow` bytes of every tracked flow unchanged after open +
  save; the vendored patch documented; KIs for items 7 and 8 registered then closed.
- **UX-02** — ED01–ED05 green; `test_ui_widgets`/`test_ap03_editor` extended, E08 still green; KIs for
  items 16 and 17 registered then closed; prefs round-trip in `editor.toml`.
- **UX-03** — LH01–LH02 green; `CosmicApp.exe` launcher lists only real projects; E06 extended and green;
  PendulumLab first in the samples group.
- **UX-04** — the zip and the installer are produced from the lane's Release build; SD01 script passes from
  a temp dir with no `COSMIC_SDK`; K02 file lists still identical across the three app-package paths; KI-63
  closed; SD05: every import of every packaged Release binary resolves in the package or the OS (the VC++ runtime KI
  registered, then closed), `sdk.toml` carries `toolchain = "msvc"`, `docs/installer-guide.md` corrected.
- **UX-G0** — the design doc written from source; WM01–WM03 green; every existing app/game project and
  golden byte-identical; the §6 items each in their own commit.
- **UX-D2** — DM01/DM02 green; the checker runs in CI; the four AP-01 manifest rows point at
  `app-services.md`.
- **UX-D1** — eight guides with every image in the manifest and produced by the declared method; the
  rename landed with the link checker strict 0; DG02 executable (driver + wrapper + manifest); DG03 green (guide 00's
  install step, the Build Tools in the README prerequisites).
- **UX-05** — EX01–EX04 green locally, EX05 on Kaden's push; nothing under `Cosmic/src` or `Projects/`
  changed except the specimen README.
- **UX-H1** — crash dumps produced on a deliberate fault in both hosts; the dry run and the scale profile
  recorded; tidy commits each B06-clean and 0-warn; H1-E: no MSVC toolset → one clear message, never a raw build
  failure (KI closed), guide 00's compiler-check picture added at landing; H1-F: no executable file hard-codes a VS
  edition path.
- **UX-D3** — the developer chapters describe the fixed editor and the app model; strict coverage passes
  for `systems/app-platform.md`.
- **UX-Q1** — every suite and manifest green or honestly blocked; DOC02 executed from the zip; quick legs run, soaks listed as deferred (D-SOAKS);
  release report with the requirement → case → evidence matrix, stating D-TOOLCHAIN (MSVC only; the llvm-mingw
  flavour → [TOOLCHAIN-PLAN](../TOOLCHAIN-PLAN.md)); staged commands for Kaden.
