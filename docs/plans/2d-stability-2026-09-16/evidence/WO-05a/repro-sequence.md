# WO-05a — reproduction status and scripted sequences

- **Base:** `main` @ `490209e`, `COSMIC_2D_ONLY=ON`.
- **Ordered per the WO:** (1) targeted production-path test, (2) live app + virtual/real COM, (3) if no
  device/virtual COM → `ENVIRONMENT_BLOCKED` + source hazard analysis + the deterministic reachable repro.

## Environment probe (what is actually available)

| Prerequisite | Present? | Evidence |
| --- | --- | --- |
| Real serial/COM device | **No** | Standing campaign decision (no COM/serial hardware). `SerialPort::GetAvailablePorts()` reads `HKLM\HARDWARE\DEVICEMAP\SERIALCOMM` (`SerialPort.cpp:311-341`); no ports enumerate. |
| Virtual COM pair (com0com / com2tcp) | **No** | Standing campaign decision — none installed; installing a kernel-mode null-modem driver is out of scope for this spike and not authorized. |
| Bluetooth SPP port (the port class that actually blocks `CreateFileA` 10–20 s) | **No** | none paired. |
| Injectable transport seam in `SerialPort`/`SerialLink` | **No** | Does not exist yet — this is exactly what WO-04 must add (KI-2; `transport-seam-spec.md`). |

**Result:** step (2) the **live app + COM repro is `ENVIRONMENT_BLOCKED`**, and step (1) a *failing*
production-path test is also blocked because the connected state (a port that opens/receives/drops)
has no headless entry point today. A blocked live repro is **not** "no bug" — see H1 in
`hazard-analysis.md`, which is confirmed from source alone. The deterministic artifact that *is*
buildable now is the reachable-path guard + reserved seats in `tests/test_serial_shutdown_race.cpp`
(step 1, as far as the missing seam allows).

Missing prerequisite to lift the block, named explicitly: **one Bluetooth SPP port OR a com0com virtual
pair, OR (preferred, deterministic, CI-friendly) the WO-04 injectable transport seam.**

---

## Scripted manual reproduction — symptom (a)+(b) unified (run once a COM/BT port exists)

This is the sequence that reproduces **H1** on real Windows. It is written so WO-05 can run it verbatim
the moment a port is available; each step names the observable.

**Setup:** a Bluetooth SPP COM port (e.g. an ESP32 paired as `COMn`) that can be powered off, OR a
com0com pair where the peer end can be closed.

1. Launch the engine, open **SF_Telem**, go to **Main**.
2. In the **Serial Link** panel: select `COMn`, leave **Auto-reconnect** ON, click **Connect**. Confirm
   the status reads `RECEIVING (COMn)` (or `OPEN - no data` for com0com with no writer).
   - *State reached:* `m_WantConnection=true`, port open, read thread live.
3. **Drop the link:** power off the ESP32 / close the peer com0com handle. Status flips to
   `OPEN - no data … retrying…` then `RECONNECTING (COMn)…`. Auto-reconnect is now spawning a worker in
   `CreateFileA` every ~3 s (`SerialLink.cpp:56-68`).
4. **Within a retry window** (they overlap most of the time on a BT port), **close the app window** (title-bar
   ✕ / Alt-F4).
5. **Observe — expected FAIL:** the window disappears but the process does not exit promptly; it lingers
   until the in-flight `CreateFileA` returns (~10–20 s on BT SPP). Windows may mark it "not responding."
   - *Instrumentation:* attach a debugger and break-all during the stall → the main thread is parked in
     `SerialPort::Close` → `std::thread::join` → (worker) `DoOpen` → `CreateFileA`
     (`SerialPort.cpp:298`, worker at `:99`). Capture with:
     `procdump -ma <pid>` at the stall, or Task Manager → Analyze wait chain → "waiting on thread … CreateFile".
   - *PASS criterion after the WO-05 fix:* process exits within the abort budget (target: sub-second),
     the open being cancellable / the worker detached-under-abandon.

**Why (a) and (b) are the same bug:** step 3 (*lose the connection*) is what starts the worker churn that
step 4/5 (*close*) then blocks on. "Close while opening" (a) is the same join on a worker that happens to
still be in its first `CreateFileA`.

### Crash-vs-hang note
On BT SPP this manifests as a **multi-second freeze on exit**; a user force-killing the "not responding"
process reads as a crash. A hard *fault* (rather than a hang) is only expected in the connected-state
race surface (H2/H3), which is `ENV-BLOCKED` and deferred to the WO-05 matrix.

---

## Deterministic reachable repro (buildable now) — `tests/test_serial_shutdown_race.cpp`

What can be exercised headlessly today, on the real production API, without hardware and without
reimplementing the state machine:

- **Reachable guard (PASS, characterization):** drive `SerialPort::BeginOpen("COM999")` (the well-formed
  but nonexistent port that fails `CreateFileA` immediately) to spawn a real connect worker, then run the
  teardown `Close()` while that worker is in flight — the exact `BeginOpen → Close(join)` contract the
  exit-hang rides on. Asserts no crash, bounded join, final `State::Idle`. With a *fast-failing* port the
  join is sub-second; the in-code comment records that a **blocking** open turns this identical join into
  H1's exit hang (the part that needs the seam/hardware to show).
- **Two reserved `ENV-BLOCKED` seats (skipped, not passed):**
  - `EXIT HANG: close while a reconnect worker blocks in CreateFileA` — needs a port whose open blocks.
  - `CONNECTED-STATE DROP: lose an open port then close` — needs the WO-04 transport seam or a virtual COM.
  Each emits a doctest `MESSAGE` naming the missing prerequisite and pointing here + at the seam spec.

Skipped cases report as **skipped**, never as passes (honest-gates, rule 7). See `test-run.md` for the
exact command, config, exit code, and counts.
