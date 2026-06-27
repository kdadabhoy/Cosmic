// ============================================================================
// sf_test_single_weapon.ino  —  SF_TelemTest: single WEAPON ESC telemetry test
// ============================================================================
//
// Reads the weapon ESC's KISS telemetry and forwards it as a tagged ASCII frame
// over USB-Serial AND Bluetooth-SPP, identical to the main SF_Telem protocol:
//
//      $W,<temp>,<vraw>,<iraw>,<craw>,<erpmraw>*<HH>\n
//
// The weapon shares UART0 (the `Serial` port): its RX is remapped to GPIO13
// while TX stays on GPIO1 so USB debug/output and flashing still work.
//
// Wire the weapon ESC telemetry pad to:
//      WEAPON telem ->  GPIO13  (UART0 RX, remapped)
//      GND          ->  GND     (common ground, required)
//
// Open the SF_TelemTest app, "Single Weapon" screen, connect the COM port.
// ============================================================================

#include <Arduino.h>
#include "BluetoothSerial.h"

#define BT_DEVICE_NAME  "SF_TelemTest"
#define WEAPON_TLM_PIN  13        // weapon ESC telemetry -> UART0 (Serial) RX (remapped)
#define STALE_MS        600

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
    Serial.print(msg);                       // UART0 TX (GPIO1) -> USB
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
    // UART0 / Serial: RX remapped to the weapon pin, TX kept on GPIO1 for USB.
    Serial.setRxBufferSize(512);
    Serial.begin(115200, SERIAL_8N1, WEAPON_TLM_PIN, 1);
    delay(1000);
    SerialBT.begin(BT_DEVICE_NAME);
    Serial.println("# SF_TelemTest single-weapon online");
    Serial.printf("# WEAPON telem pin: %d (UART0 RX remapped)\n", WEAPON_TLM_PIN);
}

void loop()
{
    if (serviceKiss(Serial, 'W', fs)) lastMs = millis();

    if (millis() - lastHeartbeat > 1000)
    {
        const bool ok = (millis() - lastMs) < STALE_MS;
        char s[96];
        snprintf(s, sizeof(s), "# W=%s bt=%s | b=%lu f=%lu d=%lu\n",
                 ok ? "ok" : "ERR", SerialBT.connected() ? "yes" : "no",
                 fs.bytes, fs.good, fs.drops);
        broadcastPrint(s);
        fs.bytes = fs.good = fs.drops = 0;
        lastHeartbeat = millis();
    }
}
