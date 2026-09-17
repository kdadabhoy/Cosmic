# WO-05a — Reproduce the reported COM crash early

**Gate:** feeds G3 · **Depends on:** WO-02 only · **Acceptance:** feeds T03/T04 (diagnosis, not the full matrix) · **Status:** DONE 2026-09-17 — diagnosis delivered (KI-4); live BT/virtual-COM repro ENVIRONMENT_BLOCKED; seam spec handed to WO-04. Evidence: `../evidence/WO-05a/` + `tests/test_serial_shutdown_race.cpp`.

New work order from the review. Your #1 reported pain is the COM close/link-loss crash. This spike
**reproduces and diagnoses it before the full acceptance runner (WO-04) exists**, so the runner is
designed around a known failure and the highest-value fix is not gated behind the whole apparatus.
It does **not** ship the fix — that lands in WO-05 with the regression.

## Copy-paste prompt

~~~text
Execute only WO-05a from the Cosmic 2D stability packet. Read work-orders/README.md and finding
F2/F3 in ../05... context. Work on main (main-only campaign), COSMIC_2D_ONLY=ON. Goal: REPRODUCE, then
DIAGNOSE — do not land a fix here.

Target the reported symptoms: (a) close the app window while a COM port is opening/open, and
(b) lose the connection while a port is open, then close. Study the real ownership chain, not an
isolated SerialPort:
  - m_Link (SerialLink) is owned by the SF_Telem ROOT and shared with the screens
    (Projects/SF_Telem/src/SF_Telem.cpp:93), driven every frame by m_Link.OnUpdate(ts)
    (SF_Telem.cpp:104);
  - auto-reconnect calls m_Port.BeginOpen(...) in the background (Cosmic/src/serial/SerialLink.cpp:67);
  - teardown runs SF_Telem::OnDetach (SF_Telem.cpp:84-93): Testing.Shutdown, TelemHub.Shutdown,
    Link.Shutdown — confirm the host actually calls OnDetach on window close, and whether any
    OnUpdate/reconnect tick can run concurrently with or after teardown begins.

Reproduce with whatever is available, in this order, and label each honestly:
  1. a targeted production-path test that drives the shared-link teardown while a connect worker /
     reconnect is in flight (fake transport allowed to control open/read timing — do NOT
     reimplement the state machine);
  2. the live app on real Windows with a virtual COM (com0com) or a real device if present;
  3. if no device/virtual COM is available, document it as ENVIRONMENT_BLOCKED and deliver the
     source-level hazard analysis + the deterministic fake-transport repro from step 1.

Deliver a failing reproduction (test or a scripted manual sequence with a crash dump / log), a
root-cause hypothesis with file:line, and the exact injectable-transport seam WO-04 must add for
the WO-05 connected-state matrix. Commit the repro + analysis locally per D-commit (author
kdadabhoy, no AI trailer). Do not push. Do not fix unless the fix is trivial AND you also add the
failing-before/passing-after evidence — otherwise leave it for WO-05.
~~~

## Scope
- **In:** reproduce + diagnose the reported close/link-loss crash via the real shared-ownership
  chain; specify the transport seam WO-04 needs.
- **Out:** the full T03/T04 lifecycle matrix (WO-05); shipping the fix (WO-05); the acceptance
  runner (WO-04).

## Deliverables
- A failing reproduction (deterministic test and/or documented crash sequence + dump/log).
- Root-cause hypothesis with `file:line`.
- The injectable-transport seam spec handed to WO-04.
- An entry appended to `known-issues.md`.

## Done when (DoD)
Either a real reproduction exists with a diagnosis, or the source-level hazard is analysed and the
live reproduction is explicitly `ENVIRONMENT_BLOCKED` on missing hardware/virtual COM — never
counted as "no bug." The seam WO-04 must build is specified.

## Rollback
Spike work on the candidate — discard the repro branch/patch; nothing shipped.
