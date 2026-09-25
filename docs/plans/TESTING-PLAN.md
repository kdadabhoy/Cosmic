# Cosmic — Testing plan (long runs and user-run checks)

Status: live, 2026-09-24. This is the home for the testing that is **deliberately not run inside a campaign**:
the multi-hour soaks and the checks that need a person or a clean machine. Each campaign's qualification WO
runs the quick suites (CosmicTests, the render goldens, every manifest, S03, K02); the items here are run when
Kaden schedules them, and their results are recorded in the evidence dir named below. The master roadmap's
acceptance ledger points here ([`00-MASTER-ROADMAP.md`](00-MASTER-ROADMAP.md#acceptance-ledger-user-run-items--never-silently-dropped)).

**Decision (Kaden, 2026-09-24):** the long soaks are postponed out of the UX & Shipping campaign — "do tests
and quick things", no multi-hour runs now. UX-Q1 runs the quick legs only (S03 ×5, K02, the S01 fake-clock leg)
and lists everything below as *not run — deferred to TESTING-PLAN.md*. Nothing here is a pass until it has run.

## Why these matter

Every automated test in the repo runs for seconds to minutes. A soak is the only thing that catches what
grows slowly: a leak of a few KB per screen switch, fixed-step clock drift that adds up over an hour, a
disconnect/reconnect race that fails once in hundreds of tries, frame time that creeps because something is
never trimmed. SF_Telem runs for whole test sessions and PendulumLab ships in the SDK zip, so those two are the
ones worth running first.

## Before any soak run

- The PC is **unavailable for the whole run**: the drivers bring windows to the front and inject clicks. Nothing
  else on the desktop.
- **Screen lock and sleep off** for the duration — set by Kaden; a session never changes power or lock settings.
  A locked desktop mid-run is `ENVIRONMENT_BLOCKED`, never a pass. A manual kill is never a pass.
- Release binaries at one pinned SHA (`git rev-parse HEAD`, clean `git status`), from `C:\dev\Cosmic`,
  `-TempRoot C:\dev\Cosmic\build\_temp\soak`. Results go to `docs/plans/<current packet>/evidence/soaks/<id>/`,
  keyed to the SHA; `*.log` stays ignored (commit excerpts + JSON).
- Every child runs in a Windows job object (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`). None exists yet — the runner
  kills by `taskkill /T` (`tests/acceptance/AcceptanceRunner.psm1`); write `tests/acceptance/fixtures/JobObject.psm1`
  (Add-Type P/Invoke) first and use it for every run below.

## The soaks

| ID | What it proves | Wall time | Priority | Driver today |
| --- | --- | --- | --- | --- |
| **S01-native** | SF_Telem (the in-tree specimen) 2 h on the real clock: record/autosave/export, screen switches, disconnect/reconnect, final close; a separate non-recording segment for the memory plateau | 2 h | **1** | exists (`wo06` `-Profile native`) |
| **Y03** | Packaged PendulumLab.exe 2 h: screen switch every 30 s, Reset every 5 min, memory plateau (WO-02 method), no hang | 2 h | **1** | **to write** |
| S02 | AnalysisSample 2 h: animation/scrub/resize/capture loop + project reopen; memory and frame-time trend | 2 h | 3 | **to write** (X01 in `wo10-sample` is the functional half) |
| N02-drift-2h | 432,000 frames of fixed-step drift (WO-10) | ~55 min | 2 | exists (`wo10-drift` `-Profile release`) |
| T05 | 30 min controlled telemetry: exactly 216,000 accepted frames, 108,000 ticks/entity at 60 Hz | 30 min | 2 | exists (`wo05` `-Profile nightly`) |

Recommended first sitting: **S01-native + Y03** (~4 h, one evening). Then N02 + T05 (~1.5 h). S02 last, if ever
— nobody runs AnalysisSample for hours.

Run them **sequentially**, never concurrently (S03 in AP-Q1 showed concurrent processes perturb timing).

### Commands (from `C:\dev\Cosmic`, Release)

```
# S01-native — SF_Telem, real clock, 2 h
powershell -ExecutionPolicy Bypass -File tests\acceptance\Run-Acceptance.ps1 -Manifest C:\dev\Cosmic\tests\acceptance\manifests\wo06.manifest.json -Profile native -Config Release -TempRoot C:\dev\Cosmic\build\_temp\soak
# N02-drift-2h — ~55 min
powershell -ExecutionPolicy Bypass -File tests\acceptance\Run-Acceptance.ps1 -Manifest C:\dev\Cosmic\tests\acceptance\manifests\wo10-drift.manifest.json -Profile release -Config Release -TempRoot C:\dev\Cosmic\build\_temp\soak
# T05 — 30 min
powershell -ExecutionPolicy Bypass -File tests\acceptance\Run-Acceptance.ps1 -Manifest C:\dev\Cosmic\tests\acceptance\manifests\wo05.manifest.json -Profile nightly -Config Release -TempRoot C:\dev\Cosmic\build\_temp\soak
```

(The S01 fake-clock leg, `wo06 -Profile pr` "D05-two-hour", runs in minutes and stays in each campaign's
qualification.)

### Drivers still to write (one session, before the first sitting)

- **`JobObject.psm1`** — as above.
- **Y03**: `tests/acceptance/fixtures/Run-SoakY03.ps1` + `tests/acceptance/manifests/soak-y03.manifest.json` over the
  PendulumLab.exe that a fresh `apq1-y02` run packages — take the dist folder from the Y02 result JSON, never a
  hard-coded `<repo>\dist` (after UX-04 an external project packages under its own root). Screen switch every 30 s
  and Reset every 5 min by injected input (`Send-Click.ps1` / `Bring-ToFront.ps1` in
  `app-platform-2026-09-18/evidence/AP-Q1/` are the pattern); memory plateau by the WO-02 method
  (`2d-stability-2026-09-16/evidence/WO-02/runtime-baselines.txt` §(e)); no hang. "0 fixed-step drift over the
  injected clock" needs in-process instrumentation PendulumLab lacks: report it NOT COVERED with the reason unless a
  registered KI justifies adding it — never PASS by omission.
- **S02**: `Run-SoakS02.ps1` + `soak-s02.manifest.json` per the stability row
  (`2d-stability-2026-09-16/03-Acceptance-Test-Catalog.md` S02), same honesty rule.

Oracles: `2d-stability-2026-09-16/03-Acceptance-Test-Catalog.md` rows T05, N02, S01, S02;
`app-platform-2026-09-18/03-Acceptance-Catalog.md` row Y03. Any failure is a KI in the
[register](2d-stability-2026-09-16/contracts/known-issues.md) before it is fixed.

## Checks that need a person or a clean machine

| Check | Needs | Origin |
| --- | --- | --- |
| Clean-machine install of the packaged PendulumLab and SF_Telem (K03/K04) | a disposable Windows VM | stability catalog K03/K04 (AP-P1 ran them on the dev machine) |
| `Starforge-Setup-<ver>.exe` on a machine without Visual Studio (SD04) | a disposable Windows VM | UX catalog SD04 |
| The root README Quickstart and guide 00 followed by a person, not a script (DOC02) | a person, a fresh clone or the SDK zip | AP-D1 proved it by script; UX-Q1 runs DG02 by driver |

## Log

| Date | SHA | Run | Result | Evidence |
| --- | --- | --- | --- | --- |
| — | — | nothing run yet | — | — |
