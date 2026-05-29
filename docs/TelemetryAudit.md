# Telemetry System Audit
**Date:** 2026-05-29  
**Scope:** All files under `Cosmic/src/telemetry/`, `templates/ExampleProject/src/AgentSystem.h`, `TemplateTelemetryLayer.h/.cpp`  
**Method:** Source cross-check against `README.md §26` (client guide) and `§38` (internals)

---

## Table of Contents

1. [Layer Time Scaling — Root Cause of the Bug](#1-layer-time-scaling--root-cause-of-the-bug)
2. [Binary Format — Timestamps Discarded on Export](#2-binary-format--timestamps-discarded-on-export)
3. [DataPlayer::Tick — Freeze at Position 0 with Negative dt](#3-dataplayertick--freeze-at-position-0-with-negative-dt)
4. [DataRecorder — Inconsistent Mutex Usage on m_NameToId](#4-datarecorder--inconsistent-mutex-usage-on-m_nametoid)
5. [v1 Format Loading is Broken (README vs Code Mismatch)](#5-v1-format-loading-is-broken-readme-vs-code-mismatch)
6. [Minor: Comment Numbering Gap in TelemetryPanel::OnImGuiRender](#6-minor-comment-numbering-gap-in-telemetrypanelonimguirender)
7. [Minor: Confusing Variable Name in Replay Position Sync](#7-minor-confusing-variable-name-in-replay-position-sync)
8. [README Documentation Gaps](#8-readme-documentation-gaps)
9. [What Is Correct and Working](#9-what-is-correct-and-working)
10. [Priority Fix List](#10-priority-fix-list)

---

## 1. Layer Time Scaling — Root Cause of the Bug

**Files:** `TemplateTelemetryLayer.cpp:121–128`, `TemplateTelemetryLayer.cpp:289–299`, `README.md §7`

### What the engine actually provides

From the frame loop documented in `README.md §3`:

```
Pass 1A — OnFixedUpdate(scaledFixedDelta)
Pass 1B — UpdateLayerTime(scaledDelta) → OnUpdate(scaledDelta)
```

Both `scaledDelta` and `scaledFixedDelta` are:

```
scaledDelta      = rawDelta      * globalTimeScale
scaledFixedDelta = (1/60)        * globalTimeScale
```

The engine passes the **globally-scaled delta** to `OnUpdate` and `OnFixedUpdate`. It does NOT automatically apply the layer's own per-layer time scale to these parameters — it only applies the layer scale when accumulating `GetLocalTime()` internally via `UpdateLayerTime`.

### What TemplateTelemetryLayer does (correctly)

```cpp
// TemplateTelemetryLayer.cpp:121
void TemplateTelemetryLayer::OnUpdate(float ts)
{
    const float localTs = ts * GetTimeScale();   // ← manually applies layer scale
    m_Camera.OnUpdate(localTs);
    m_Panel.OnUpdate(localTs);
    ...
}

// TemplateTelemetryLayer.cpp:289
void TemplateTelemetryLayer::OnFixedUpdate(float dt)
{
    const float localDt = dt * GetTimeScale();   // ← manually applies layer scale
    if (localDt <= 0.0f) return;
    m_Scene->OnFixedUpdate(localDt);
    m_Recorder.Tick(localDt);
}
```

This is the **correct pattern** for a layer that wants independent per-layer time control. `ts * GetTimeScale()` produces `rawDelta * globalTimeScale * layerTimeScale` — identical to what `GetLocalTime()` accumulates frame-by-frame.

### Why it "only works in the telem layer"

Other client layers that call `OnUpdate(float ts)` and use `ts` directly receive `rawDelta * globalTimeScale`. They never apply the layer's own scale multiplier. When the user calls `SetTimeScale(0.5f)` on their layer, `GetLocalTime()` slows down correctly, but `ts` and `dt` passed to their code are unchanged — giving the impression that per-layer time only works in the telem layer.

### The gap the README doesn't explain

Section 7 of the README describes `GetLocalTime()` as "accumulated scaled time in seconds" and `GetTimeScale()` as "this layer's own scale multiplier," but it never states that `ts` in `OnUpdate` is **not** layer-scaled. A reader naturally assumes both use the same scale. The section should explicitly say:

> `ts` in `OnUpdate(float ts)` is globally scaled only. To apply your layer's own time scale to the incoming delta, use `ts * GetTimeScale()`. Use `GetLocalTime()` for any accumulated time value (e.g. shader `u_Time`) — it is already double-scaled automatically.

### Immediate consequence for the recorder

When `globalTimeScale ≠ 1.0`, `m_Recorder.Tick(localDt)` advances `m_ElapsedTime` by `globalScale × layerScale / 60` per fixed tick instead of `1/60`. The timestamps embedded in each recorded sample therefore reflect **simulated time**, not wall-clock time. This is correct behaviour — and is directly connected to the binary format bug described next.

---

## 2. Binary Format — Timestamps Discarded on Export

**Files:** `DataRecorder.cpp:279–286`, `DataPlayer.cpp:84–87`, `DataPlayer.cpp:388–409`

### What the recorder stores vs. what is written

`DataRecorder::RecordImpl` stores a timestamp per frame in `EntityRecord::timestamps` (the actual `m_ElapsedTime` at record time). This correctly reflects the simulated clock, including any global/layer scaling.

`DataRecorder::Flush` writes only the **channel values** to `scene.bin`. The `timestamps` vector is never written to the binary:

```cpp
// DataRecorder.cpp:279 — row-major data write
for (uint32_t s = 0; s < snap.sampleCount; ++s)
{
    for (uint32_t ch = 0; ch < chCount; ++ch)
        rowBuf[ch] = snap.columns[ch][s];   // channel data only
    binFile.write(rowBuf.data(), chCount * sizeof(float));
    // timestamps[s] is never written
}
```

### What the player reconstructs

`DataPlayer::LoadBinaryFile` (v3 path, line 279) reconstructs timestamps as:

```cpp
entities[e].frames[s].timestamp = static_cast<float>(s) / sampleRate;
```

This assumes every frame was recorded exactly `1/sampleRate` seconds apart in simulation time. That is only true when `globalTimeScale == 1.0` throughout the entire recording session.

### The duration calculation is wrong when time scale ≠ 1

`DataPlayer::Load` computes the playable duration as:

```cpp
// DataPlayer.cpp:84
float dur = static_cast<float>(entity.frames.size() - 1) / entity.sampleRate;
```

**Example:** Recording is done with `globalTimeScale = 0.5`. The fixed update runs at 60 Hz (real time), but `Tick(localDt)` advances `m_ElapsedTime` by `0.5/60` per tick. After 2 real seconds: 120 frames recorded, `m_ElapsedTime = 1.0s`.

- **Actual simulated duration:** 1.0 s
- **DataPlayer computed duration:** `(120 - 1) / 60 ≈ 1.98 s`
- **Effect:** Replay plays back at ~2× the recorded simulation speed

The CSV export is not affected — it writes `timestamps[s]` as the time column and is correct.

### Fix

Either write the timestamps to the binary format, or write a `timePerSample` float per entity to the descriptor (actual average `m_ElapsedTime / sampleCount`) so the player can reconstruct correctly. The simplest safe fix is to write `m_ElapsedTime` to the file header as a `totalDuration` float that the player uses instead of the frame-count calculation:

```
[header] float total_duration  ← actual m_ElapsedTime (per-entity in v3 descriptor)
```

Then `DataPlayer::Load` uses `entity.totalDuration` instead of `frames / sampleRate`.

---

## 3. DataPlayer::Tick — Freeze at Position 0 with Negative dt

**Files:** `DataPlayer.cpp:335–352`

```cpp
void DataPlayer::Tick(float dt)
{
    if (!m_Playing || !m_Loaded) return;

    m_Position += dt * m_Speed;

    if (m_Position >= m_Duration)   { m_Position = m_Duration; m_Playing = false; }
    else if (m_Position <= 0.0f)
    {
        m_Position = 0.0f;
        if (m_Speed < 0.0f)         // ← only stops if speed is negative
            m_Playing = false;
    }
}
```

**Trigger:** Global time scale goes negative (`Application::SetTimeScale(-1.0f)`) while a replay is loaded and playing forward (`m_Speed = +1.0f`).

- `localTs = ts * GetTimeScale()` → `ts` is negative → `localTs < 0`
- `TelemetryPanel::OnUpdate(localTs)` calls `m_Player->Tick(localTs)`
- `m_Position += negative * 1.0f` → position decreases
- Position clamps to 0.0, but `m_Speed < 0.0f` is false → `m_Playing` stays `true`
- Every subsequent frame: clamps to 0 again, never stops

The player is in an infinite "stuck-at-zero-but-playing" state with no audio or visual indication.

**Fix:**

```cpp
else if (m_Position <= 0.0f)
{
    m_Position = 0.0f;
    if (m_Speed < 0.0f || dt < 0.0f)   // stop also if dt pulled us backward
        m_Playing = false;
}
```

Or more robustly, guard against negative `dt` at the top of `TelemetryPanel::OnUpdate`:

```cpp
void TelemetryPanel::OnUpdate(float dt)
{
    if (dt > 0.0f && m_Mode == Mode::Replay && m_Player && m_Player->IsLoaded())
        m_Player->Tick(dt);
    ...
}
```

This is the cleaner fix because it matches the mental model: replay only advances when simulated time moves forward.

---

## 4. DataRecorder — Inconsistent Mutex Usage on m_NameToId

**Files:** `DataRecorder.cpp:100–111`, `DataRecorder.cpp:121–128`, `DataRecorder.h:214`

`m_NameToId` is written under `m_RegistryMutex` in `Register()`. Three functions read it:

| Function | Locks m_RegistryMutex? |
|---|---|
| `GetEntityNames()` | ✅ Yes |
| `GetCurrentFrame()` | ❌ No |
| `GetInfo()` | ❌ No |

```cpp
// DataRecorder.cpp:100 — no lock
bool DataRecorder::GetCurrentFrame(const std::string& entityName, TelemetryFrame& out) const
{
    auto it = m_NameToId.find(entityName);   // data race if Register() runs concurrently
```

The documented contract (Register() on main thread only, before any Record() calls) means this is not a live data race in the template project. However:
- The inconsistency is a maintenance trap — a future developer may reasonably add a late `Register()` call
- `GetEntityNames()` taking the lock while the other two don't is misleading

**Fix:** Add `std::lock_guard<std::mutex> lock(m_RegistryMutex);` to both `GetCurrentFrame` and `GetInfo`, or add a comment on each explaining why the lock is intentionally omitted.

---

## 5. v1 Format Loading is Broken (README vs Code Mismatch)

**Files:** `DataPlayer.cpp:104–113`, `README.md §38`

The README's format compatibility table says:

> v1 — Single entity per `.bin` (no `"CSMC"` magic) — Derived from file size

But `DataPlayer::LoadBinaryFile` checks for the CSMC magic **unconditionally** as the very first operation:

```cpp
// DataPlayer.cpp:104
char magic[4] = {};
file.read(magic, 4);
if (magic[0] != 'C' || magic[1] != 'S' || magic[2] != 'M' || magic[3] != 'C')
{
    CS_CORE_ERROR("... Not a CSMC file.");
    return false;          // ← always fails for files without magic
}
```

If v1 files truly lack the CSMC magic prefix, they can never be loaded — the function returns false before reaching the `if (version == 1u)` branch. The v1 loading code is dead code.

**Likely explanation:** Either (a) the README description of v1 is wrong and all v1 files did have the CSMC magic, or (b) v1 files without magic exist on disk and are silently unloadable. Either way, one of the two must be corrected.

If v1 files have no magic, the loader needs a pre-check before the magic read (e.g., if file size matches single-entity layout, treat as v1). If v1 files do have the magic, update the README table.

---

## 6. Minor: Comment Numbering Gap in TelemetryPanel::OnImGuiRender

**File:** `TelemetryPanel.cpp:204–273`

```cpp
// 1. Replay loader
...
// 2. Entity selector combo
...
// 3. ImPlot charts       ← last numbered section
...
// 5. Inspector           ← jumps to 5, section 4 is missing
```

Section 4 was presumably removed or merged during a refactor (likely the transport controls, which were moved to `DrawTransportControls()`). The comment should either be renumbered or a `// 4. (transport — see DrawTransportControls)` placeholder added.

---

## 7. Minor: Confusing Variable Name in Replay Position Sync

**File:** `TemplateTelemetryLayer.cpp:143–154`

```cpp
for (auto rawE : view)
{
    const std::string& tag = view.get<Cosmic::TagComponent>(rawE).Tag;
    //                 ^^^  ← named "tag" but actually the entity name ("Agent_00")
    Cosmic::TelemetryFrame frame;
    if (m_Player.GetFrame(tag, frame) && ...)
```

`Cosmic::TagComponent::Tag` holds the entity's name (e.g., `"Agent_00"`), not its category tag (e.g., `"Agent"`). The variable name `tag` makes it look like a category filter. The code is functionally correct — `DataPlayer::GetFrame` looks up by entity name — but the name is misleading and will cause confusion during maintenance.

**Fix:** Rename to `entityName`:

```cpp
const std::string& entityName = view.get<Cosmic::TagComponent>(rawE).Tag;
if (m_Player.GetFrame(entityName, frame) && ...)
```

---

## 8. README Documentation Gaps

### §7 — Per-Layer Time Scale not explained for ts/dt

The README explains `GetLocalTime()` and `GetTimeScale()` but never states that `ts` in `OnUpdate(float ts)` is only globally-scaled. The critical pattern:

```cpp
// To apply both global AND layer time scale to delta:
const float localTs = ts * GetTimeScale();
```

...is only visible in the template layer source and is not documented anywhere in the client guide.

**Recommended addition to §7:**

> **Note:** `ts` in `OnUpdate(float ts)` and `dt` in `OnFixedUpdate(float dt)` are pre-scaled by the global `TimeScale` only — the layer's own scale multiplier is **not** automatically applied to these parameters. If your layer uses `SetTimeScale()` for independent time control, apply it manually:
> ```cpp
> void MyLayer::OnUpdate(float ts)
> {
>     const float localTs = ts * GetTimeScale();  // global + layer scale
>     // use localTs for movement, animation, player Tick, etc.
> }
> ```
> `GetLocalTime()` is always double-scaled automatically (no extra work needed for time values that accumulate).

### §38 — v1 format description contradicts the code

The format compatibility table says v1 has "no `CSMC` magic." The implementation rejects files without the magic. One of these must be corrected.

### §26 — Replay position sync loop is undocumented

The fact that `OnUpdate` does a full entity view iteration every frame to override `TransformComponent` from the player is a non-obvious pattern that's likely to be copied incorrectly. It deserves a note in the client guide.

---

## 9. What Is Correct and Working

The following subsystems were verified against the README and source and are correct:

| Subsystem | Status |
|---|---|
| **DataRecorder columnar storage** | ✅ Correct — zero alloc after `ReserveCapacity` |
| **DataRecorder thread-safety model** | ✅ Correct — per-entity mutex, atomic ElapsedTime, no global contention |
| **DataRecorder v3 binary write** | ✅ Correct — per-entity sample_count, descriptor + data layout matches spec |
| **DataPlayer v2/v3 loading** | ✅ Correct — reads descriptor and data in matching order |
| **DataPlayer interpolation** | ✅ Correct — `clamp(floor(t), 0, N-2)` prevents out-of-bounds on last frame |
| **DataPlayer::SampleAt** | ✅ Correct — does not modify m_Position |
| **EntitySelection subscription model** | ✅ Correct — snapshot-before-fire prevents re-entrant deadlock |
| **EntitySelection::Unsubscribe in destructor** | ✅ Correct — dangling callback impossible |
| **TelemetryPanel mode state machine** | ✅ Correct — Live/Replay exclusive, buffers cleared on mode switch |
| **TelemetryPanel ring buffer** | ✅ Correct — write index, eviction, Y-range from valid slots only |
| **EntityPicker screen-to-world math** | ✅ Correct — Y-flip, perspective divide, AABB hit test |
| **AgentSystem parallel/merge pattern** | ✅ Correct — ForEachAsync (no WaitIdle), merge writes TransformComponent on main thread |
| **Trail scrub detection** | ✅ Correct — `abs(actualStep) > abs(expectedStep)` clears stale trail |
| **Replay picking blocked** | ✅ Correct — event handler returns false during replay mode |
| **WaitForFlush on OnDetach** | ✅ Correct — blocks until background write completes before scene teardown |
| **DrawReplayLoader null guard** | ✅ Correct — `DrawReplayLoader()` only called when `m_Player != nullptr` |
| **Transport >| button wraparound** | ✅ Correct — Jump-to-end logic, reverse wraparound on rewind |

---

## 10. Priority Fix List

| Priority | Issue | File(s) | Impact |
|---|---|---|---|
| **P1** | Binary format loses actual timestamps; replay duration is wrong at any time scale ≠ 1.0 | `DataRecorder.cpp:279`, `DataPlayer.cpp:84` | Incorrect replay playback speed after scaled recording |
| **P1** | README §7 doesn't explain that `ts`/`dt` are not layer-scaled | `README.md §7` | Every new client layer that uses `SetTimeScale()` will not behave correctly |
| **P2** | `DataPlayer::Tick` freezes in playing state at position 0 when `dt < 0` | `DataPlayer.cpp:335` | Infinite no-op Tick calls when global time scale is negative during replay |
| **P2** | v1 format description in README contradicts the code | `DataPlayer.cpp:104`, `README.md §38` | Either legacy files are unloadable, or the docs are wrong |
| **P3** | `GetCurrentFrame` and `GetInfo` read `m_NameToId` without `m_RegistryMutex` | `DataRecorder.cpp:100,113` | Potential data race if contract is violated; inconsistent with `GetEntityNames` |
| **P4** | Variable `tag` in replay sync loop actually holds the entity name | `TemplateTelemetryLayer.cpp:143` | Misleading naming, maintenance hazard |
| **P4** | Comment section numbering skips 4 | `TelemetryPanel.cpp:265` | Cosmetic |

---

*Generated by code audit — all line numbers reference the `5/29/2026` revisions of the listed files.*
