// ============================================================================
// sf_test_sniffer_decode.ino  —  SF_TelemTest: DECODING telemetry sniffer
// ============================================================================
//
// Companion to sf_test_sniffer.ino. The raw sniffer only COUNTS bytes — it can't
// tell valid telemetry from noise. This one runs the real self-syncing KISS
// decoder and reports, per wire every ~150 ms:
//
//      SNIFF,<tag>,<bytesThisInterval>,<goodFrames>,<crcDrops>\n   (tag = R / L / W)
//
// goodFrames ~= bytes/10  -> clean, valid telemetry.
// bytes high but goodFrames low / crcDrops high -> the wire is noisy/corrupt
// (e.g. weapon-motor EMI), NOT a framing bug. Use this to tell the two apart.
//
// Wire the ESC telemetry pads to:
//      RIGHT  -> GPIO16 (UART1)   LEFT -> GPIO17 (UART2)   WEAPON -> GPIO13 (UART0 RX remapped)
//      GND    -> GND    (common ground, required)
//
// Open the SF_TelemTest app, "Sniffer" screen, pick "Decode", connect the COM port.
// ============================================================================

#include <Arduino.h>
#include "BluetoothSerial.h"

#define BT_DEVICE_NAME  "SF_TelemTest"
#define RIGHT_TLM_PIN   16
#define LEFT_TLM_PIN    17
#define WEAPON_TLM_PIN  13        // UART0 RX (remapped); TX stays on GPIO1 for USB
#define ESC_BAUD        115200
#define REPORT_MS       150

HardwareSerial  SerialRight(1);   // UART1
HardwareSerial  SerialLeft(2);    // UART2
BluetoothSerial SerialBT;

enum { R = 0, L = 1, W = 2, N = 3 };
const char TAG[N] = { 'R', 'L', 'W' };

struct ESC_Data { uint8_t temp = 0; uint16_t voltage = 0, current = 0, consumption = 0, erpm = 0; bool valid = false; };
unsigned long lastReport = 0;

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
// fail -> drop ONE byte and re-test (slides onto the real boundary). emit=false suppresses
// the $-frame output — here we only want the bytes/good/drops diagnostics.
struct FrameSync
{
    uint8_t       buf[64];
    uint8_t       len   = 0;
    unsigned long bytes = 0, good = 0, drops = 0;
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

void setup()
{
    // Weapon on UART0: RX remapped to GPIO13, TX kept on GPIO1 for USB output.
    Serial.setRxBufferSize(512);
    Serial.begin(115200, SERIAL_8N1, WEAPON_TLM_PIN, 1);
    delay(1000);
    SerialRight.setRxBufferSize(512);
    SerialLeft.setRxBufferSize(512);
    SerialRight.begin(ESC_BAUD, SERIAL_8N1, RIGHT_TLM_PIN, -1, false);
    SerialLeft.begin(ESC_BAUD, SERIAL_8N1, LEFT_TLM_PIN,  -1, false);
    SerialBT.begin(BT_DEVICE_NAME);
    Serial.println("# SF_TelemTest decode-sniffer online");
    Serial.printf("# Sniffing R=%d (UART1) L=%d (UART2) W=%d (UART0)\n",
                  RIGHT_TLM_PIN, LEFT_TLM_PIN, WEAPON_TLM_PIN);
}

void loop()
{
    serviceKiss(SerialRight, 'R', fs[R], false);
    serviceKiss(SerialLeft,  'L', fs[L], false);
    serviceKiss(Serial,      'W', fs[W], false);

    if (millis() - lastReport >= REPORT_MS)
    {
        for (int i = 0; i < N; i++)
        {
            char line[64];
            snprintf(line, sizeof(line), "SNIFF,%c,%lu,%lu,%lu\n", TAG[i], fs[i].bytes, fs[i].good, fs[i].drops);
            broadcastPrint(line);
            fs[i].bytes = fs[i].good = fs[i].drops = 0;
        }
        lastReport = millis();
    }
}
