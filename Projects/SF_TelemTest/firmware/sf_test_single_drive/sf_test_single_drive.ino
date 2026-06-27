// ============================================================================
// sf_test_single_drive.ino  —  SF_TelemTest: single DRIVE ESC telemetry test
// ============================================================================
//
// Reads ONE drive ESC's KISS telemetry and forwards it as a tagged ASCII frame
// over USB-Serial AND Bluetooth-SPP, identical to the main SF_Telem protocol:
//
//      $<S>,<temp>,<vraw>,<iraw>,<craw>,<erpmraw>*<HH>\n   (S = DRIVE_SIDE, 'R' or 'L')
//
// Wire the drive ESC telemetry pad to:
//      DRIVE telem  ->  GPIO16  (UART1 RX, board silk "RX2")
//      GND          ->  GND     (common ground, required)
//
// Open the SF_TelemTest app, "Single Drive" screen, connect the COM port.
// ============================================================================

#include <Arduino.h>
#include "BluetoothSerial.h"

#define BT_DEVICE_NAME  "SF_TelemTest"
#define DRIVE_TLM_PIN   16        // drive ESC telemetry -> UART1 RX
#define DRIVE_SIDE      'R'       // 'R' or 'L' — match the app's Right/Left toggle
#define ESC_BAUD        115200
#define STALE_MS        600

HardwareSerial  SerialDrive(1);   // UART1
BluetoothSerial SerialBT;

struct ESC_Data { uint8_t temp = 0; uint16_t voltage = 0, current = 0, consumption = 0, erpm = 0; bool valid = false; };
unsigned long lastMs = 0, lastHeartbeat = 0;

// ---- KISS CRC8 ----
uint8_t update_crc8(uint8_t crc, uint8_t seed)
{
    uint8_t u = crc ^ seed;
    for (int i = 0; i < 8; i++) u = (u & 0x80) ? (0x07 ^ (u << 1)) : (u << 1);
    return u;
}
uint8_t get_crc8(uint8_t* b, uint8_t size)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < size; i++) crc = update_crc8(b[i], crc);
    return crc;
}
bool parseESC(uint8_t* b, ESC_Data& d)
{
    if (get_crc8(b, 9) != b[9]) return false;
    d.temp = b[0];
    d.voltage     = (b[1] << 8) | b[2];
    d.current     = (b[3] << 8) | b[4];
    d.consumption = (b[5] << 8) | b[6];
    d.erpm        = (b[7] << 8) | b[8];
    d.valid = true;
    return true;
}

// BT write can briefly block under SPP congestion, but that's no longer fatal: the
// self-syncing reader below re-locks cleanly after any stall instead of desyncing.
void broadcastPrint(const char* msg)
{
    Serial.print(msg);
    if (SerialBT.connected()) SerialBT.print(msg);
}

void sendFrame(char side, const ESC_Data& d)
{
    char payload[48];
    int n = snprintf(payload, sizeof(payload), "%c,%u,%u,%u,%u,%u",
                     side, (unsigned)d.temp, (unsigned)d.voltage, (unsigned)d.current,
                     (unsigned)d.consumption, (unsigned)d.erpm);
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
// byte-drops to re-lock, never a permanent offset like the old readBytes(10) path.
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

FrameSync fs;

void setup()
{
    Serial.begin(115200);
    delay(500);
    SerialDrive.setRxBufferSize(512);
    SerialDrive.begin(ESC_BAUD, SERIAL_8N1, DRIVE_TLM_PIN, -1, false);  // RX-only one-wire
    SerialBT.begin(BT_DEVICE_NAME);
    Serial.println("# SF_TelemTest single-drive online");
    Serial.printf("# DRIVE telem pin: %d (UART1)\n", DRIVE_TLM_PIN);
}

void loop()
{
    if (serviceKiss(SerialDrive, DRIVE_SIDE, fs)) lastMs = millis();

    if (millis() - lastHeartbeat > 1000)
    {
        const bool ok = (millis() - lastMs) < STALE_MS;
        char s[96];
        snprintf(s, sizeof(s), "# %c=%s bt=%s | b=%lu f=%lu d=%lu\n",
                 DRIVE_SIDE, ok ? "ok" : "ERR", SerialBT.connected() ? "yes" : "no",
                 fs.bytes, fs.good, fs.drops);
        broadcastPrint(s);
        fs.bytes = fs.good = fs.drops = 0;
        lastHeartbeat = millis();
    }
}
