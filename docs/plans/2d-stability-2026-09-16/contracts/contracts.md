# Cosmic 2D — draft contracts

Status: WO-00 decision record, 2026-09-16. Gate G0.
Revalidated at HEAD `72b47771c869666f3a645a47bfbfae917d3167f2`.

Each contract below is a **draft**. Every one carries the marker
**"draft — ratified with WO-02 measurements"**: the deadlines, budgets and tolerances are proposed
requirements, not measured facts, until WO-02 establishes a baseline on a named reference machine
(see [`numeric-bar-policy.md`](numeric-bar-policy.md)). WO-00 fixes the *shape* of each contract
(what is promised, who owns it, what invalidates it); WO-02 fills the numbers; the owning WO
implements and proves it.

Open questions that need Kaden are marked **[Kaden]** inline and collected at the end.

---

## 1. Bounded serial close / cancel

*Draft — ratified with WO-02 measurements. Owner: WO-05 (contract), WO-04 (seam). Covers T03/T04,
KI-2.*

**Promise.** Every serial lifecycle operation (open, delayed/stalled open, pending read, in-flight
write, link loss, reconnect, close) reaches an **observable completion or failure state within a
bounded deadline**, with no deadlock, no use-after-free, no stale callback after teardown, and no
detached worker that still references a destroyed object.

**Ownership / threads.**
- `SerialPort::Close` joins the connection worker (`SerialPort.cpp:11,26,40`); `BeginOpen` joins any
  prior connect thread before starting a new one (`SerialPort.cpp:66`). The connection worker owns
  the blocking `CreateFileA`; a Bluetooth SPP port can sit inside it for **10–20 s**.
- The permitted caller/thread contract must be stated explicitly: which thread may call `Close`,
  what it is allowed to block on, and for how long. **A blocking shutdown must not be "fixed" by
  detaching a thread that still points at freed storage** — bounded cancellation, not fire-and-forget.
- Overlapped I/O storage must outlive its OS operation's completion; a late open/read result is
  cleaned exactly once.

**Deadlines (proposed, measurement-derived — see numeric-bar-policy).**
- Normal close with an idle / open / stalled port and no large pending export: process/app exits
  within **2 s**; no post-unload callback. **[Kaden]** Is 2 s the accepted normal-close deadline
  given a live SPP `CreateFileA` stall, or does a stalled-open close return earlier and clean the
  late handle once, asynchronously? The test must **not** extend the deadline to the known 10–20 s
  Bluetooth stall and then call shutdown "bounded."
- Delayed-connect cancellation: UI stays responsive; completion/cleanup is bounded; no unsafe
  detached worker.
- Close with a pending recording export: an explicit saving/cancel/error state stays responsive;
  successful completion within **30 s** for the declared 2-hour fixture, *or* an explicit failure
  that does **not** claim data was saved.

**Invalidated by / re-opened when:** WO-05a reproduces the reported close-after-open or
link-loss-while-open crash (then this contract gains the exact reproduced sequence), or WO-02
measures a normal-close time that forces a different deadline.

---

## 2. Recording durability + max session / retention policy

*Draft — ratified with WO-02 measurements. Owner: WO-06 (contract), corroborated in S01. Covers
D03/D04/D05.*

**Promise.** A recording either completes to a **valid, loadable** file or fails with an explicit
error; the **last committed snapshot always remains loadable**; no invalid/partial file is ever
reported as a valid full recording.

**Durability.**
- Disk-full, permission-denied, partial-write, invalid-path, interrupted-autosave, and
  shutdown-during-flush are all handled with a truthful result and a preserved last-good file.
- The **actual data-loss window is measured**, not assumed. The 5-second autosave interval is
  **not** a proven crash-loss bound until snapshot + write duration and successful completion are
  measured (D05). No "five-second loss bound" claim without that measurement.

**Retention / volume (proposed — measurement-derived).**
- `DataRecorder` grows by design: per-entity `std::vector<float> timestamps` and
  `std::vector<std::vector<float>> columns` (`DataRecorder.h:224-225`) are appended, `Flush` takes a
  full snapshot, and CSV export widens to `double` columns (`DataRecorder.cpp:337`,
  `DataExport.cpp:37`). `ReserveCapacity(18,000)` is **preallocation, not a cap**.
- 60-Hz recording for 2 h ⇒ 432,000 stored samples/entity; raw float history ≈
  `Σ 4·sample_count·(1+channel_count)` bytes, plus vector-capacity / metadata / simultaneous-snapshot
  / CSV-double overhead.
- A **supported maximum session length / data volume**, an explicit **stop-or-rotate** policy, and
  recoverable limit behaviour must be documented. **No infinite-recording promise.** **[Kaden]** What
  is the supported maximum session length / data volume, and is the over-limit behaviour stop, rotate,
  or reject?
- Provisional process private-bytes ceiling for the 2-hour recording: **2 GiB** (with
  capacity/snapshot/CSV-temporary growth accounted separately).

**Invalidated by / re-opened when:** WO-02/WO-06 measures snapshot+write duration (sets the real
loss window) or the 2-hour memory profile (sets the real ceiling).

---

## 3. Restricted numeric CSV grammar

*Draft — ratified with WO-02 measurements. Owner: WO-06. Covers D06.*

**Promise.** Cosmic's CSV is a **restricted numeric** format, **not** a general quoted-CSV parser
(unless Kaden separately chooses general CSV import). Finite numeric values round-trip; invalid
input is **rejected with a useful result**, never coerced into a successful dataset.

**Grammar (draft).**
- Rows must be exactly rectangular. Ragged rows (too few / too many columns) are **rejected** — this
  is already asserted (`test_lookuptable.cpp` ragged-row case; loader in `Cosmic/src/utils/DataExport.cpp`
  and `DataPlayer.cpp`). Preserving this rejection is a *feature*, not a regression.
- Cells are finite numbers. Blank / whitespace-only numeric cells are **rejected**, **not** silently
  coerced to zero. Numeric-prefix-with-trailing-junk is rejected.
- LF and CRLF line endings accepted; exponential notation, `+0`/`-0`, small/large finite doubles
  accepted. Output uses `max_digits10` (`DataExport.cpp:70`); on-disk representation is
  **locale-invariant**.
- Header vs headerless numeric tables supported; duplicate / numeric-looking headers, BOM, Unicode
  paths, quoted commas/newlines each get an **explicit** supported-or-rejected verdict.
- NaN / Inf / overflow / underflow and locale changes have a chosen finite-data policy, asserted
  consistently.
- **[Kaden]** Confirm the format stays restricted-numeric (recommended), and confirm signed-zero
  preservation is required (declared) vs not. A generic reader is **not** a MATLAB/JPL/Horizons
  importer — a real consumer schema needs its own fixture/adapter.

**Invalidated by / re-opened when:** Kaden opts into general CSV import (a scope change, not a bug),
or a consumer schema is adopted.

---

## 4. Runtime-plugin vs editor-module reload state

*Draft — ratified with WO-02 measurements. Owner: WO-07. Covers L01–L04, M1.*

**Promise.** The two DLL lifecycles have **different, explicit** state contracts. Neither promises
preservation of arbitrary running C++ state.

**Runtime plugin (`Application` runtime plugin).**
- `Application` adopts ImGui/ImPlot contexts **before** creating the runtime plugin. Any module UI
  usage must run under a validly adopted context.
- Reload = defined **reset / reopen** behaviour. `OnDetach` / destructors run **before** `FreeLibrary`
  and GL-context loss. No callback fires after unload; owned resources (texture, framebuffer,
  script/component, listener, log sink, job, watcher, audio handle) net to zero after quiescence.

**Editor game module (Starforge `GameModule`).**
- `ReloadModule` **preserves the serialized edit-scene and reflected/custom fields**, **clears
  selection and undo**, and **stops Play** (documented, supported rule). Old module code stays loaded
  until its objects die; registry entries neither accumulate nor vanish incorrectly.
- Running `Starforge.exe --project` exercises only the runtime-host override — it is **not** proof of
  the editor game-module lifecycle.
- **[Kaden]** Confirm "clears selection/undo, stops Play, preserves serialized scene" is the intended
  editor-reload contract (recommended, matches source), i.e. no promise to preserve live undo history
  or in-flight Play state across a reload.

**Invalidated by / re-opened when:** a reload path is proven to leak an owned resource or invoke a
stale callback (becomes a KI), or the editor-reload rule is changed by decision.

---

## 5. Numeric precision — float telemetry vs double scientific source

*Draft — ratified with WO-02 measurements. Owner: WO-10 (contract), consumer deferral per D-9km.
Covers N03/N04, X01.*

**Promise.** The telemetry / recorder / display representation is **`float`**; scientific source
data for an analysis consumer is kept **`double`** and converted to local relative coordinates
before float rendering. The engine is **not** migrated to double during this milestone.

**Source facts.**
- `TelemetryChannel::values`, `DataRecorder` history (`DataRecorder.h:224-225`) and the plot buffers
  are `float`. CSV load/export uses `double` (`DataRecorder.cpp:337`, `DataExport.cpp:37,70`), so a
  `double` → recorder `float` conversion **loses precision** by design.
- For trajectory/orbit consumers, scientific truth (epoch, units, large coordinates) must live in
  `double` **outside** the float telemetry/display path. X01's `1e11` origin offset is subtracted in
  `double` **before** float display — it tests the *sample's* conversion, not a new native
  large-coordinate promise throughout Cosmic.

**Tolerances (proposed — measurement/spec-derived).**
- Default float comparison where no domain bound is supplied:
  `abs(error) ≤ 1e-6 + 1e-5·abs(expected)` — **not** applied to bit-exact storage assertions.
- Analytic references (RK4 `1e-4` relative, oscillator `2e-3`, energy `< 1.10·initial`) keep their
  existing bounds; independent `double` reference values are added where a consumer needs them.
- **[Kaden]** Confirm the milestone keeps float display/telemetry and **defers** the consumer's
  double-precision units/epoch/tolerance contract to when real to-9km data exists (per D-9km).
  Define units / time-origin / conversion tolerance is a *pre-integration* obligation for that
  consumer, not this milestone.

**Invalidated by / re-opened when:** a real to-9km schema arrives and defines its own precision/units
contract (D-9km revisit).

---

## 6. Trunk 2D-only enforcement (recommendation recorded here)

*Not a measurement contract — a policy recommendation for WO-03 to implement and WO-12 to document.
Covers KI-3, B03–B05.*

**Recommended policy.** "`main` is 2D-only" means:
1. `COSMIC_2D_ONLY` **defaults ON** on the supported trunk (reverses the current `CMakeLists.txt:67`
   default of OFF), and
2. the trunk **rejects `-DCOSMIC_2D_ONLY=OFF` at configure time** — a hard configure error, not a
   silent 3D build — so no default entry point, stale cache, or script override can ship 3D.
3. The **OFF path is retained only on `engine-3d`** (the frozen full-tree snapshot), never on the
   trunk.

Supporting requirements (WO-03): CI / scaffolds / editor packaging / release staging / `package.bat`
all explicitly select 2D mode; the CI cache key carries build **mode** + architecture + toolchain
(fixing KI-3's stale-cache hole). Fencing excludes the *designated 3D* subsystems only; **shared
Jolt/physics, shared math and shared cameras stay** (B04 carve-out; see
[`support-matrix.md`](support-matrix.md) retained-dependency note). No blanket "no 3D-looking symbol"
rule.

**[Kaden]** Ratify the *hard configure-time rejection* of OFF on the trunk (recommended) vs a softer
warning-only default. This is the one enforcement-strictness decision WO-03 needs signed off.

---

## Open contract questions for Kaden (collected)

1. **Serial close deadline** (§1): is **2 s** the accepted normal-close deadline given a live SPP
   `CreateFileA` stall, or may a stalled-open close return earlier and clean the late handle once,
   asynchronously?
2. **Recording max session** (§2): supported maximum session length / data volume, and over-limit
   behaviour = stop, rotate, or reject?
3. **CSV grammar** (§3): keep restricted-numeric (recommended) vs adopt general CSV import; and is
   signed-zero preservation required?
4. **Editor-reload contract** (§4): confirm clears selection/undo + stops Play + preserves serialized
   scene as the intended rule (no live-state preservation promise)?
5. **Numeric precision deferral** (§5): confirm float telemetry stays and the consumer double
   units/epoch/tolerance contract is deferred per D-9km?
6. **2D-only enforcement strictness** (§6): hard configure-time rejection of OFF on the trunk
   (recommended) vs warning-only default?
7. **Soak minimum**: confirm **2 h** is the accepted minimum soak (S01/S02), explicitly *not* an
   unlimited-recording guarantee.

Numbers in §1–§5 are provisional until WO-02 measures a baseline on a named reference machine.
