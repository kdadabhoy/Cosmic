# Serial & Telemetry — How It Works

> **STATUS: SKELETON** — to be filled by work order **D33** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** bytes from a COM port (or a simulation) flow through optional COBS framing
into named, typed telemetry channels stored in columns — from there they plot live, record
to compact binary files, and replay through the exact same UI as live data.
**Source:** `Cosmic/src/serial/*`, `Cosmic/src/telemetry/*`
**API Reference:** [../reference/serial-telemetry.md](../reference/serial-telemetry.md) · **Guide:** root README §20, §26, §42

## Section plan

1. **Overview** — the test-rig story: hardware or sim on one side, plots/recordings on the other, same pipe. <!-- TODO(D33) -->
2. **Mental model** — diagram **DG-13** (device → SerialPort/SerialLink → decode → channels → recorder file ⇄ player → panel). Columnar storage explained as "one array per signal, not one struct per moment" and why that plots fast. <!-- TODO(D33) -->
3. **Step-by-step** — a live session (async `BeginOpen` for Bluetooth, connect policy, data → panel) and a replay session (player drives the same channels; panel owns replay UI). <!-- TODO(D33) -->
4. **Technical implementation** — `SerialPort` threading + failure/disconnect model, `SerialLink` shared connect UI/policy (why it moved engine-side), COBS+CRC framing (E5) with a worked byte example, channel registration + unsubscribe handles + `Mode` enum, binary format **v1** (docstrings claiming v3 are stale — the code is truth), `DataRecorder` autosave failsafe, `EntitySelection`/`EntityPicker` bridge to the viewport. <!-- TODO(D33) -->
5. **Design decisions** — columnar rewrite rationale (2026-05 rewrite), ASCII protocols (SF_Telem) vs binary COBS (Viper HIL) as app choices atop engine transport. <!-- TODO(D33) -->
6. **Limits & future work.** <!-- TODO(D33) -->

**Truth sources:** README §26/§42 (migrating here), `TelemetryChannel.h`, `DataRecorder.cpp`
(format constants), SF_Telem + ViperSim as living exemplars.
