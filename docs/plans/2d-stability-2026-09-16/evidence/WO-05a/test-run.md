# WO-05a — build + test evidence

## Environment
- **Repo base:** `main` @ `490209e` (pre-commit). Working-tree changes for this WO:
  `tests/CMakeLists.txt` (+4, register the new file), new `tests/test_serial_shutdown_race.cpp`,
  new `docs/plans/2d-stability-2026-09-16/evidence/WO-05a/`. Untracked root plan file left untouched.
- **Machine:** DESKTOP-SEOA4BT — Win11 26200, VS18 2026 (Community), Windows SDK 10.0.26100.0,
  generator `Visual Studio 18 2026`, x64.
- **Mode:** `COSMIC_2D_ONLY=ON`, `COSMIC_BUILD_TESTS=ON` (existing `build/` cache; re-configured to pick
  up the new source — configure exit 0).
- **Serial environment probe:** `HKLM\HARDWARE\DEVICEMAP\SERIALCOMM` exposes exactly one node —
  `\Device\Serial0 = COM1` (a legacy 16550 UART node). This is **not** a Bluetooth SPP port and does not
  reproduce the ~10–20 s blocking `CreateFileA` that H1 needs; driving `Connect()` on it would open real
  hardware, which the suite avoids. So the live H1/H3 repro stays `ENVIRONMENT_BLOCKED` — missing
  prerequisite = a Bluetooth SPP port / com0com virtual pair / the WO-04 seam (not "any COM port").

## Commands + results

Configure:
```
cmake -S . -B build            # exit 0
```
Build (incremental — only the new TU + link):
```
cmake --build build --config Debug   --target CosmicTests    # exit 0, 0 warnings, 0 errors
cmake --build build --config Release --target CosmicTests    # exit 0, 0 warnings, 0 errors
```
Run (from `build/Runtime/<CONFIG>/`):
```
CosmicTests.exe --test-case="WO-05a*"    # the reachable guard cases
CosmicTests.exe                          # full suite
```

| Config | WO-05a cases | Full suite | Assertions | Skipped (ENV-BLOCKED seats) |
| --- | --- | --- | --- | --- |
| Debug   | 3 passed / 0 failed | **343 passed / 0 failed** | 116,591 passed | **2 skipped** |
| Release | 3 passed / 0 failed | **343 passed / 0 failed** | 116,588 passed | **2 skipped** |

Baseline (WO-02) was **340** cases. This WO adds **3 reachable** cases (340 → 343 passing) and **2
skipped** ENVIRONMENT_BLOCKED seats. Skipped cases are reported by doctest as skipped and are **not**
counted as passes (honest-gates, rule 7).

## What each case demonstrates
- *teardown joins a reconnect worker that is in flight (reachable path)* — the exact `BeginOpen → Close(join)`
  ownership-chain contract the exit-hang rides on; bounded here because COM999 fails `CreateFileA` fast.
- *repeated reconnect churn then teardown* — the 3 s auto-reconnect retry shape, race/leak-free at teardown.
- *SerialLink pumped without Connect never opens a port (KI-2 boundary)* — auto-reconnect `BeginOpen`
  is gated on `m_WantConnection` (set only by `Connect()`), so no headless path opens a port; we do not
  call `Connect()` (would open the real COM1).
- *H1 exit hang* / *H3 connected-state drop* — **skipped, ENVIRONMENT_BLOCKED**, each naming the missing
  prerequisite and pointing at the seam spec. These are the failing reproductions WO-05 lights up on the
  WO-04 seam.

## Honest disposition
No fix shipped (WO-05a is diagnose-only). No golden regenerated, no tolerance loosened, no scope broadened.
The reported crash's most-likely mechanism (H1 exit soft-hang) is confirmed from source
(`hazard-analysis.md`); the true connected-state crash surface (H2/H3) is env-blocked and handed to WO-05
with the seam spec it needs.
