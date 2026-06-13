# Shear_Force_TelemApp

Dual-ESC drive telemetry app built on the Cosmic engine. One ESP32 reads **two
drive ESCs (Right + Left)**, streams raw KISS telemetry over a Bluetooth-SPP COM
port; the host decodes it, overlays Right vs Left in live/replay charts, records
both sides to CSV/`.bin`, and drives a **differential-drive robot visual** so you
can watch the robot translate and turn.

If one ESC's telemetry wire dies, that side is flagged (error note + red wheel)
and the app keeps running on the surviving side.

## Files

| File | Purpose |
|------|---------|
| `src/EscTelemetry.h` | Wire protocol, Right/Left side tags, decode constants, raw→engineering conversion, frame parser. |
| `src/Shear_Force_TelemApp.{h,cpp}` | The plugin layer: serial UI + raw monitor, parsing, telemetry pipeline, dual-overlay dashboard, robot kinematics. |
| `firmware/esc_telemetry_esp32.ino` | ESP32 sketch: reads two ESCs, framed/checksummed raw telemetry. |

## Wire protocol (ESP32 → PC)

One ASCII line per packet, `\n` terminated:

```
$<S>,<temp>,<vraw>,<iraw>,<craw>,<erpmraw>*<HH>\n
```

- `$` start, `<S>` = **`R` or `L`** (side tag — routes the packet to the right
  motor; a corrupt/unknown side fails the parse), `*` end-of-payload,
  `<HH>` = XOR checksum (hex) of everything between `$` and `*`.
- All fields are **raw** KISS values (`vraw`/`iraw` = centi-units, `erpmraw` = eRPM/100).
- `#`-prefixed lines (heartbeat `# R=ok L=ERR bt=yes`) are logged but ignored.

## ESP32 wiring

| Motor | TLM wire → | UART |
|-------|-----------|------|
| RIGHT | GPIO 18 | UART1 |
| LEFT  | GPIO 16 | UART2 |

Change `RIGHT_TLM_PIN` / `LEFT_TLM_PIN` at the top of the sketch.

## Host-side decode (live-editable in **Decode Constants**)

Both motors share the drivetrain, so one config covers both:

```
Voltage_V = vraw * VoltageScale            (0.01)
Current_A = iraw * CurrentScale            (0.01)
eRPM      = erpmraw * ErpmScale            (100)
MotorRPM  = eRPM / PolePairs               (7)
WheelRPM  = MotorRPM / GearRatio / SlipFactor   (19, 0.933)
Speed_mph = WheelRPM * (pi * WheelDiameterIn) / 1056   (3.5 in)
Power_W   = Voltage_V * Current_A
```

## Robot visual (differential drive)

Pose is integrated from the two wheel speeds:

```
v     = (vR + vL) / 2          forward speed
omega = (vR - vL) / TrackWidth yaw rate   (right faster => turns left)
```

`TrackWidth`, `SpeedScale`, and an `Invert turn direction` toggle live in **Robot
Kinematics**, with a **Reset to Origin** button.

The viewport is a top-down **Cartesian map**: an origin marker at (0,0) and a grid
whose spacing snaps to nice 1/2/5 numbers with projected numeric axis labels. The
robot roams freely (no bounding box). **Map View** controls:
- **Auto-scale view** (default): the grid zooms to keep the robot + trail framed,
  so it never drives off-screen.
- Manual: untick it and set **View size (± units)** to a fixed half-extent.

> **Direction caveat:** KISS telemetry is *unsigned* — it reports wheel-speed
> magnitude only, with no forward/reverse or true rotation sense. The visual
> therefore assumes forward drive and infers turning from the left/right speed
> difference. If you later wire in throttle/direction, the sign can be applied.

## Windows

- **Serial Link** — COM port + baud, Connect/Disconnect, good/bad frame counts, raw monitor.
- **Drive Dashboard** — per-side health banner (LIVE / `NO SIGNAL`), and one chart
  per channel overlaying **Right (red)** vs **Left (blue)**. Both sides update
  simultaneously in live and replay.
- **Telemetry (drill-down)** — engine `TelemetryPanel`: replay loader + single-side
  detailed charts + inspector. Pick `ESC_Right` / `ESC_Left` in the combo.
- **Project Inspector Top** — replay transport, recording (Start/Stop/Export),
  decode + kinematics constants.

## Workflow

1. **Build:** engine first (`build_engine.bat` — exports `SerialPort`), then
   `build.bat` for the project.
2. Pair the ESP32 in Windows Bluetooth → outgoing COM port.
3. Run `CosmicApp.exe`, load this project, **Serial Link → Connect** (115200).
4. Watch **Drive Dashboard** + the robot. Selecting a side in **Telemetry** drills
   into it.
5. **Start Recording → run → Stop → Export CSV + bin** → `logs/<session>/`
   (`scene.bin` + `ESC_Right.csv` + `ESC_Left.csv`).
6. Replay: **Telemetry → Replay → Load** the `scene.bin`; the transport bar
   scrubs both sides and the robot replays its path.

> Capture is continuous so charts always scroll; *Start Recording* clears the
> buffer for a clean segment, *Export* writes it.

## Engine note

`Cosmic::SerialPort` is now marked `COSMIC_API` (`Cosmic/src/serial/SerialPort.h`)
so the project DLL can link it. Rebuild the engine once to pick that up.
