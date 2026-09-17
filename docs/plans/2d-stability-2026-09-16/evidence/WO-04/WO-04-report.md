# WO-04 - acceptance runner + injectable serial transport seam

**Gate:** G2 · **Acceptance:** H01-H04 · **Status:** DONE (local commit; not pushed).
**Deps:** WO-02 baseline + WO-05a seam spec (`../WO-05a/transport-seam-spec.md`).

## 0. Revalidated state (packet said "revalidate, don't assume")

- The WO-04 prompt's "state since packet authored" was already stale. At start:
  `HEAD == origin/main == cdee06d` (Kaden had pushed WO-03, the 3D-project deletion,
  WO-05a, and the WO-04 prompt refresh). Working tree clean apart from the untracked
  root plan file `Cosmic - 2D Trunk Consolidation & Acceptance Plan.md` (**left
  untouched** per rule 2). Main-only (D-WORKFLOW); no candidate branch/worktree touched.
- Toolchain: DESKTOP-SEOA4BT, Win11 build 26200.9457, VS 18 2026 (Community),
  Windows SDK 10.0.26100, generator `Visual Studio 18 2026`, cmake 4.3.1-msvc1, x64.
  Ryzen 7 7800X3D, 31.2 GiB, NVIDIA RTX 5070 Ti - the WO-02 reference machine.
- Pre-change baseline reconfirmed fresh (not assumed): Debug **343 passed / 0 failed /
  2 skipped**, 116,591 assertions - matches WO-05a exactly.
- Mode: `-DCOSMIC_2D_ONLY=ON` (existing cache; re-configured for the new sources).

## 1. Deliverable 1 - injectable serial transport seam

Built EXACTLY the spec in `../WO-05a/transport-seam-spec.md`. Injected ONLY at the OS
boundary; the entire state machine is real and untouched.

### Files
- **new** `Cosmic/src/serial/ISerialTransport.h` - `ReadResult{bytes,status}` +
  `ISerialTransport{Open,Read,Write,Close,List}`.
- **new** `Cosmic/src/serial/Win32SerialTransport.{h,cpp}` - today's OS calls, verbatim;
  the DEFAULT transport. It is the shipping code.
- **edit** `Cosmic/src/serial/SerialPort.{h,cpp}` - `m_Handle` moved into the transport;
  added `std::unique_ptr<ISerialTransport> m_Transport` (default = Win32), a test-only
  `explicit SerialPort(std::unique_ptr<ISerialTransport>)` ctor, and instance
  `ListPorts()`. `DoOpen`/`ReadLoop`/`Write`/`CloseReadSession` now call `m_Transport->...`.
- **edit** `Cosmic/src/serial/SerialLink.{h,cpp}` - test-only pass-through ctor
  `explicit SerialLink(std::unique_ptr<ISerialTransport>)`; `RefreshPorts()` discovers
  through `m_Port.ListPorts()` (the seam) instead of the static registry scan.
- **new** `tests/FakeSerialTransport.h` - **tests/ only**; the fake with exactly the spec
  controls: `SetOpenResult`, `SetOpenBlockMs` (blocks inside `Open`, honouring the stop
  handle - the KI-4 lever), `PushBytes`, `SignalDrop`, `SetAvailablePorts`,
  `SetWriteResult`, and `OpenCount/CloseCount/Written` observers.
- **new** `tests/test_serial_seam.cpp` - the seam's own smoke coverage (5 cases).
- **edit** `tests/CMakeLists.txt` - registers `test_serial_seam.cpp` (shared tier).

### What stayed real (NOT moved into the seam)
Both `std::thread`s, `m_Abandon`, the manual-reset **stop event** (`m_StopEvent`, still
owned and signalled by `SerialPort` - it is synchronization, not transport), every
`State` transition (`Open`/`BeginOpen`/`Close`/`CloseReadSession`), and SerialLink's
auto-reconnect policy. The seam replaces the four Win32 syscalls + the registry scan and
nothing else. No raw-handle exposure, no private-flag mutation, no second simulated app,
no parser reimplementation. KI-4 is **left unfixed** (that is WO-05): `Close()` still
joins the connect worker and `m_Abandon` is still read only after the open returns.

Behaviour-preserving detail: `m_StopEvent` is now created just before the transport
`Open` (so a fake `Open` can observe cancellation); the Win32 transport ignores it
(CreateFileA cannot be cancelled - that inability *is* KI-4). The belt-and-suspenders
`CancelIoEx` moved into `Win32SerialTransport::Close()` after the read-thread join; the
reader still wakes on the stop event, so the join stays bounded exactly as before.

### Inertness proof (shipping build is byte-identical behaviour)
Raw output: `seam-and-inertness.txt`.
- `test_serial_lifecycle.cpp` re-run green against the Win32 **default** BEFORE any fake
  is wired: **16 cases / 855 assertions, 0 failed** (Release). Same behaviour as before.
- Production uses only the default ctors: `SF_Telem` holds `Cosmic::SerialLink m_Link;`
  by value; a grep of `Cosmic/` + `Projects/` finds **no** call that passes a transport
  argument. `FakeSerialTransport` appears **only** under `tests/`.
- Shipping `Cosmic.dll` (Release): `dumpbin /EXPORTS` -> **0** `Fake` symbols; raw string
  scan -> **0** `FakeSerialTransport` occurrences. The fake does not ship.
- Packaging is `cmake --install`-driven (`package.bat` STAGE 3). `CosmicTests` has **no**
  `install()` rule, so the test exe - and with it the fake - is never staged/packaged.
- Both configs stay **0-warning** and the full unit suite is green (see below).

### Seam smoke test (WO-04's own coverage) - both configs
`test_serial_seam.cpp`, driving the REAL SerialLink/SerialPort commands over the fake:
- default-is-Win32 + injected-list wiring (2 cases).
- **(a)** `SetAvailablePorts + Connect + PushBytes` -> `IsReceiving()` true,
  `ConsumeJustConnected()` one-shot, `Poll()` returns the exact bytes.
- **(b)** `SignalDrop` -> `State::Failed` -> auto-reconnect re-opens (`OpenCount >= 2`,
  back to `State::Open`).
- **(c)** `SetOpenBlockMs(1200) + Connect + teardown` -> teardown measured at **~1.20 s**
  in both Debug and Release: the KI-4 join-on-a-blocked-open is now directly observable
  under test (bounded, clean `State::Idle`). Not fixed here - that is WO-05.

Results (raw in `seam-and-inertness.txt`):

| Config | Full suite | Seam cases | KI-4 case (c) teardown |
| --- | --- | --- | --- |
| Debug   | **348 passed / 0 failed / 2 skipped**, 116,620 assertions | 5/5 | 1.201 s |
| Release | **348 passed / 0 failed / 2 skipped**, 116,617 assertions | 5/5 | 1.202 s |

348 = baseline 343 + 5 new seam cases. The 2 skipped are WO-05a's H1/H3
ENVIRONMENT_BLOCKED seats - **left skipped and untouched** (WO-05 un-skips them on this
seam). `test_serial_shutdown_race.cpp` still: 3 reachable pass, 2 skipped.

## 2. Deliverable 2 - minimal acceptance runner

`tests/acceptance/`: `Run-Acceptance.ps1` + `AcceptanceRunner.psm1` +
`manifests/selftest.manifest.json` + `fixtures/` + `goldens/` + `README.md`. Windows
PowerShell 5.1 compatible; all manifest paths parameterized (`{BIN}`, `{ACCEPTANCE}`,
`{RUNTEMP}`, `{USERDATA}`, ...) - no absolute user paths.

- Process runner with a per-run isolated temp + user-data dir (child `TEMP`/`TMP`/
  `COSMIC_USER_DATA` redirected there); independent per-case deadlines; JSON + JUnit
  evidence capturing commit SHA, dirty-diff hash, mode/config, environment (OS build,
  CPU, RAM, GPU/driver), capabilities, and per case the exact command, exit code,
  crash/hang evidence, duration, seed, fixture hash, and log paths.
- Tier separation U/W/G/I/Q; capability gating; missing capability => ENVIRONMENT_BLOCKED
  (named prerequisite); a filtered profile with zero cases => FAILURE.

### H01-H04 self-test (proof) - `acceptance-selftest/`
Run: `Run-Acceptance.ps1 -Manifest manifests/selftest.manifest.json -SelfTest
-DisableCapability gpu-gl -Config Release`. Each case declares an `expectVerdict`; the
runner asserted it classified **every one** exactly as expected -> `[SELF-TEST OK]`, exit
0. (`gpu-gl` forced off to exercise the GPU-blocked branch on a GPU machine; the forced
removal is recorded, never faked as real absence.)

| Case | Tier | Verdict | Proves |
| --- | --- | --- | --- |
| H01a intentional nonzero exit | W | FAILED (exit 7) | H01: nonzero exit caught |
| H01b crashing fixture | W | FAILED (crash, exit -2146232797) | H01: crash caught, logs preserved |
| H01c passing fixture | W | PASSED | H01: a good fixture passes |
| H02 hanging child | W | TIMEOUT (2 s) | H02: timeout=FAILED, child-tree killed, parent survived |
| H03 missing fixture | U | MISSING | H03: missing fixture fails |
| H03 missing-test zero count | U | FAILED | H03: zero-tests-run is a failure (minTests) |
| H03 missing golden | G | MISSING | H03: missing golden fails; goldens not regenerated |
| H04 serial hardware | W | ENVIRONMENT_BLOCKED | H04: no phantom pass (serial-device absent by policy) |
| H04 gpu | G | ENVIRONMENT_BLOCKED | H04: GPU-blocked branch |
| H04 fake transport | W | PASSED (transport=fake) | H04: fake case runs and self-identifies |
| Hnormal minimal | U | PASSED | normal minimal case passes end to end |

Corroborating evidence (`results.json`): `golden_mutated=False`, H02
`child_alive_after_kill=False` + `parent_alive=True`, JUnit parses (11 tests / 6
failures / 2 skipped), per-case logs preserved. Extra honest-gate proofs
(`honest-gates.txt`): `-UpdateGoldens` in acceptance mode => refused (exit 3); zero cases
selected => FAILURE (exit 2).

## 3. Acceptance / serial IDs: executable vs still environment-blocked

**Now executable headlessly (seam-driven, WO-04):**
- H01, H02, H03, H04 (runner self-tests) - all pass their oracle.
- The connected-state serial edges the seam unblocks: `IsReceiving`,
  `ConsumeJustConnected` one-shot, `Poll` exact bytes, drop->`Failed`->auto-reconnect,
  and the KI-4 teardown-join being observable - proven by `test_serial_seam.cpp` (a).(b).(c).
- This makes the **W-tier serial** cases (T03 fake/delayed-transport half) authorable on
  the seam by WO-05, and the WO-05a H1/H3 seats un-skippable by WO-05.

**Still ENVIRONMENT_BLOCKED (prerequisite named; not a pass):**
- Real serial hardware / virtual COM / Bluetooth SPP - T04, and the physical half of
  T03/T06/S01. Prereq: a virtual-COM pair or SPP device (none; campaign standing rule).
- GPU render tiers (R01-R07, G/I cases) and editor-UI tiers (L05, P01, I/Q) - authored
  by their own WOs; the runner gates them by `gpu-gl` / `editor-ui` capability.
- KI-4 **fix** itself (failing-before/passing-after on the H1 seat) - WO-05.

## 4. Honest disposition + KI register
No golden regenerated, no tolerance loosened, no scope broadened, no retry-until-green.
No **new** crash/hang/data-loss discovered by this WO (KI-4 was already registered by
WO-05a; the seam makes it observable, exactly as scoped). KI-2 updated in
`work-orders/README.md`: the seam is built; the connected state is reachable under test;
the full matrix + fix remain WO-05. Rollback: the runner + seam are additive; revert the
commit to remove them (no behaviour change to production serial paths).
