# Intermittent "stale weapon" telemetry — KISS frame desync

> **Verified against commit:** `88b4b27` (references to `Projects/SF_Telem/src/FirmwareTemplates.h`,
> `Projects/SF_TelemTest/src/FirmwareTemplates.h`, `Projects/SF_TelemTest/src/TestHub.{h,cpp}`, and the
> `Projects/SF_Telem*/firmware/**/*.ino` sketches).
> **Status:** Fixed in firmware. Keep — the delimiter-less-framing failure mode is generic to any fixed-length
> serial protocol without a start byte, and the `availableForWrite()` footgun applies to ESP32 BluetoothSerial.

## Symptom

On the combat robot, the SF_Telem desktop app intermittently showed the **weapon** ESC reading as **STALE**,
but only *sometimes*, and most often **when the weapon was spun up** (the drive ESCs stayed live). The separate
**sniffer** firmware showed a *constant* byte stream on the weapon wire the whole time, which made it look like
the data was arriving fine and the app was at fault.

That last observation is the trap. The sniffer (`sf_test_sniffer.ino`) only **counts bytes** — it never checks
CRC or frame alignment. "Constant stream" only proves bytes are *present*, not that they are *valid* or
*aligned*. So the real question was never "are bytes arriving?" (they are) but "why does the firmware fail to
turn them into valid frames?".

## Background: the wire format

Each ESC emits **KISS telemetry**: a fixed **10-byte, delimiter-less** frame where `byte[9]` is a CRC8 over
`byte[0..8]`. Frames are sent back-to-back with a short inter-frame gap (~30 ms at the observed ~33 fps).
There is **no start byte** — nothing in the stream marks where a frame begins.

The ESP32 reads three of these (RIGHT/LEFT on UART1/UART2, WEAPON on UART0 with RX remapped) and forwards each
as a tagged ASCII line `$<S>,<temp>,<v>,<i>,<c>,<erpm>*<HH>\n` over USB + Bluetooth. The host treats a side as
stale after 1.5 s with no valid `$` frame (`TelemHub::Stale`, `k_StaleTimeout`).

## Root cause

The old reader assumed that whatever 10 bytes were sitting in the UART FIFO formed one aligned frame:

```cpp
if (port.available() > 50) while (port.available()) port.read();  // "resync guard": flush everything
if (port.available() >= 10) { port.readBytes(buf, 10); parseESC(buf, ...); }
```

This desyncs **permanently** under one disturbance:

1. `loop()` stalls occasionally. The prime suspect is `SerialBT.print()` — ESP32 classic-Bluetooth SPP
   **blocks** when its TX buffer is congested. Spinning the weapon adds current draw and EMI on top.
2. A stall long enough to pile up >50 bytes triggers the `while(available()) read()` flush. That flush lands
   **mid-frame** and leaves a few residual bytes (say 4) in the FIFO.
3. Now every `readBytes(10)` reads `[4 tail bytes of frame N] + [6 head bytes of frame N+1]` → CRC fails. And
   it **stays** failed: each subsequent frame leaves the same 4-byte residual, so the read phase never changes.
   The ~30 ms inter-frame gap doesn't rescue it either — there are always leftover bytes, so the FIFO never
   drains to empty to re-align. The side is stuck off-by-4 **forever**, until another lucky flush happens to
   land on a boundary. That is exactly "works sometimes, inconsistently."

Why the weapon specifically: it shares UART0 with all the USB `Serial.print` traffic, and it is the motor
actually being spun (the disturbance source). The drives, on dedicated RX-only UARTs and not being spun, kept
re-aligning by chance.

## Fix

The core change is a **self-synchronizing reader** that exploits the CRC instead of trusting byte counts. A
correctly-aligned 10-byte window is the *only* window whose `byte[9]` matches the CRC8 of its first 9 bytes, so
the parser can *search* for alignment:

```cpp
int serviceKiss(Stream& port, char side, FrameSync& fs, bool emit = true)
{
    ESC_Data d;
    while (port.available()) {                 // ingest everything into a rolling buffer
        int b = port.read(); if (b < 0) break;
        fs.bytes++;
        if (fs.len >= sizeof(fs.buf)) { memmove(fs.buf, fs.buf + 1, sizeof(fs.buf) - 1); fs.len--; }
        fs.buf[fs.len++] = (uint8_t)b;
    }
    int got = 0; uint8_t i = 0;
    while ((uint8_t)(fs.len - i) >= 10) {
        if (get_crc8(&fs.buf[i], 9) == fs.buf[i + 9]) {   // aligned frame
            parseESC(&fs.buf[i], d); if (emit) sendFrame(side, d);
            fs.good++; got++; i += 10;                    // consume 10 — stay locked on the boundary
        } else { fs.drops++; i += 1; }                    // misaligned — drop ONE byte, slide the window
    }
    if (i) { fs.len -= i; memmove(fs.buf, fs.buf + i, fs.len); }   // keep the unconsumed tail
    return got;
}
```

**Why this addresses the root cause, not the symptom:** on a CRC miss it advances by **one** byte and re-tests,
sliding the 10-byte window until it lands on a real boundary. Any disturbance (mid-frame flush, a corrupt or
dropped byte) costs **at most ~9 byte-drops** to re-lock — never a permanent offset. Re-lock is CRC-driven, so
the inter-frame gap is irrelevant, and every real frame re-asserts the lock. (A misaligned window has a ~1/256
chance of a false CRC pass; if it ever happens during resync it self-heals on the next real frame.)

Supporting changes:

- **`setRxBufferSize(512)`** on every telemetry UART before `begin()`, so a transient stall can't overflow the
  FIFO and shed bytes.
- **Diagnostic counters in the heartbeat.** Each decode sketch now appends per-side `b`/`f`/`d` =
  bytes / valid frames / resync-drops since the last beat, e.g.
  `# R=ok L=ERR W=ok bt=yes | ... Wb=250 Wf=5 Wd=0`. The host ignores `#` lines, so it shows verbatim in the
  in-app serial monitor. This turns the original ambiguity into a decision: `Wb` high + `Wf` low + `Wd` high =
  noisy/corrupt wire (EMI); `Wb` → 0 = ESC stopped / wire / ground; `Wb` high + `Wf` healthy but app stale =
  link/BT.
- **Second sniffer.** Kept the raw byte-counter and added `sf_test_sniffer_decode.ino` (`BuildDecodeSniffer`)
  which runs `serviceKiss(..., emit=false)` and reports `SNIFF,<tag>,<bytes>,<goodFrames>,<crcDrops>` — a real
  "are the bytes *valid*?" probe. The Sniffer screen has a Raw / Decode toggle (`TestHub.cpp`).

### Modularity

The KISS logic (CRC, parse, output, `FrameSync` + `serviceKiss`) is factored into a single `FwKissModule()`
string **per app**, concatenated by every decode generator in that app's `FirmwareTemplates.h`. The two copies
(one in SF_Telem, one in SF_TelemTest) are intentional duplicates — the apps are independent CMake plugins — and
are kept in sync the same way the templates already mirror the standalone `firmware/*.ino` sketches.

## Gotcha: `BluetoothSerial::availableForWrite()`

The first cut of the "don't let BT stall the loop" idea gated the BT write on
`SerialBT.availableForWrite() >= strlen(msg)`. **Do not do this on ESP32.** The installed core
(`esp32 3.3.10`) `BluetoothSerial` does **not** override `availableForWrite()`, so it inherits
`Print::availableForWrite()`, which returns **0**. The guard would then be `0 >= N` → always false → **all
Bluetooth output suppressed**, silently killing the wireless link the robot actually uses. It was removed; BT
output is unconditional on `connected()`. This is safe now precisely because the self-syncing reader makes a BT
stall non-fatal — bytes back up, the loop drains them, and the parser re-locks — so there is no longer any need
to avoid the occasional blocking write.

## Verification

1. Flash `sf_telem_esp32`, open the in-app serial monitor. At idle expect steady `$W,...*HH` frames and, in the
   heartbeat, `Wf ≈ Wb/10` with `Wd ≈ 0`.
2. Spin the weapon and watch the counters + the app. The weapon should stay **LIVE**. If it ever stales, the
   counters now classify it: `Wd` climbing → wire noise/EMI (hardware); `Wb` dropping → dropout (wiring/ground).
3. Flash `sf_test_sniffer_decode` and confirm it reports healthy `goodFrames` where the raw sniffer only showed
   a byte count.

## Hardware note (out of scope of the firmware fix)

The firmware now survives a single disturbance, but if the diagnostics show `Wb` high / `Wf` low while spinning,
the disturbance is **physical** and worth fixing at the source: power the ESP32 from a clean/separate BEC (not
the weapon ESC's), twist the telemetry wire with its ground and route it away from motor phase wires, add a
series ~1k resistor at the input, and ensure a short solid common ground. Also seen in the capture: the **left
drive** wire read 0 bytes — dead/unplugged, unrelated to the weapon issue.
