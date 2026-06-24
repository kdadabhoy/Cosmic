# SF_Telem

Combined **drive + weapon** telemetry app built on the Cosmic engine. One ESP32
reads **three ESCs** (2 drive + 1 weapon), streams raw KISS telemetry over a
Bluetooth-SPP COM port; the host decodes everything, shows it on three screens,
records to CSV/`.bin`, and predicts weapon spin-up.

It combines the logic of the existing telemetry apps into one application with a
screen switcher, sharing a single serial connection and recorder across screens.

## Screens

Switch screens from the **Main / Drivetrain / Weapon** buttons in *Project
Inspector Top*. Each screen registers its panels into the engine's dock ports.

1. **Main** — a **Live Dashboard** fills the center: the weapon photo on top and
   the drivetrain photo below, each overlaid with white **readout boxes** that show
   the live value plus the running **avg + max** (in gray). Three compact **ESC
   readout panels** (Weapon / Left Drive / Right Drive) line the bottom, each
   auto-wrapping to 2 boxes per row. The right column holds **per-ESC plot tabs**
   (Right / Left / Weapon) and the telemetry drill-down. Records all three ESCs to CSV.
2. **Drivetrain** — the Shear Force drivetrain calculator (inputs, performance
   curves, results + CSV, what-if explorers, to-scale schematic). Mirrors
   `SF_DrivetrainCalcsApp`.
3. **Weapon** — the weapon data boxes + the predicted spin-up model + weapon
   plots. Drivetrain removed.

> The dashboard photos live in `assets/images/` (`SF_Weapon.PNG`,
> `SF_Drivetrain.PNG`) and load as textures. Box positions are **normalized** over
> each image (`k_WeaponReadouts` / `k_DriveReadouts` in `MainLayer.cpp`), so moving
> a box is a one-line edit. The dashboard owns the center via the engine's
> `DockPort::Center` (the empty Viewport tab is hidden on this screen).

> **Live stats:** every box's avg/max reset together from *Project Inspector Top →
> Live Stats* — set an **Avg window (s)** to auto-reset on an interval (0 = keep
> accumulating until you press **Reset All Stats**); each panel also has its own reset.

## Robustness (1, 2 or 3 ESCs)

Each ESC is fully independent. The wire format is `$<S>,...*HH` with a side tag
`S ∈ {R,L,W}`; the host routes each frame and tracks presence per ESC. If a
telemetry wire is unplugged, that ESC simply shows **no signal** while the others
keep streaming, recording, and plotting — nothing fails. The *Serial Link* panel
shows each ESC's live/stale/absent state.

## Files

| File | Purpose |
|------|---------|
| `src/Telemetry.h` | Wire protocol + drive/weapon decode + configs for R/L/W. |
| `src/TelemHub.{h,cpp}` | Shared backbone: serial, parse/route, samples, max-stats, rings, recorder, weapon model, shared Serial + Recording UI, per-ESC plots. |
| `src/SF_Telem.{h,cpp}` | Root manager: owns the hub + 3 screens, screen switching, per-screen dock layout, decode constants. |
| `src/MainLayer.{h,cpp}` | Screen 1: Live Dashboard (image overlays) + 3 ESC readout panels + plot tabs. |
| `src/DrivetrainLayer.{h,cpp}` | Screen 2 (drivetrain calculator). |
| `src/WeaponLayer.{h,cpp}` | Screen 3 (weapon + predicted model). |
| `src/StatBox.h` | Framed indication-box widgets (live value + avg + max) and the responsive grid. |
| `src/FirmwareTemplates.h` | Embedded `.ino` sources (main + simulator) for the in-app "Copy firmware" buttons. |
| `assets/images/` | Weapon + drivetrain photos (Live Dashboard) + `ESP32_Dev_Pin_Layout.png` (pinout pop-out). |
| `src/WeaponModel.h` | Predicted weapon spin-up (ported spreadsheet). |
| `src/DrivetrainModel.h` | Drivetrain spin-up physics. |
| `firmware/sf_telem_esp32/` | ESP32 sketch: reads all 3 ESCs, R/L/W tags, alive heartbeat. |
| `firmware/sf_telem_sim_test/` | No-ESC simulator (drive weave + weapon spin cycle). |

## Firmware / ESP32 wiring

Each AM32/KISS ESC has a **telemetry wire** that is a one-way **3.3 V UART @
115200 8N1** emitting a 10-byte binary frame (temp, voltage, current, mAh,
eRPM, CRC8). It is RX-only into the ESP32; the PC does all the unit conversion.
**Full protocol + pin rationale: [`docs/WIRING.md`](docs/WIRING.md).**

The classic ESP32 has three hardware UARTs and UART0 is the USB port, so the
weapon shares UART0 (RX remapped, USB TX kept). On a 30-pin ESP32 Dev Module:

| ESC telemetry | ESP32 GPIO | Board silk | UART |
|-----|-----------|-----------|------|
| RIGHT drive | GPIO 16 | `RX2` | UART1 |
| LEFT drive  | GPIO 17 | `TX2` | UART2 |
| WEAPON      | GPIO 13 | `D13` | UART0 (RX remapped; USB TX kept) |
| Ground      | GND     | `GND` | → **common star ground (required)** |
| 5 V in      | VIN     | `VIN` | from the UBEC 5 V rail |

All three GPIOs are safe (not flash 6–11, not strapping 0/2/5/12/15, not
input-only 34–39). The `RX2`/`TX2` silk labels are just chip defaults — they
work as inputs via the GPIO matrix. Each frame is tagged R/L/W so drive routes to
drive and weapon to weapon. To test the missing-ESC behaviour, flash
`sf_telem_sim_test` and set any of `SEND_RIGHT/LEFT/WEAPON` to 0.

### Copy a ready-to-flash sketch from the app

You don't have to hand-edit pins. The **Serial Link → Arduino Firmware** section
(open by default) lets you set the **Right / Left / Weapon** **GPIO numbers** —
these are GPIO numbers, *not* the 1–30 board positions (e.g. `16` = pad `RX2`,
`17` = pad `TX2`, `13` = pad `D13`); each field shows its pad name live. The `(?)`
hint and the **Pinout** pop-out show the board diagram
(`assets/images/ESP32_Dev_Pin_Layout.png`) with a naming-scheme legend underneath,
then
**Copy firmware (.ino)** — the generated sketch has your pins baked into the
`#define`s. Paste into the Arduino IDE and upload. **Copy simulator** grabs the
no-ESC simulator. The embedded templates live in `src/FirmwareTemplates.h` and
mirror `firmware/*.ino`.

## Decode (live-editable in *Project Inspector Top → Decode Constants*)

```
Drive:  MotorRPM = eRPM/(Poles/2) ; Speed_mph = (MotorRPM/Gear/Slip)*pi*WheelDia/1056
        (14-pole drive motor)  Predicted RPM = MotorKv * Voltage  (no-load)
Weapon: WeaponRPM = (eRPM/(Poles/2))/Gear ; TipSpeed = WeaponRPM*pi*WeaponDia/1056
        (6-pole weapon motor)  Predicted RPM = weapon spin-up model steady-state (vs aero drag)
```

## Workflow

1. **Build:** `build_all.bat` at the repo root (engine + all projects), or
   `build_engine.bat` then `build.bat` here.
2. Flash `firmware/sf_telem_esp32` (or `sf_telem_sim_test`), pair the ESP32 in
   Windows Bluetooth → outgoing COM port.
3. Run `CosmicApp.exe`, load **SF_Telem**, **Serial Link → Connect** (115200).
4. Watch the data boxes + plot tabs. **Start Recording → run → Stop → Export
   CSV + bin** → `logs/<session>/` (one CSV per ESC + `scene.bin`).
5. Replay via **Telemetry (drill-down) → Replay → Load** the `scene.bin`.
6. Switch to **Drivetrain** to size wheels/pulleys, or **Weapon** for the
   predicted spin-up vs your measured RPM.

## Engine note

Uses the engine dock-port API (`DockWindow(DockPort::…)`, incl. `DockPort::Center`
+ `WorkspaceLayer::SetViewportVisible`), the `Cosmic::UI` overlay/font helpers and
`Cosmic::Texture2D` for the dashboard, and the shared `Cosmic::SerialPort` /
`DataRecorder` / `TelemetryPanel`. Multithreading comes from those engine
subsystems (threaded serial reader + async recorder flush); the screen-manager
pattern follows the Template Project.

See the engine README's [Fonts and Text Rendering](../../README.md#27-fonts-and-text-rendering)
and [ImGui Overlay & Image Helpers](../../README.md#28-imgui-overlay--image-helpers)
for the dashboard's text/overlay APIs.
