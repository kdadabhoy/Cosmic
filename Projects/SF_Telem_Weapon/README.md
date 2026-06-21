# SF_Telem_Weapon

Single-ESC **weapon-motor** telemetry app built on the Cosmic engine. One ESP32
reads **one weapon ESC**, streams raw KISS telemetry over a Bluetooth-SPP COM
port; the host decodes it, plots every channel live (and in replay), and records
the run to CSV/`.bin`.

It is the single-ESC sibling of `Shear_Force_TelemApp`. Same telemetry pipeline,
same record/replay/export — but with the dual Right/Left overlay and the
differential-drive robot visual removed (a weapon motor has no kinematics to
draw). The focus is the **plots, the data, and the export**.

If the telemetry wire dies, the app flags **NO SIGNAL** and keeps running.

## Files

| File | Purpose |
|------|---------|
| `src/WeaponTelemetry.h` | Wire protocol, decode constants, raw→engineering conversion, frame parser. |
| `src/SF_Telem_Weapon.{h,cpp}` | The plugin layer: serial UI + raw monitor, parsing, telemetry pipeline, per-channel charts. |
| `firmware/weapon_telemetry_esp32/` | ESP32 sketch: reads one ESC, framed/checksummed raw telemetry. |
| `firmware/weapon_telemetry_sim_test/` | No-ESC simulator: synthesizes a spin-up/hold/spin-down cycle for end-to-end testing. |

## Wire protocol (ESP32 → PC)

One ASCII line per packet, `\n` terminated:

```
$<temp>,<vraw>,<iraw>,<craw>,<erpmraw>*<HH>\n
```

- `$` start, `*` end-of-payload, `<HH>` = XOR checksum (hex) of everything
  between `$` and `*`.
- All fields are **raw** KISS values (`vraw`/`iraw` = centi-units, `erpmraw` = eRPM/100).
- `#`-prefixed lines (heartbeat `# weapon=ok bt=yes`) are logged but ignored.

There is **no side tag** — this is the single-ESC difference from the dual-drive
app.

## ESP32 wiring

| Motor  | TLM wire → | UART |
|--------|-----------|------|
| WEAPON | GPIO 18   | UART1 |

**Change the pin** by editing `WEAPON_TLM_PIN` at the top of
`firmware/weapon_telemetry_esp32/weapon_telemetry_esp32.ino`.

## Host-side decode (live-editable in **Decode Constants**)

```
Voltage_V    = vraw * VoltageScale          (0.01)
Current_A    = iraw * CurrentScale          (0.01)
eRPM         = erpmraw * ErpmScale          (100)
MotorRPM     = eRPM / PolePairs             (3 for a 6-pole motor; 7 for 14-pole)
WeaponRPM    = MotorRPM / GearRatio         (4.0 = 4:1 pulley; 1.0 = direct drive)
TipSpeed_mph = WeaponRPM * (pi * WeaponDiameterIn) / 1056   (7.874 in = 0.2 m)
Power_W      = Voltage_V * Current_A
```

Tracked channels: `Temp_C, Voltage_V, Current_A, Consumption_mAh, eRPM,
MotorRPM, WeaponRPM, TipSpeed_mph, Power_W`.

## Predicted spin-up model

`src/WeaponModel.h` ports the *Weapon Speed Analysis* spreadsheet — a
torque-balance, forward-Euler spin-up simulation. From the motor/battery, weapon
inertia, reduction, tip diameter and an aerodynamic-drag coefficient it predicts
the full-throttle spin-up curve and the steady state:

```
Kt              = 60 / (2*pi*Kv)
MotorNoLoadRPM  = BatteryVoltage * Kv
MotorStall      = (MaxCurrent - NoLoadCurrent) * Kt
WeaponNoLoadRPM = MotorNoLoadRPM / Reduction
WeaponStall     = MotorStall     * Reduction
TWSlope         = WeaponStall / WeaponNoLoadRPM

step:  motorT = WeaponStall - TWSlope*weaponRPM      (linear t-w line, full throttle)
       dragT  = DragCoeff * weaponRPM^2              (CFD quadratic fit)
       alpha  = (motorT - dragT) / Inertia
       omega += alpha*dt   ->   tipSpeed = omega * (dia/2)
```

Reduction (`GearRatio`) and tip diameter (`WeaponDiameterIn`) are shared with the
Decode Constants so geometry lives in one place. With the default inputs the
model reproduces the spreadsheet exactly: **244.7 mph max tip, 10 445 rpm,
2.56 s to 90%**. The predicted steady-state max is overlaid (green) on the live
TipSpeed / WeaponRPM charts so measured performance is compared against theory.

By default the prediction runs on the **live measured battery voltage** (so the
predicted ceiling sags with the real pack); untick *Use live battery voltage* in
the model panel to pin it to a fixed value instead. With no telemetry connected
it falls back to the manual voltage, so it still reproduces the spreadsheet.

## Windows

- **Serial Link** — COM port + baud, Connect/Disconnect, good/bad frame counts, raw monitor.
- **Weapon Dashboard** — health banner (LIVE / `NO SIGNAL`) + one live chart per channel, with the predicted max overlaid in green.
- **Weapon Model (Predicted)** — editable spin-up inputs, derived motor/weapon params, predicted max tip speed / RPM / time-to-90%, and the predicted spin-up curve.
- **Telemetry (drill-down)** — engine `TelemetryPanel`: replay loader + detailed charts + inspector.
- **Project Inspector Top** — replay transport, recording (Start/Stop/Export), decode constants.

## Workflow

1. **Build:** engine first (`build_engine.bat` — exports `SerialPort`), then
   `build.bat` for the project.
2. Flash `firmware/weapon_telemetry_esp32` (or `weapon_telemetry_sim_test` to
   test with no ESC), pair the ESP32 in Windows Bluetooth → outgoing COM port.
3. Run `CosmicApp.exe`, load this project, **Serial Link → Connect** (115200).
4. Watch the **Weapon Dashboard** charts.
5. **Start Recording → run → Stop → Export CSV + bin** → `logs/<session>/`
   (`scene.bin` + `ESC_Weapon.csv`).
6. Replay: **Telemetry → Replay → Load** the `scene.bin`; the transport bar
   scrubs the run and the charts replay.

> Capture is continuous so charts always scroll; *Start Recording* clears the
> buffer for a clean segment, *Export* writes it.

## Engine note

`Cosmic::SerialPort` is marked `COSMIC_API` (`Cosmic/src/serial/SerialPort.h`)
so the project DLL can link it. Rebuild the engine once to pick that up.
