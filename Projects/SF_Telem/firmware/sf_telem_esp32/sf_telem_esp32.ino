// ============================================================================
// sf_telem_esp32.ino  —  SF_Telem firmware (2 DRIVE ESCs + 1 WEAPON ESC)
// ============================================================================
//
// One ESP32 reads THREE ESCs and forwards RAW KISS telemetry to the host over
// Bluetooth-SPP (and USB-TX). Each frame is tagged with its side so the host
// always routes drive->drive and weapon->weapon:
//
//      $<S>,<temp>,<vraw>,<iraw>,<craw>,<erpmraw>*<HH>\n      S = 'R','L','W'
//
//   $          start-of-frame
//   <S>        side tag: 'R' right drive, 'L' left drive, 'W' weapon  <-- routing flag
//   <temp>     raw KISS temperature  (deg C)
//   <vraw>     raw voltage           (centi-volts)
//   <iraw>     raw current           (centi-amps)
//   <craw>     raw consumption       (mAh)
//   <erpmraw>  raw eRPM / 100
//   *          end-of-payload
//   <HH>       XOR checksum of every char between '$' and '*', 2 hex digits
//
// UART ASSIGNMENT (ESP32 has three hardware UARTs)
// -----------------------------------------------
//   RIGHT  drive  -> UART1
//   LEFT   drive  -> UART2
//   WEAPON        -> UART0 (the `Serial` port) with its RX REMAPPED to a free
//                    GPIO. TX stays on GPIO1 so USB debug + Bluetooth output
//                    still work, and flashing (bootloader on GPIO1/3) is
//                    unaffected. Telemetry is emitted over Bluetooth either way.
//
//   >>> Change the three *_TLM_PIN defines to move any wire. <<<
//
// ROBUSTNESS: each ESC is serviced independently. If a telemetry wire is not
// plugged in, that side just stops sending '$' frames; the others keep
// streaming and the host flags the missing one. The '#' heartbeat shows which
// sides are alive (R=ok/ERR L=ok/ERR W=ok/ERR).
// ============================================================================

#include <Arduino.h>
#include "BluetoothSerial.h"

// ---- Configuration ---------------------------------------------------------
#define BT_DEVICE_NAME  "SF_Telem"
#define RIGHT_TLM_PIN   16      // RIGHT drive ESC telemetry -> UART1 RX
#define LEFT_TLM_PIN    17      // LEFT  drive ESC telemetry -> UART2 RX
#define WEAPON_TLM_PIN  13      // WEAPON ESC telemetry -> UART0 (Serial) RX (remapped)
#define ESC_BAUD        115200
#define SIDE_STALE_MS   600     // no valid packet within this -> "ERR" in heartbeat

HardwareSerial  SerialRight(1); // UART1
HardwareSerial  SerialLeft(2);  // UART2
// Weapon uses Serial (UART0). TX (GPIO1) keeps USB output; RX is remapped below.
BluetoothSerial SerialBT;

enum { R = 0, L = 1, W = 2, N = 3 };
const char SIDE_CHAR[N] = { 'R', 'L', 'W' };

struct ESC_Data
{
    uint8_t  temp        = 0;
    uint16_t voltage     = 0;
    uint16_t current     = 0;
    uint16_t consumption = 0;
    uint16_t erpm        = 0;
    bool     valid       = false;
};

unsigned long lastMs[N]      = { 0, 0, 0 };
unsigned long lastHeartbeat  = 0;

// ---- KISS CRC8 (validates the ESC -> ESP link) -----------------------------
uint8_t update_crc8(uint8_t crc, uint8_t crc_seed)
{
    uint8_t crc_u = crc ^ crc_seed;
    for (int i = 0; i < 8; i++)
        crc_u = (crc_u & 0x80) ? (0x07 ^ (crc_u << 1)) : (crc_u << 1);
    return crc_u;
}

uint8_t get_crc8(uint8_t* b, uint8_t size)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < size; i++) crc = update_crc8(b[i], crc);
    return crc;
}

bool parseESC(uint8_t* buffer, ESC_Data& d)
{
    if (get_crc8(buffer, 9) != buffer[9]) return false;
    d.temp        = buffer[0];
    d.voltage     = (buffer[1] << 8) | buffer[2];
    d.current     = (buffer[3] << 8) | buffer[4];
    d.consumption = (buffer[5] << 8) | buffer[6];
    d.erpm        = (buffer[7] << 8) | buffer[8];
    d.valid       = true;
    return true;
}

// ---- Output helpers --------------------------------------------------------
// BT write can briefly block under SPP congestion, but that's no longer fatal: the
// self-syncing reader below re-locks cleanly after any stall instead of desyncing.
void broadcastPrint(const char* msg)
{
    Serial.print(msg);                       // UART0 TX -> USB (debug)
    if (SerialBT.connected()) SerialBT.print(msg);
}

void sendFrame(char side, const ESC_Data& d)
{
    char payload[48];
    int n = snprintf(payload, sizeof(payload), "%c,%u,%u,%u,%u,%u",
                     side,
                     (unsigned)d.temp,
                     (unsigned)d.voltage,
                     (unsigned)d.current,
                     (unsigned)d.consumption,
                     (unsigned)d.erpm);
    if (n <= 0) return;

    uint8_t crc = 0;
    for (int i = 0; i < n; i++) crc ^= (uint8_t)payload[i];

    char frame[64];
    snprintf(frame, sizeof(frame), "$%s*%02X\n", payload, crc);
    broadcastPrint(frame);
}

// ---- Self-synchronizing KISS reader ----------------------------------------
// KISS frames are 10 delimiter-less bytes; byte[9] is a CRC8 over bytes[0..8], so a
// correctly-aligned window is the ONLY one that passes CRC. Ingest every available byte,
// then test the CRC at the current offset: pass -> consume 10 (locked on the boundary);
// fail -> drop ONE byte and re-test (slides onto the real boundary). A glitch costs <=9
// byte-drops to re-lock, never a permanent offset like the old readBytes(10) path that
// stayed stuck at the wrong phase until a lucky flush. emit=false suppresses $-output.
struct FrameSync
{
    uint8_t       buf[64];
    uint8_t       len   = 0;
    unsigned long bytes = 0, good = 0, drops = 0;   // diagnostics; reset each heartbeat
};

int serviceKiss(Stream& port, char side, FrameSync& fs, bool emit = true)
{
    ESC_Data d;
    while (port.available())
    {
        int b = port.read();
        if (b < 0) break;
        fs.bytes++;
        if (fs.len >= sizeof(fs.buf)) { memmove(fs.buf, fs.buf + 1, sizeof(fs.buf) - 1); fs.len--; }
        fs.buf[fs.len++] = (uint8_t)b;
    }
    int got = 0;
    uint8_t i = 0;
    while ((uint8_t)(fs.len - i) >= 10)
    {
        if (get_crc8(&fs.buf[i], 9) == fs.buf[i + 9])
        {
            parseESC(&fs.buf[i], d);
            if (emit) sendFrame(side, d);
            fs.good++; got++; i += 10;
        }
        else { fs.drops++; i += 1; }
    }
    if (i) { fs.len -= i; memmove(fs.buf, fs.buf + i, fs.len); }
    return got;
}

FrameSync fs[N];

// ---- Setup -----------------------------------------------------------------
void setup()
{
    // UART0 / Serial: RX remapped to the WEAPON pin, TX kept on GPIO1 for USB.
    // Bigger RX buffers so a transient loop stall can't overflow the FIFO and drop bytes.
    Serial.setRxBufferSize(512);
    Serial.begin(115200, SERIAL_8N1, WEAPON_TLM_PIN, 1);
    delay(1000);

    // One-wire telemetry on the two drive UARTs (RX only, TX pin = -1).
    SerialRight.setRxBufferSize(512);
    SerialLeft.setRxBufferSize(512);
    SerialRight.begin(ESC_BAUD, SERIAL_8N1, RIGHT_TLM_PIN, -1, false);
    SerialLeft.begin(ESC_BAUD, SERIAL_8N1, LEFT_TLM_PIN,  -1, false);

    SerialBT.begin(BT_DEVICE_NAME);

    Serial.println("# Telemetry System Online (2 drive + 1 weapon ESC)");
    Serial.printf("# Bluetooth: %s\n", BT_DEVICE_NAME);
    Serial.printf("# RIGHT pin: %d (UART1)   LEFT pin: %d (UART2)   WEAPON pin: %d (UART0)\n",
                  RIGHT_TLM_PIN, LEFT_TLM_PIN, WEAPON_TLM_PIN);
}

// ---- Loop ------------------------------------------------------------------
void loop()
{
    if (serviceKiss(SerialRight, SIDE_CHAR[R], fs[R])) lastMs[R] = millis();
    if (serviceKiss(SerialLeft,  SIDE_CHAR[L], fs[L])) lastMs[L] = millis();
    if (serviceKiss(Serial,      SIDE_CHAR[W], fs[W])) lastMs[W] = millis();

    // Heartbeat — '#' comment line; the host logs but ignores it. Shows which sides are
    // alive plus per-side diagnostics since the last beat: <X>b = bytes received,
    // <X>f = valid frames decoded, <X>d = bytes dropped re-syncing. Bytes high with
    // frames low / drops high == a noisy/corrupt wire (e.g. weapon EMI), not a framing bug.
    if (millis() - lastHeartbeat > 1000)
    {
        const bool rOk = (millis() - lastMs[R]) < SIDE_STALE_MS;
        const bool lOk = (millis() - lastMs[L]) < SIDE_STALE_MS;
        const bool wOk = (millis() - lastMs[W]) < SIDE_STALE_MS;
        char status[200];
        snprintf(status, sizeof(status),
                 "# R=%s L=%s W=%s bt=%s | Rb=%lu Rf=%lu Rd=%lu Lb=%lu Lf=%lu Ld=%lu Wb=%lu Wf=%lu Wd=%lu\n",
                 rOk ? "ok" : "ERR", lOk ? "ok" : "ERR", wOk ? "ok" : "ERR",
                 SerialBT.connected() ? "yes" : "no",
                 fs[R].bytes, fs[R].good, fs[R].drops,
                 fs[L].bytes, fs[L].good, fs[L].drops,
                 fs[W].bytes, fs[W].good, fs[W].drops);
        broadcastPrint(status);
        for (int k = 0; k < N; k++) { fs[k].bytes = fs[k].good = fs[k].drops = 0; }
        lastHeartbeat = millis();
    }
}
