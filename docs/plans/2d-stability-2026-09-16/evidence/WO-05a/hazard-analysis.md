# WO-05a — COM close / link-loss crash: source-level hazard analysis

- **Base:** `main` @ `490209e` (WO-03 landed; `origin/main` still `a246822`). Working tree clean apart
  from the untracked root plan file (left untouched, per rule 2).
- **Mode:** `COSMIC_2D_ONLY=ON` (SerialPort/SerialLink/SF_Telem are dimension-agnostic; the serial
  stack is identical on both engine configurations).
- **Goal (WO-05a):** *reproduce + diagnose* the reported COM close/link-loss crash through the real
  shared-ownership chain, and specify the injectable-transport seam WO-04 must build. **No fix ships
  here.**
- **Method:** read-only source inspection of the whole ownership chain (host loop → WorkspaceLayer →
  SF_Telem → SerialLink → SerialPort → OS). No hardware available (see `repro-sequence.md`).

Labels: **[CONFIRMED-BY-ANALYSIS]** = the control/data flow is proven from source and requires no
runtime to see; **[HYPOTHESIS]** = plausible from source but needs the blocked runtime to observe;
**[ENV-BLOCKED]** = cannot be exercised without a Bluetooth/virtual COM or the WO-04 seam.

---

## 0. TL;DR

Both reported symptoms — *(a) close the window while a COM port is opening/open*, and *(b) lose the
connection while a port is open, then close* — **converge on one mechanism**: losing the link starts a
background auto-reconnect that spawns a worker thread blocked inside `CreateFileA` on the now-unreachable
Bluetooth SPP port (the documented 10–20 s block), and **app teardown then joins that worker**, so the
process cannot exit until the blocking open returns. `m_Abandon` does **not** cancel an in-flight
`CreateFileA`, so the join is a multi-second **soft-hang on exit** (bounded, not an infinite deadlock).
To a user this is "the app froze / wouldn't close after I unplugged the ESP32," and a force-kill reads
as a crash.

This is **H1**, and it is **[CONFIRMED-BY-ANALYSIS]** end-to-end. The genuinely *concurrent* crash
candidates (a data race between the live read thread / connect worker and teardown when a port is truly
open and receiving) are **[ENV-BLOCKED]**: the connected state is unreachable headlessly today (KI-2),
so they must be qualified by the WO-05 matrix once the WO-04 transport seam exists. This spike hands
WO-04 the exact seam (`transport-seam-spec.md`) and hands WO-05 a test seat
(`tests/test_serial_shutdown_race.cpp`).

---

## 1. The real ownership chain (as wired, not as assumed)

```
Application::Run()                         Cosmic/src/core/Application.cpp:132
  loop: PollEvents(); RenderSingleFrame(); ProcessDeferredTransitions();   :138-156
    WindowCloseEvent → OnWindowClose → m_Running = false                    :637-641
    RenderSingleFrame() STILL runs this frame's PASS 1B OnUpdate            :146, :240-247
      for layer in m_LayerStack: layer->OnUpdate(dt)
        WorkspaceLayer::OnUpdate → m_ClientViewportLayer->OnUpdate(dt)      WorkspaceLayer.cpp:96-100
          SF_Telem::OnUpdate → m_Link.OnUpdate(ts)                          Projects/SF_Telem/src/SF_Telem.cpp:104
            SerialLink::OnUpdate → (auto-reconnect) m_Port.BeginOpen(...)   Cosmic/src/serial/SerialLink.cpp:56-68
              SerialPort::BeginOpen → std::thread worker → DoOpen→CreateFileA  SerialPort.cpp:59-83, :92-102
  loop exits (m_Running == false)
~Application → Shutdown()                   Application.cpp:109-112, :380
  UnloadProjectDLL()                                                        :388, :778
    m_WorkspaceLayer->ClearViewportLayer()                                  :783-786
      → m_ClientViewportLayer->OnDetach()   ← THIS is what calls OnDetach   WorkspaceLayer.cpp:58-67
        SF_Telem::OnDetach → m_Testing/TelemHub/Link.Shutdown()             SF_Telem.cpp:84-97
          SerialLink::Shutdown → m_Port.Close()                            SerialLink.cpp:99-105
            SerialPort::Close → m_ConnectThread.join()  ← BLOCKS here      SerialPort.cpp:294-301
    delete m_ActivePluginLayer  → ~SF_Telem (members; Close is idempotent)  Application.cpp:791
```

`m_Link` is a **by-value member of the SF_Telem root** (`SF_Telem.h:62`), shared with the screens; the
consumers hold it by **raw pointer** — `m_TelemHub.Init(&m_Link)` and `m_Testing.Init(&m_Link)`
(`SF_Telem.cpp:65-66`). Every consumer touches the link **only on the main thread** inside its
`OnUpdate` (`TelemHub::PumpSerial` calls `m_Link->ConsumeJustConnected()` / `m_Link->Poll()`,
`TelemHub.cpp:323-329`) — there is **no consumer-owned worker thread** that touches the link. The only
background threads in the whole chain are the two SerialPort owns: the **connect worker**
(`m_ConnectThread`) and the **read thread** (`m_ReadThread`).

### 1.1 Does the host actually call `OnDetach` on window close? — YES, but by a non-obvious path.

The WO asked this explicitly. The answer corrects the intuitive guess:

- The plugin layer is **not** in `m_LayerStack`; it is held separately in `m_ActivePluginLayer` and
  bound as the WorkspaceLayer's client (`Application.cpp:721, :744`). So the LayerStack's OnDetach
  machinery never touches it, and `LayerStack::ForceCleanForShutdown()` **deliberately skips OnDetach**
  (`LayerStack.cpp:111-117`).
- `OnDetach` is nonetheless invoked, because `Shutdown()`'s first act is `UnloadProjectDLL()`, which
  calls `m_WorkspaceLayer->ClearViewportLayer()` (`Application.cpp:783-786`), and that calls
  `m_ClientViewportLayer->OnDetach()` (`WorkspaceLayer.cpp:64`) **before** the `delete`.
- Same for return-to-launcher (`ProcessDeferredTransitions` → `UnloadProjectDLL`, `Application.cpp:284`).

**Consequence:** `m_Link.Shutdown()` *does* run on close, so the SerialPort threads *are* joined and
nothing leaks — but the guarantee is **fragile and load-bearing on a side effect**: it holds only while
`m_WorkspaceLayer` is still set at Shutdown and while `ClearViewportLayer()` precedes the `delete`. A
future refactor that deletes the plugin without first clearing the viewport layer would silently drop
`OnDetach` (and with it the recording flush in `TelemHub::Shutdown`, §4). Recorded as **H4**.

### 1.2 Can an OnUpdate / reconnect tick run concurrently with, or after, teardown?

- **The tick itself:** No. `OnUpdate` and `OnDetach` are both main-thread; the last `OnUpdate` is the
  final frame's PASS 1B, and `OnDetach` runs later from `~Application`. They are sequential.
- **The worker the tick spawned:** **YES.** The final `OnUpdate` can call `BeginOpen`, whose worker
  thread is still executing `CreateFileA` while teardown begins. Teardown's `Close()` is what joins it.
  This is the crux of H1.

---

## 2. H1 — Exit soft-hang: teardown joins a reconnect worker blocked in `CreateFileA`  **[CONFIRMED-BY-ANALYSIS]**

### 2.1 Preconditions (all reachable in normal SF_Telem use)
Auto-reconnect fires `BeginOpen` from `OnUpdate` only when (`SerialLink.cpp:56-68`):
`m_AutoReconnect` (default **true**, `SerialLink.h:85`) **&&** `m_WantConnection` (set by a prior
`Connect()`, `SerialLink.cpp:87`) **&&** `!IsReceiving()` **&&** `GetState() != Connecting` **&&**
`m_ReconnectClock >= 3.0 s` (`k_ReconnectInterval`, `SerialLink.h:95`) **&&** `!m_Selected.empty()`.

That is exactly the reported field scenario: the user connected to an ESP32 over a Bluetooth COM port,
data flowed, then the ESP32 was unplugged / powered off. The port is now "open — no data" or dropped,
and auto-reconnect retries every 3 s.

### 2.2 The block
Each retry's worker runs `DoOpen` → `CreateFileA("\\\\.\\COMn", …)` (`SerialPort.cpp:99-100`). On an
unreachable **Bluetooth SPP** port this call blocks ~10–20 s before failing — the exact behaviour the
async machinery was built to keep off the render thread (header note `SerialPort.h:84-88`, and the
BeginOpen doc-comment `SerialPort.cpp:51-58`). `DoOpen` does **not** consult `m_Abandon` while inside
`CreateFileA`.

### 2.3 The join that cannot be short-circuited
`SerialPort::Close()` (`SerialPort.cpp:294-301`):
```cpp
m_Abandon.store(true);                       // tells the worker to self-close AFTER DoOpen returns
if (m_ConnectThread.joinable())
    m_ConnectThread.join();                  // ← blocks until CreateFileA returns (~10–20 s)
CloseReadSession();
m_State.store(State::Idle);
```
`m_Abandon` is only ever read in the worker **after** `DoOpen` returns (`SerialPort.cpp:80-82`); it is
never checked inside the blocking `CreateFileA`. So `join()` — and therefore `OnDetach` →
`Application::Shutdown` → process exit — stalls for the full open timeout.

### 2.4 Timing window makes it likely, not rare
The retry cadence is 3 s and each open blocks 10–20 s, so a worker is mid-`CreateFileA` for the large
majority of wall-clock time after a drop. Closing the window in that state is the common case, not a
corner. The window is already visually gone (SwapBuffers stopped) while the process lingers → the
classic "app won't close / not responding" that gets force-killed.

### 2.5 Classification
A **bounded soft-hang / apparent freeze on exit**, not an infinite deadlock (the OS resolves
`CreateFileA` eventually) and not a memory-safety fault. It is the **single most likely source of both
reported symptoms**, because *losing the link* is precisely what starts the worker churn that then
blocks *the close*. Confirmed purely from control flow; the only thing the missing hardware changes is
the exact number of seconds.

---

## 3. H2 / H3 — connected-state teardown races (true crash candidates)  **[ENV-BLOCKED / HYPOTHESIS]**

These require a port that **actually opens** and a **live read thread**, i.e. the connected state that
KI-2 already flags as unreachable headlessly. They are enumerated so the WO-05 matrix (built on the
WO-04 seam) probes them deliberately rather than rediscovering them.

- **H2 — `Open()` vs. a finishing worker (TOCTOU).** The synchronous `SerialPort::Open()` guards only on
  `State == Connecting` (`SerialPort.cpp:29-33`) and then calls `CloseReadSession()` **without joining
  `m_ConnectThread`**. Between the worker storing `State::Open` (`:77`) and finishing its abandon-check
  tail (`:80-82`), `State != Connecting`, so a concurrent `Open()` would run `CloseReadSession()` while
  the worker may also touch `m_Handle` / `m_ReadThread`. **Not on the SF_Telem path** (SF_Telem only ever
  uses `BeginOpen`, via `Connect()`/reconnect — never `Open()`), so it is a *latent* hazard, not the
  reported bug. `BeginOpen` and `Close` both join the connect thread first (`:66`, `:297-298`), which is
  why the reachable paths are race-free. **[HYPOTHESIS]** — flag for WO-05; do not fix here.

- **H3 — read-thread self-termination on a mid-session drop, racing auto-reconnect churn.** When the
  device drops, `ReadLoop` sets `m_Connected=false` / `State::Failed` and breaks (`SerialPort.cpp:180-192`)
  but leaves `m_ReadThread` joinable and `m_Handle` valid until the next `CloseReadSession()`. The
  auto-reconnect then closes-and-reopens every 3 s (`BeginOpen` → `CloseReadSession` → new worker). By
  inspection this path is **serialized by joins** (`CloseReadSession` joins the read thread before
  `CloseHandle`, `:270-277`; `BeginOpen` joins the prior connect thread before touching handle state),
  so no data race is *provable from source*. But it has **never been executed** (no open port headlessly),
  so "no reproduced race" must not be read as "no race." **[ENV-BLOCKED]** — this is the core of reported
  symptom (b) and belongs to the WO-05 connected-state matrix.

Honesty note: this spike did **not** find a provable memory-safety crash in the *reachable* code. The
reachable SerialPort lifecycle (COM999 fast-fail, abandon race, destructor-joins-worker, no-stack) is
already covered green by `tests/test_serial_lifecycle.cpp` and holds up under inspection. The reported
"crash" is best explained by **H1's exit hang**; the true concurrent-crash surface is **gated behind the
missing seam** and is where WO-05 must look.

---

## 4. H4 — `OnDetach`-dependent recording flush  **[CONFIRMED-BY-ANALYSIS; hand to WO-06]**

`TelemHub::Shutdown()` performs the clean final flush of an in-progress recording
(`TelemHub.cpp:175-184`: flush if `m_RecordingDirty && frames>0 && !flushing`, then `DisableAutosave`,
`WaitForFlush`). It runs on close **only because** `OnDetach` runs (§1.1). Today that holds, so there is
no live data-loss bug — but it inherits the same fragility as H4/§1.1: if the `OnDetach` side effect ever
regresses, the final flush is skipped and only the rolling `_autosave/` snapshot (every few seconds,
`TelemHub.cpp:617-618`) survives. Not a WO-05a crash; recorded for WO-06's recording/replay/CSV matrix.

---

## 5. What WO-04 must build, and what WO-05 must then test

- **WO-04:** add the injectable transport seam specified in `transport-seam-spec.md` — inject only at the
  four OS boundary calls (`CreateFileA`/DCB, overlapped `ReadFile`+wait, `WriteFile`,
  `CancelIoEx`/`CloseHandle`); leave the entire state machine (threads, `m_Abandon`, stop-event,
  `State` transitions) real. Add the matching pass-through on `SerialLink` so its auto-reconnect can be
  driven without hardware.
- **WO-05 (connected-state matrix, using the seam + this repro):** at minimum —
  1. **H1 regression:** worker set to *block* in `Open` for N ms, then teardown; assert `Close()`
     returns within the abort budget once the fix (cancel/timeout the in-flight open, or detach the
     worker under abandon) lands. Failing-before / passing-after.
  2. **H3:** open → deliver bytes (reach `IsReceiving` / `ConsumeJustConnected` RX reset) → inject a
     mid-session drop → assert auto-reconnect churn is race-free and bounded, then teardown mid-retry.
  3. **H2:** `Open()` racing a finishing worker (guard/join correctness), if `Open()` stays in the API.
  4. Repeated connect/drop/close lifecycle soak for handle/thread leaks.

See `tests/test_serial_shutdown_race.cpp` for the reachable guard + the two skipped ENV-BLOCKED seats
these extend.
