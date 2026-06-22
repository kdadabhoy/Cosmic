# SF_Telem — KISS Telemetry & ESP32 Wiring

Authoritative reference for **how the telemetry works** and **which ESP32 pins
must be used**. Verified against a 30-pin ESP32 Dev Module (ESP-WROOM-32) and the
project's combat-robot wiring diagram (drive 6S + weapon 12S, AM32 ESCs).

## How KISS ESC telemetry works

Each ESC (KISS / BLHeli_32 / **AM32**, e.g. the SQESC 12100/12200) has a dedicated
**telemetry wire** separate from the throttle/PWM signal. On that wire the ESC is
a **one-way UART transmitter**:

- **Electrical:** 3.3 V logic, **115200 baud, 8N1**, output-only. It is an INPUT
  to the ESP32 (connect to an ESP32 RX pin). The line idles high.
- **Packet:** a fixed **10-byte binary frame**, big-endian:

  | Byte(s) | Field | Units |
  |---|---|---|
  | 0 | Temperature | °C |
  | 1–2 | Voltage | centi-volts (V = raw/100) |
  | 3–4 | Current | centi-amps (A = raw/100) |
  | 5–6 | Consumption | mAh |
  | 7–8 | eRPM / 100 | (eRPM = raw × 100) |
  | 9 | **CRC8** (poly 0x07 over bytes 0–8) | — |

- **Cadence:** with no flight controller requesting frames, set the ESC to
  **continuous / "30 ms" auto-telemetry** (AM32 setting) so it streams on its own.

The ESP32 firmware validates the CRC8, then re-emits each reading as a small ASCII
frame tagged with its side (`R`/`L`/`W`) over Bluetooth. **All engineering
conversion (volts, amps, RPM, speed) happens on the PC**, live-tunable — the
firmware ships raw values only.

## Why specific pins are required

The classic ESP32 has exactly **three hardware UARTs**, and **UART0 is the USB /
flashing port**. Reading three ESCs at once needs all three UART peripherals, so
one ESC (the weapon) must share UART0. The firmware remaps UART0's **RX** to a
free GPIO and keeps **TX on GPIO1**, so USB debug output and flashing still work.

> Pin choice is via the ESP32 **GPIO matrix** — any UART's RX can route to almost
> any GPIO. The `RX2`/`TX2` silk labels on the board are only the chip *defaults*;
> a pin labeled `TX2` works perfectly as a UART **input** here.

## The pins to use (matches `firmware/sf_telem_esp32`)

| ESC telemetry wire | ESP32 GPIO | Board silk (30-pin) | UART |
|---|---|---|---|
| **RIGHT** drive (T1) | **GPIO16** | `RX2` (pin 21) | UART1 |
| **LEFT** drive (T2)  | **GPIO17** | `TX2` (pin 22) | UART2 |
| **WEAPON** (T3)      | **GPIO13** | `D13` (pin 13) | UART0 (RX remapped) |
| **Ground**           | **GND**    | `GND` (pin 14)  | → star ground |
| **5 V in**           | **VIN/5V** | `VIN` (pin 15)  | from the UBEC 5 V rail |

These are set at the top of the sketch and easy to change:

```c
#define RIGHT_TLM_PIN   16   // UART1
#define LEFT_TLM_PIN    17   // UART2
#define WEAPON_TLM_PIN  13   // UART0 (Serial), RX remapped, TX stays on GPIO1
```

All three GPIOs are safe on this board: not flash pins (6–11), not strapping pins
(0/2/5/12/15), not input-only (34–39). **Avoid GPIO12** for any ESC line — it is a
strapping pin that can block boot if held high.

## Does it check out with your wiring diagram?

Yes. Each point that matters for telemetry:

- **Three telemetry UART lines → ESP32.** Your diagram routes T1 (right ESC),
  T2 (left ESC), T3 (weapon ESC) into the "ESP32 TELEMETRY" block. Land them on
  GPIO16 / GPIO17 / GPIO13 respectively. (If Right/Left read swapped in the app,
  just swap those two wires or the two `*_TLM_PIN` defines.)
- **Common ground is mandatory and you have it.** The diagram's
  **"ALL GND COMMON @ STAR POINT"** is exactly the requirement: the weapon runs
  off a separate 12S pack, but its telemetry is a 3.3 V signal referenced to the
  shared star ground, so the drive-side-powered ESP32 can read it. The ESP32 GND
  **must** tie to that star point.
- **ESP32 power.** Feed the ESP32 from the **UBEC 5 V** rail into **VIN** (5 V),
  GND to the star point. Do not back-feed any ESC 5 V into the ESP32 elsewhere.
- **Voltage levels are safe.** AM32 telemetry is 3.3 V on all three ESCs (drive
  and weapon alike) — no level shifting needed. The 44.4 V weapon bus never
  touches the signal wire.
- **Telemetry wire only.** Connect the ESC's **telemetry pad** (not the PWM
  signal wire) to the ESP32 RX pin. It is RX-only; nothing is sent back.

## Robustness (1, 2, or 3 wires)

Each line is independent. Plug in only the weapon, only the drives, or all three
— the app shows present ESCs live and the rest as "no signal". The firmware's
`# R=ok L=ERR W=ok` heartbeat reports which sides are alive at the bench.

## Flashing note

Because the weapon uses UART0, keep its wire on **GPIO13** (not the default RX0 /
GPIO3). The bootloader drives GPIO1/GPIO3 during upload, so flashing is
unaffected, and GPIO13 is not a strapping pin — no boot conflict.
