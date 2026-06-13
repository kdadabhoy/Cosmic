# Shear_Force_TelemApp

ESP32 / ESC telemetry app built on the Cosmic engine. Connects to an ESP32 over
a Bluetooth-SPP COM port, decodes raw ESC telemetry on the host, plots it live
with ImPlot, drives a moving square, and records to CSV/`.bin` for replay.

Built for **1 ESC** today; scales to **3** by raising `k_EscCount` (the wire
protocol already carries a per-ESC id).

## Files

| File | Purpose |
|------|---------|
| `src/EscTelemetry.h` | Wire protocol, host-side decode constants, raw→engineering conversion, frame parser. |
| `src/Shear_Force_TelemApp.{h,cpp}` | The plugin layer: serial UI + raw monitor, parsing, telemetry pipeline, square render, constants editor. |
| `firmware/esc_telemetry_esp32.ino` | Improved ESP32 sketch: framed, checksummed, sends **raw** values. |

## Wire protocol (ESP32 → PC)

One ASCII line per packet, `\n` terminated:

```
$<id>,<temp>,<vraw>,<iraw>,<craw>,<erpmraw>*<HH>\n
```

- `$` start, `*` end-of-payload, `<HH>` = XOR checksum (hex) of everything between them.
- All fields are **raw** KISS values (`vraw`/`iraw` = centi-units, `erpmraw` = eRPM/100).
- Heartbeat/status lines start with `#` and are logged but ignored by the parser.

ASCII framing is deliberate — human-readable in any serial monitor and free of
NUL bytes, so it survives the engine's string-based serial buffer.

## Host-side decode (live-editable in the **Decode Constants** panel)

```
Voltage_V = vraw * VoltageScale            (0.01)
Current_A = iraw * CurrentScale            (0.01)
eRPM      = erpmraw * ErpmScale            (100)
MotorRPM  = eRPM / PolePairs               (7)
WheelRPM  = MotorRPM / GearRatio / SlipFactor   (19, 0.933)
Speed_mph = WheelRPM * (pi * WheelDiameterIn) / 1056   (3.5 in)
Power_W   = Voltage_V * Current_A
```

Change a constant and it applies to subsequently received packets — no reflash.

## Workflow

1. **Build:** `build.bat` (Debug) — the engine must be built first; `SerialPort`
   is now exported from `Cosmic.dll`.
2. Pair the ESP32 in Windows Bluetooth settings → it appears as an outgoing COM port.
3. Run `CosmicApp.exe`, load this project, open **Serial Link**, pick the COM
   port + 115200 baud, **Connect**.
4. **Telemetry** window: ImPlot charts per channel + inspector. Click the square
   in the viewport (or the entity combo) to select an ESC.
5. **Project Inspector Top**: *Start Recording* → run → *Stop* → *Export CSV + bin*.
   Output lands in `logs/<session>/` (`scene.bin` + one `.csv` per ESC).
6. Replay: **Telemetry → Replay → Load** a `scene.bin`; use the transport bar to
   scrub/play. The square is driven from the recorded data.

> Capture runs continuously so live charts always scroll. *Start Recording*
> clears the buffer to begin a clean capture; *Export* writes the current buffer.

## Engine note

`Cosmic::SerialPort` was not exported across the DLL boundary. A one-line change
marks it `COSMIC_API` (`Cosmic/src/serial/SerialPort.h`) so projects can link it.
Rebuild the engine (`build_engine.bat`) once to pick that up.
