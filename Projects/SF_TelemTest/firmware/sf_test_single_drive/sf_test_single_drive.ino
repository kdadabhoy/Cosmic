// ============================================================================
// sf_test_single_drive.ino  —  SF_TelemTest: single DRIVE ESC telemetry test
// ============================================================================
//
// Reads ONE drive ESC's KISS telemetry and forwards it as a tagged ASCII frame
// over USB-Serial AND Bluetooth-SPP, identical to the main SF_Telem protocol:
//
//      $R,<temp>,<vraw>,<iraw>,<craw>,<erpmraw>*<HH>\n
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
#define ESC_BAUD        115200
#define STALE_MS        600

HardwareSerial  SerialDrive(1);   // UART1
BluetoothSerial SerialBT;

struct ESC_Data { uint8_t temp = 0; uint16_t voltage = 0, current = 0, consumption = 0, erpm = 0; bool valid = false; };
ESC_Data      tlm;
uint8_t       buf[10];
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

bool serviceSide(Stream& port)
{
    if (port.available() > 50) while (port.available()) port.read();  // resync guard
    if (port.available() >= 10)
    {
        port.readBytes(buf, 10);
        if (parseESC(buf, tlm)) { sendFrame('R', tlm); tlm.valid = false; return true; }
    }
    return false;
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    SerialDrive.begin(ESC_BAUD, SERIAL_8N1, DRIVE_TLM_PIN, -1, false);  // RX-only one-wire
    SerialBT.begin(BT_DEVICE_NAME);
    Serial.println("# SF_TelemTest single-drive online");
    Serial.printf("# DRIVE telem pin: %d (UART1)\n", DRIVE_TLM_PIN);
}

void loop()
{
    if (serviceSide(SerialDrive)) lastMs = millis();

    if (millis() - lastHeartbeat > 1000)
    {
        const bool ok = (millis() - lastMs) < STALE_MS;
        char s[48];
        snprintf(s, sizeof(s), "# R=%s bt=%s\n", ok ? "ok" : "ERR", SerialBT.connected() ? "yes" : "no");
        broadcastPrint(s);
        lastHeartbeat = millis();
    }
}
