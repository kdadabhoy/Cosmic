# SF_TelemTest

A **bench-test companion** to [SF_Telem](../SF_Telem/README.md). Same engine, same
wire protocol, same look — but every screen is geared toward answering one
diagnostic question, and each has its own dedicated Arduino sketch. Use it to
prove an ESC telemetry chain works **before** running the full app.

It reuses SF_Telem's ASCII wire protocol and host-side decode (`Telemetry.h`), so
the test firmware and the main firmware speak the same language.

## Screens (tests)

Switch with the buttons in *Project Inspector Top*. Each screen shows a big
**PASS / STALE / WAITING** banner, the hardware photo with live overlay boxes,
and frame diagnostics (good frames, frames/sec, CRC/parse errors, last raw frame).

1. **Single Drive** — verify ONE drive ESC streams valid telemetry. Pick
   **Right / Left** at the top to choose which side you're testing (match the
   sketch's `DRIVE_SIDE`, default `'R'`).
2. **Single Weapon** — verify the weapon ESC streams valid telemetry.
3. **Dual Drive** — verify BOTH drive ESCs at once (per-side PASS/PARTIAL).
4. **Sniffer** — detect **any bytes at all** on each telemetry wire, valid KISS
   or not. This answers "is the ESC sending *anything*?" — per-wire
   **DETECTED/SILENT**, bytes/sec, totals, a raw hex dump, and an overall
   "ESP32 LINK ALIVE" indicator. It does **not** validate the data.

## Pins per test (same as the main SF_Telem app)

| Test screen | Reads from | ESP32 GPIO | Board silk | UART | Sketch |
|-------------|-----------|-----------|-----------|------|--------|
| Single Drive  | one drive ESC | **GPIO16** | `RX2` | UART1 | `firmware/sf_test_single_drive` |
| Single Weapon | weapon ESC | **GPIO13** | `D13` | UART0 (RX remapped; USB TX on GPIO1) | `firmware/sf_test_single_weapon` |
| Dual Drive    | right + left drive | **GPIO16** + **GPIO17** | `RX2` / `TX2` | UART1 + UART2 | `firmware/sf_test_dual_drive` |
| Sniffer       | all three wires | **GPIO16 + GPIO17 + GPIO13** | `RX2` / `TX2` / `D13` | UART1 + UART2 + UART0 | `firmware/sf_test_sniffer` |

A **common ground** between the ESC(s) and the ESP32 is required in every case.
Telemetry is one-way 3.3 V UART @ 115200 8N1. To move a wire, edit the
`*_TLM_PIN` define at the top of the relevant sketch.

## Firmware

Each sketch forwards over **USB-Serial and Bluetooth-SPP** (device name
`SF_TelemTest`), so connect via either COM port.

- `sf_test_single_drive` / `sf_test_single_weapon` / `sf_test_dual_drive` decode
  the 10-byte KISS frame (CRC8-checked) and emit the tagged ASCII frame
  `$<S>,<temp>,<vraw>,<iraw>,<craw>,<erpmraw>*<HH>` (S = `R`/`L`/`W`).
- `sf_test_sniffer` does **not** decode. It counts raw bytes per wire and emits
  `SNIFF,<tag>,<bytesThisInterval>,<totalBytes>,<hexSample>` every ~150 ms.

### Copy a ready-to-flash sketch from the app

The **Serial Link → Arduino Firmware** section (open by default) **auto-matches the
active test screen** — pick the screen you're testing, set its **GPIO** pin(s) —
GPIO numbers, *not* the 1–30 board positions (e.g. `16` = pad `RX2`); each field
shows its pad name live (single-drive also has a Right/Left tag toggle; the `(?)`
hint and **Pinout** pop-out show `assets/images/ESP32_Dev_Pin_Layout.png` with a
naming-scheme legend underneath), then **Copy sketch (.ino)**. The
generated sketch has your pins baked in — paste into the Arduino IDE and upload.
Templates live in `src/FirmwareTemplates.h` and mirror `firmware/sf_test_*.ino`.

## Files

| File | Purpose |
|------|---------|
| `src/SF_TelemTest.{h,cpp}` | Root manager: 4 test screens, switcher, dock layout, DLL entry points. |
| `src/TestHub.{h,cpp}` | Shared serial backbone: parse `$frames` (per-ESC counts/fps/last-frame/decode), `SNIFF` lines (per-wire activity), raw link bytes/sec; Serial Link UI. |
| `src/TestLayers.{h,cpp}` | The four screens (PASS/FAIL banners, photo overlays, diagnostics, sniffer cards). |
| `src/Telemetry.h` | Wire protocol + decode (copied from SF_Telem — identical format). |
| `src/StatBox.h` | Framed value widgets + responsive grid. |
| `src/FirmwareTemplates.h` | Embedded `.ino` sources for all four tests (in-app "Copy sketch"). |
| `assets/images/` | Weapon + drivetrain photos + `ESP32_Dev_Pin_Layout.png` (pinout pop-out). |
| `firmware/sf_test_*` | One Arduino sketch per test (see table above). |

## Workflow

1. **Build:** `build_all.bat` at the repo root (engine + all projects), or
   `build_engine.bat` then `build.bat` here.
2. Flash the sketch for the test you want to run (e.g. `sf_test_single_drive`),
   wire the ESC telemetry pad(s) to the pin(s) in the table + common GND.
3. Run `CosmicApp.exe`, load **SF_TelemTest**, pick the matching screen, and
   **Serial Link → Connect** (115200).
4. Power the ESC so it streams. The banner should turn green and the boxes
   populate. Use **Reset Counts** to start a fresh measurement.
5. If you get nothing, switch to **Sniffer** (flash `sf_test_sniffer`) to see
   whether the wire carries any bytes at all — that isolates a dead ESC/wire
   from a decode/baud problem.

## Engine note

Uses `DockPort::Center` + `WorkspaceLayer::SetViewportVisible(false)` (no 3D
scene), the `Cosmic::UI` overlay/font helpers and `Cosmic::Texture2D` for the
photo dashboards, and the shared `Cosmic::SerialPort`. Same screen-manager
pattern as SF_Telem.
