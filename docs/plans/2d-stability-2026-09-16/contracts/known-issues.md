# Cosmic 2D — known-issue register

Status: WO-00 decision record, 2026-09-16. Gate G0.
Revalidated at HEAD `72b47771c869666f3a645a47bfbfae917d3167f2`.

**This is the running register. Every later work order appends to it.** Add every newly discovered
crash, data-loss, hang, deadlock, use-after-free, stale callback, or silent-corruption defect here,
each with a minimal regression and a disposition. A missing fixture, a skipped test, or missing
equipment is **never** logged here as a pass — it is `ENVIRONMENT_BLOCKED` in the acceptance run.

Append format (copy the block below for a new entry):

```
### KI-N — <one-line title>
- Status: <Confirmed defect | Coverage gap | Enforcement gap | Suspected / not reproduced>
- Owner WO: <WO-xx>
- Anchor: <file:line at HEAD sha>
- Repro: <minimal steps / command / fixture>
- Regression: <test id/path once written; "none yet" until then>
- Disposition: <open | fix landed <sha> | won't-fix (reason)>
```

---

### KI-1 — Editor viewport snap-chip aborts on click (ImGui style-stack imbalance)

- **Status:** Confirmed defect.
- **Owner WO:** WO-07 (fix; L05 asserts ImGui stack balance).
- **Anchor:** `Projects/Starforge/src/ViewportController.cpp:1280-1310` — the `snapChip` lambda.
  The imbalance is the `PushStyleColor` at `:1288` (guarded by `if (on)` at `:1286-1289`) paired
  with the `PopStyleColor` at `:1293` (guarded by `if (on)` at `:1292`), while the button in
  between **flips `on`** (`:1290-1291 if (ImGui::Button(...)) on = !on;`). On the click that
  toggles a chip, the push and pop see opposite values of `on` → the ImGui colour stack is left
  unbalanced by one.
- **Reachability:** the three `snapChip(...)` calls (`:1306-1313`, Move/Rotate/Scale) render
  **before** the `#ifndef COSMIC_2D_ONLY` fence at `:1337`, so the bug **ships in the 2D editor**.
- **Contrast (proof it is a real, already-understood pattern):** the sibling `toggle` lambda at
  `:1314-1332` was already fixed — it latches `const bool pushed = on;` at `:1322` and guards both
  push (`:1326`) and pop (`:1331`) on `pushed`, so the flip cannot unbalance it. `snapChip` never
  received the same fix.
- **Repro:** launch Starforge (Debug, `COSMIC_2D_ONLY=ON`), open a scene, click any of the three
  snap chips in the viewport strip. Debug: assert + `abort()`. Release: silent style-stack
  corruption. The Phase 29 W7 on-GPU pass already observed this in both 2D and 3D editors.
- **Regression:** none yet. WO-07 writes a failing-before repro (headless or UI-tier) asserting the
  ImGui colour-stack depth is balanced across a simulated chip toggle, then applies the `pushed`-latch
  fix, then the same assertion passes (L05).
- **Fix shape (for WO-07, not implemented here):** mirror the `toggle` lambda — latch
  `const bool pushed = on;` before the button and guard the pop on `pushed`.
- **Disposition:** open.

### KI-2 — SerialLink connected-state behaviour is unreachable headlessly (coverage gap)

- **Status:** Coverage gap (not a proven defect — a hole where the reported COM crashes could hide).
- **Owner WO:** WO-04 (build the injectable transport seam), WO-05 (use it for the connected-state
  matrix). WO-05a reproduces the *reported* close/link-loss crash early.
- **Anchor:** `Cosmic/src/serial/SerialLink.cpp:56-70` — auto-reconnect: it calls
  `m_Port.BeginOpen(...)` at `:67` on the reconnect interval — and the one-shot edge
  `ConsumeJustConnected` at `:115`. Neither is reachable without a port that actually opens.
- **Stated in source:** `tests/test_serial_lifecycle.cpp:14-20` (the "COVERAGE NOTE") — SerialLink
  has no injectable byte transport and no public port setter, so connected-state (auto-reconnect
  timing, the `ConsumeJustConnected` edge) is *not reachable headlessly*; the injectable transport
  is explicitly deferred (plan doc 28 §9.6). A rapidly-failing `COM999` proves the unreachable-port
  policy at the `SerialPort` level but does **not** reproduce a stalled Bluetooth open, a pending
  read, a mid-stream link loss, or close-with-live-port.
- **Related source facts (not defects, but the surface WO-05 must test):**
  `SerialPort::Close` joins the connection worker (`SerialPort.cpp:11,26,40`), and `BeginOpen`
  joins any prior connect thread before starting a new one (`SerialPort.cpp:66`) — a Bluetooth SPP
  port can sit inside `CreateFileA` for 10–20 s, which is why bounded cancellation + caller/thread
  ownership need an explicit contract (see [`contracts.md`](contracts.md) §1).
- **Repro:** cannot be reproduced headlessly today — that is the gap. WO-04 adds a narrow
  fake-transport seam (controls byte delivery / open outcome / timing; does **not** reimplement the
  parser or the connection state machine); WO-05/WO-05a then drive delayed-open, abandoned-open,
  pending-read, link-loss, and close-with-live-port with completion barriers, corroborated on real
  Windows virtual-COM + USB/Bluetooth hardware where available (T03/T04).
- **Regression:** none yet (owner WO-04 seam → WO-05 matrix).
- **Disposition:** open (coverage). Any actual crash found via the new seam becomes its own
  Confirmed-defect KI entry.

### KI-3 — Trunk does not enforce 2D; CI cache key omits build mode (enforcement gap)

- **Status:** Enforcement gap.
- **Owner WO:** WO-03.
- **Anchors (all at HEAD):**
  - `CMakeLists.txt:67` — `option(COSMIC_2D_ONLY "Build the 2D-only engine (excludes all 3D subsystems)" OFF)`: the default is **OFF** (3D).
  - `.github/workflows/ci.yml:41` — `cmake -S . -B build -A x64 -DCOSMIC_BUILD_TESTS=ON`: CI configures **without** `-DCOSMIC_2D_ONLY=ON`, so it builds the default 3D engine.
  - `.github/workflows/ci.yml:36` — cache key `cmake-build-${{ runner.os }}-${{ hashFiles('CMakeLists.txt', 'Cosmic/CMakeLists.txt', 'Runtime/CMakeLists.txt', 'tests/CMakeLists.txt') }}`: the key is keyed on CMake **file hashes only** — it omits the build mode / the `COSMIC_2D_ONLY` value. Flipping the flag without editing those files reuses a **stale, opposite-mode** `build/`.
  - `.github/workflows/release.yml:24` — `cmake -S . -B build -A x64 -DCOSMIC_BUILD_TESTS=OFF`: release staging also omits the 2D flag.
  - `package.bat:48` — `if exist build rmdir /s /q build` discards the cache, then `:54/:56`
    `cmake .. [-A x64] -DCOSMIC_BUILD_ENGINE_ONLY=OFF` configures **without** the 2D flag → a clean
    package can ship 3D.
- **Repro:** on a checkout with a locally-selected 2D cache, run a fresh default configure (or CI /
  `package.bat`): the result is the 3D engine, because none of the default entry points sets the
  flag. Then flip the flag in CI without touching the four hashed CMake files: the cache restore
  serves a stale opposite-mode `build/`.
- **Regression:** none yet. WO-03 makes default/CI/scaffold/packaging/release explicitly select the
  supported 2D mode (or reject an unsupported OFF on the trunk), and folds build **mode** +
  architecture + toolchain into the cache key. B03–B05 prove it from both clean and stale-cache
  scenarios; a release job cannot bypass the 2D contract.
- **Policy tie-in:** the recommended enforcement is *default ON + configure-time rejection of OFF on
  the supported trunk, OFF retained only on `engine-3d`* — see [`contracts.md`](contracts.md) §6 and
  [`support-matrix.md`](support-matrix.md).
- **Disposition:** open.

---

## Register invariants

- No entry is closed without a landed regression (or an explicit reviewed won't-fix with reason).
- A defect found while executing a later WO is added here **before** it is fixed, with its
  failing-before evidence.
- Equipment/fixture unavailability is tracked as `ENVIRONMENT_BLOCKED` in the acceptance evidence,
  never as a closed KI and never as a pass.
