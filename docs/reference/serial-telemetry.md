# API Reference — Serial & Telemetry

> **STATUS: SKELETON** — to be filled by work order **D17** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/serial/SerialPort.h`, `serial/SerialLink.h`,
`serial/Framing.h`, `telemetry/TelemetryChannel.h`, `telemetry/DataRecorder.h`,
`telemetry/DataPlayer.h`, `telemetry/TelemetryPanel.h`, `telemetry/EntitySelection.h`,
`telemetry/EntityPicker.h`.

**Read first:** root README §20 (serial), §26 (telemetry); systems explainer
[serial-telemetry](../systems/serial-telemetry.md). Usage exemplars: `Projects/SF_Telem`,
ViperSim HIL backend. **Docstring warning:** some telemetry docstrings reference a "v3"
binary format — the code writes **v1**; document what the code does, not stale comments.

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
