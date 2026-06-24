# SF_DrivetrainCalcsApp

Interactive **Shear Force drivetrain calculator** built on the Cosmic engine. It
turns the old command-line tool (`ShearForce_DrivetrainCalcs`, an input-file +
console program) into a live prototyping bench: type wheel sizes / pulley counts
/ battery + motor specs into ImGui panels and the spin-up simulation re-runs
instantly, drawing Speed / Accel / Force / Torque / Distance curves in ImPlot and
exporting the full time series to CSV.

The physics is a **faithful, number-for-number port** of the original `main.cpp`
(same equations, same constants) — only the front-end is new.

## Files

| File | Purpose |
|------|---------|
| `src/DrivetrainModel.h` | Pure, UI-free physics core. `DrivetrainConfig` inputs + `Simulate()` producing per-axle time series and headline metrics, with input validation and the front/rear velocity-sync check. |
| `src/SF_DrivetrainCalcsApp.{h,cpp}` | The plugin layer: ImGui input panels, auto-recompute, ImPlot curves, results table, CSV export, and a to-scale wheel/pulley schematic in the viewport. |

## The model (per axle)

```
mass_side   = (TotalWeight_lb / 2) * 0.45359237        kg
weight_side = mass_side * 9.80665                       N
r           = (WheelDia_in / 2) * 0.0254                m
R           = WheelPulleyTeeth / MotorPulleyTeeth
K_sys       = r / (Gearbox * R)                         m per motor-rad
kt          = 60 / (2*pi*Kv)
omega_noload = Kv * Vbatt * SpeedFactor * (2*pi/60)     rad/s
torque_stall = CurrentLimit * kt * DrivetrainEff        Nm
Traction    = mu * weight_side * K_sys                  Nm cap
Inertia_eq  = mass_side * K_sys^2
```

Each step: `torque = clamp(torque_stall*(1 - omega/omega_noload), 0, Traction)`,
integrate `omega`, derive speed (`omega*K_sys`), accel, contact force
(`torque/K_sys`), and distance. Status is `LIMIT` (traction-capped, i.e.
wheelspin) or `MOTOR` (motor-curve-limited).

A robot has a **front** and a **rear** axle. The front axle is the primary
simulation (matching the original); the rear is simulated too so it can be
overlaid for tuning.

## Feasibility — wheel tangential velocity

A rigid chassis forces both driven wheels to roll at the same ground speed. Each
wheel's tangential (surface) velocity is `motor_speed * K_sys`, so if the front
and rear `K_sys` differ the wheels **must** slip or fight — the build is
mechanically impossible. The app reports the relative surface-speed gap
`|v_front - v_rear| / max * 100%` and a **POSSIBLE / IMPOSSIBLE** verdict against
an adjustable tolerance (default 1%). The Inspector shows the verdict; the
**Drivetrain Explorers → Feasibility** tab adds the numbers plus one-click fixes
(the exact sync wheel diameter or pulley ratio for either axle).

## Windows

- **Project Inspector Top** — headline KPIs (top speed, no-load ceiling, peak g,
  launch force, traction cap, distance), launch behaviour (traction- vs
  motor-limited), the front/rear sync banner, and the auto-recompute toggle /
  manual *Recompute now* button.
- **Drivetrain Inputs** — every editable parameter, grouped (Simulation,
  Reduction, Front/Rear axle, Efficiency, Battery/Motor, Constants). Editing any
  field re-runs the sim instantly (or marks results stale if auto-recompute is
  off). *Reset to template defaults*, *Overlay rear*, *Shade area*.
- **Performance Curves** — Speed / Accel / Force / Torque / Distance vs time,
  front (orange) with the rear axle (teal) optionally overlaid, auto-framed.
- **Results & Export** — a front-vs-rear metrics table and the CSV exporter.
- **Drivetrain Explorers** — a tabbed what-if bench (all sweeps hold every other
  parameter fixed and use the *other* axle as the sync reference):
  - **Feasibility** — the tangential-velocity check + one-click sync fixes.
  - **Wheel sweep** — pick an axle and a Ø range/step; get top speed, peak g,
    launch force, ratio, K_sys, status and the sync gap % per diameter, with the
    perfect-sync diameter called out and Apply-able.
  - **Pulley ratios** — pick an axle and which pulley to walk ±N teeth around the
    current value; get the resulting ratio / K_sys / metrics / sync gap % per
    tooth count, with the perfect-sync ratio called out.

## Viewport schematic

A to-scale side view of the two drive wheels (true relative diameters), each with
its driven pulley, the motor pulley and the belts, coloured front (orange) /
rear (teal). Changing a wheel diameter or tooth count is visible at a glance.

## CSV export

*Results & Export → File name → Export front + rear CSV* writes to
`assets/projects/SF_DrivetrainCalcsApp/logs/<name>.csv` (next to `CosmicApp.exe`;
the resolved absolute path is shown after export). The file keeps the original
tool's **configuration-spec header block**, then a combined time-series table with
both axles' columns (`F_*` and `R_*`). A blank file name auto-stamps with the date.

## Build & run

```
build.bat            # Debug (or: build.bat Release)
```

The engine must already be built (`C:\dev\Cosmic\build\Runtime\Debug\Cosmic.lib`).
This project needs **no engine changes**. Run `CosmicApp.exe`, load
`SF_DrivetrainCalcsApp`, and start typing numbers.

> Note: after adding/removing files in `src/`, the CMake source GLOB is stale —
> delete `build/CMakeCache.txt` (or the `build` folder) so `build.bat`
> reconfigures and picks up the new file set.
