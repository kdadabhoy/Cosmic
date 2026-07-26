# API Reference — Serial & Telemetry

> **STATUS: SKELETON** — to be filled by work order **D17** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/serial/SerialPort.h`, `serial/SerialLink.h`,
`serial/Framing.h`, `telemetry/TelemetryChannel.h`, `telemetry/DataRecorder.h`,
`telemetry/DataPlayer.h`, `telemetry/TelemetryPanel.h`, `telemetry/EntitySelection.h`,
`telemetry/EntityPicker.h`.

**Read first:** the guide chapter
[`../guide/serial-and-telemetry.md`](../guide/serial-and-telemetry.md) (D59) — it is written from
source and covers every header in this scope, including the v1 binary format read off the writer.
Root README §20 and §26 are now overviews that point at it. Systems explainer:
[serial-telemetry](../systems/serial-telemetry.md) (skeleton — D33). Usage exemplars:
`Projects/SF_Telem` (ASCII protocol, recording, replay, the panel), ViperSim's HIL backend (COBS
framing over `SerialLink`).

> **Docstring warning, narrowed by D59.** The header docstrings have been corrected — `DataRecorder.h`
> and `DataPlayer.h` both say v1 now. **One stale comment survives:** `DataRecorder.cpp:257` labels
> the write `v3 format with per-entity sample_count`, three lines above `const uint32_t version = 1u;`.
> Document what the code does.

> **D59 corrections to carry into the entries.** `SerialPort::Write` **is** implemented and exported
> (the old README §20 called it "planned"). `DataRecorder::Flush` and `DataPlayer::Load` do **not**
> resolve VFS paths — same trap as `SceneManager::Load` and `Shader::Create`. `--replay <file>` sets
> `COSMIC_REPLAY_FILE` and **nothing in the tree reads it**; the installer associates `.cham`, which
> `DataPlayer::Load` would reject anyway (it accepts a directory or `.bin` only).
> `EntityPicker.h`'s file-header overview says the hit test is an axis-aligned box while `Pick`'s own
> docstring and the code rotate the query point by `Rotation.z` — the docstring and the code are right.

## Coverage checklist *(starting point — headers are authoritative)*

- [ ] `SerialPort` — enumerate ports, open/close, **`BeginOpen` async connect (no-freeze Bluetooth)**, read/`Write`, settings (baud etc.), threading contract (which calls are safe off the main thread), error/disconnect behavior
- [ ] `SerialLink` — the shared connect-UI/policy component: state machine, auto-reconnect policy, panel draw call, how apps subscribe to received data
- [ ] `Framing.h` — COBS encode/decode + CRC (E5), frame size limits, usage with `SerialLink::Write`
- [ ] `TelemetryChannel` — channel registration, typed samples, columnar storage model, **unsubscribe handles**, `Mode` enum semantics
- [ ] `DataRecorder` — record start/stop, binary format (v1), **autosave failsafe**, file naming/location (`user://`)
- [ ] `DataPlayer` — load, seek/scrub, playback rate, driving live panels from replay
- [ ] `TelemetryPanel` — the drop-in plotting panel: what it renders, replay UI ownership, per-channel controls
- [ ] `EntitySelection` / `EntityPicker` — selection model shared between viewport picking and telemetry plots

## Sections to write

1. Data-flow Mermaid diagram: device → SerialPort/Link → decode → channels → (recorder file ⇄ player) → panel. <!-- TODO(D17) -->
2. Entries per checklist. <!-- TODO(D17) -->
3. Wire-protocol appendix: the ASCII side-tagged protocol pattern (SF_Telem) and COBS binary pattern (Viper HIL) as *examples of client protocols* — engine ships transport + framing only. <!-- TODO(D17) -->

---
*Changelog:*
