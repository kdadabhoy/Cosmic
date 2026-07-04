# Phase 21 Plan — Scripting & Connectivity Extensions

> **Created 2026-07-04.** The remaining carried-forward engine capabilities that fit neither
> rendering (doc 18) nor assets (doc 19): the **Lua tier** (doc 11 §4, mapped there in full —
> parked by user decision 2026-07-04 in favor of the C++ SystemScript tier, doc 13 H9, but per
> the "every feature has a phase home" rule it lives here with its unlock), **UDP sockets**
> (doc 03 E4, the last unshipped E-item), **positional audio** (doc 08 A3), and the
> **sequencer/cinematics** tool (doc 11 §9 P4). Items are independent.

---

## 0. Execution notes

Roadmap build recipe; doc 13 §0 engine rules; compat gate. The Lua items must re-read doc 11
§4 (archived: `docs/plans/archive/11-phase13-starforge-plan.md`) — the trade-off preamble and
the L1–L3 map written there remain the spec of record; this doc only carries the work-order
shells and unlocks.

## 1. Work orders

### C1 — UDP sockets *(origin: doc 03 E4, unstarted)*
**Files:** NEW `Cosmic/src/serial/UdpSocket.h/.cpp` (or `net/` — match the SerialPort file
pattern, pimpl'd winsock, COSMIC_API); tests with a loopback pair.
**Spec:** non-blocking send/recv datagrams, bind/connect helpers, main-thread poll model
matching `SerialPort`'s (no callback threads in v1); byte-span API mirroring
`SerialPort::Write`. The original E4 text in the archived doc 03 is the spec of record.
**Unlock:** an app needs UDP telemetry/sim-link (the original motivation was
MAVLink/QGroundControl-class links — app-side protocol, engine-side socket).
**Acceptance:** loopback round-trip test; 1k packets/s sustained without drops in the test
harness; no winsock in public headers. **Status:** ☐

### C2 — Positional audio *(origin: doc 08 A3; S14 annotation: app-side DistanceLoop covers ambience today)*
**Files:** MODIFY `Cosmic/src/audio/` (miniaudio already vendored — enable its spatializer),
`scene/Components.h` `AudioSourceComponent{ SoundPath, Loop, Volume, MinDist, MaxDist,
Play-on-start }` + listener = primary camera (or an `AudioListenerComponent` override).
**Spec:** 3D panning + distance attenuation + optional doppler via miniaudio's engine node
graph; streaming for long files (miniaudio decoder streams — verify the current `Sound` wrap);
scripts get `Audio().PlayAt(path, pos)`. Frontier's app-side `DistanceLoop` stays untouched
(compat).
**Unlock:** a scene needs true 3D audio (moving emitters, stereo image) beyond distance-gain.
**Acceptance:** a circling emitter pans correctly; doppler audible on a fast flyby (toggle);
headless-safe (no device = silent success, the A1 pattern). **Status:** ☐

### C3 — Lua L1: embed + core bindings *(origin: doc 11 §4)*
Vendor **sol2 + Lua 5.4**; `LuaScriptComponent{ ScriptPath }` with the ScriptableEntity
lifecycle; bindings **generated from the E1 reflection registry** (every registered
component/field readable/writable: `entity.Transform.Position.y = 3`); hand-bind the ~10
service entry points (Input, SceneManager::Request, spawn/destroy, log, Signals — the U2 bus).
**Unlock (per user decision 2026-07-04):** C++ reload latency measurably hurts a real
project's tuning loop, OR a non-programmer/modding need appears. Do not build speculatively.
**Acceptance:** the doc 11 §4 L1 bar — a Lua script moves an entity, reads input, and its
error lands in the Console as file:line without crashing the editor. **Status:** ☐

### C4 — Lua L2: editor integration *(origin: doc 11 §4; requires C3)*
Script asset type in the content browser, FileWatcher live reload **including during Play**,
Inspector fields via the script's `Fields` table mirrored into reflection, Console errors
click-to-open. **Acceptance:** edit-save-see loop <200 ms during Play; a field tuned in the
Inspector persists to the scene. **Status:** ☐

### C5 — Lua L3: interop + budget *(origin: doc 11 §4; requires C3/C4)*
Message/signal interop with C++ scripts (via the U2 EventBus, not direct vtables); per-frame
Lua time in the Statistics window; the "which tier does my logic belong in" doc page (docs
plan hook). **Acceptance:** a Lua script and a C++ system cooperate through signals in a
sample; Lua budget visible. **Status:** ☐

### C6 — Sequencer / cinematics *(origin: doc 11 §9 P4)*
Keyframe tracks on **reflected fields** (the E1 dividend: any float/vec field is animatable),
camera cut track, `.cseq` JSON asset, engine `SequencePlayer` (headless-sampled), Starforge
timeline panel (dopesheet-lite: tracks, keys, scrub, snap), script/flow trigger
(`Sequences().Play("intro")`, flow action). Curves ride `LookupTable` (doc 03) interpolation.
**Unlock:** trailer/demo-recording need, or a cutscene in a real project.
**Acceptance:** author a 10 s camera + light + transform sequence, scrub it, trigger from a
flow state, export via the demo-recording path; headless sampling determinism test.
**Status:** ☐

## Kickoff prompt

> You are implementing ONE work order from
> `docs/plans/20-phase21-scripting-connectivity-plan.md` in `C:\dev\Cosmic`. Read §0; for Lua
> items, doc 11 §4 in `docs/plans/archive/` is the spec of record. Engine gains only generic
> modules; headless-test what can be; compat gate; roadmap cmake recipe; no git writes.
> Finish with Acceptance + status banner.
