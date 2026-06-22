# SF_Telem — Design & Intent

Companion to `README.md` (which covers *what it is* and *how to run it*). This
file captures the **goal** and the **why** behind the implementation, for both
humans and AI agents picking up the project.

## Goal

One desktop app for a combat robot's electronics bench: read telemetry from the
robot's **three ESCs** (2 drive + 1 weapon) off a single ESP32, and present it
three ways without juggling separate apps:

- a **Main** dashboard for everything at a glance (boxes + plots + recording),
- a **Drivetrain** calculator for sizing wheels/pulleys/gearing,
- a **Weapon** view for spin-up performance vs prediction.

It had to stay useful when the robot is only **partially wired** — 1, 2, or 3
telemetry wires connected — because at the bench you rarely have everything
plugged in at once.

## Mental model

```
SF_Telem (root manager, the one plugin layer)
 ├── TelemHub          <- SHARED: serial + decode + samples + recorder + model
 ├── MainLayer         \
 ├── DrivetrainLayer    >  three "screens"; only the active one updates/renders
 └── WeaponLayer       /
```

The root owns one `TelemHub` and passes a pointer to the screens. The screens
are **views onto shared state**, not independent apps.

## Why it's built this way

- **One shared `TelemHub`, not per-screen state.** All three screens read the
  same physical ESP32 over the same COM port. Sharing the serial link, decode,
  and recorder means switching screens never drops the connection or interrupts
  a recording. (Pattern: root-owns-children, like the engine's Template Project.)
- **Decode on the host, not the ESP32.** The firmware ships *raw* KISS values;
  the PC converts to volts/amps/RPM/speed. So motor poles, gear ratio, wheel/
  weapon diameter, etc. are tunable live in the UI with no reflashing.
- **Per-ESC independence = robustness.** Every frame carries a side tag
  `R`/`L`/`W`. The host tracks presence per ESC and treats a missing wire as
  "no signal" for that ESC only; the rest keep streaming/recording. This is the
  core requirement, not an afterthought — there is no "all ESCs required" path.
- **Two predictions, defined deliberately:**
  - *Weapon* predicted RPM = steady-state of the spin-up model (motor torque vs
    aerodynamic drag; ports the weapon-analysis spreadsheet, live-voltage aware).
  - *Drive* predicted RPM = motor no-load `Kv × Voltage`.
  These are what the "RPM vs Predicted" boxes and the green plot ceilings show.
- **Data boxes are static on purpose.** The spec asked for a clean numeric
  readout (live value + running max + reset), *not* animation. The weapon /
  drivetrain images are intentional **placeholders** — a texture can be dropped
  in later without touching the box logic.
- **Dock layout is per-screen and code-driven.** Each screen registers its
  panels into the engine's fixed dock ports (`DockWindow(DockPort::…)`) when it
  becomes active, so every screen has a sensible default layout the user can
  still rearrange.
- **Firmware uses all three ESP32 UARTs.** Drives on UART1/UART2; the weapon on
  UART0 (`Serial`) with its RX remapped so USB debug + flashing still work. This
  is the constraint that makes 3 ESCs possible on a classic ESP32.

## Where things live

| Concern | File |
|---|---|
| Wire protocol + decode (R/L/W) | `src/Telemetry.h` |
| Shared state + serial + recorder + plots | `src/TelemHub.{h,cpp}` |
| Screen switching + dock layout + decode constants | `src/SF_Telem.{h,cpp}` |
| Weapon prediction physics | `src/WeaponModel.h` |
| Drivetrain physics | `src/DrivetrainModel.h` |
| ESP32 reader / simulator | `firmware/` |

## How to extend (intent for future edits)

- **Add a screen:** create a `Cosmic::Layer` subclass taking `TelemHub*`, push it
  into `m_Modes`, add a `MODE_*`, and a `case` in `ApplyDockLayout`.
- **Add/rename an ESC:** extend `EscId` + the tag mapping in `Telemetry.h`; the
  hub's arrays are sized by `ESC_COUNT`.
- **Real weapon texture:** replace `ImagePlaceholder(...)` in the weapon panels
  with an `ImGui::Image` of a loaded `Cosmic::Texture2D`; the boxes are unchanged.
- **Keep robustness intact:** never assume an ESC is present — gate on
  `TelemHub::Present(id)` / `HasData(id)`.
