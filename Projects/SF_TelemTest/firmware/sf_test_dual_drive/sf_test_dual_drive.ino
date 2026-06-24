// ============================================================================
// sf_test_dual_drive.ino  —  SF_TelemTest: BOTH drive ESCs telemetry test
// ============================================================================
//
// Reads both drive ESCs' KISS telemetry and forwards tagged ASCII frames over
// USB-Serial AND Bluetooth-SPP (same protocol as SF_Telem):
//
//      $R,...*HH   (right drive)      $L,...*HH   (left drive)
//
// Wire the drive ESC telemetry pads to:
//      RIGHT telem ->  GPIO16  (UART1 RX, board silk "RX2")
//      LEFT  telem ->  GPIO17  (UART2 RX, board silk "TX2")
//      GND         ->  GND     (common ground, required)
//
// Open the SF_TelemTest app, "Dual Drive" screen, connect the COM port.
// ============================================================================

#include <Arduino.h>
#include "BluetoothSerial.h"

#define BT_DEVICE_NAME  "SF_TelemTest"
#define RIGHT_TLM_PIN   16        // RIGHT drive ESC telemetry -> UART1 RX
#define LEFT_TLM_PIN    17        // LEFT  drive ESC telemetry -> UART2 RX
#define ESC_BAUD        115200
#define STALE_MS        600

HardwareSerial  SerialRight(1);   // UART1
HardwareSerial  SerialLeft(2);    // UART2
BluetoothSerial SerialBT;

enum { R = 0, L = 1, N = 2 };
const char SIDE_CHAR[N] = { 'R', 'L' };

struct ESC_Data { uint8_t temp = 0; uint16_t voltage = 0, current = 0, consumption = 0, erpm = 0; bool valid = false; };
ESC_Data      tlm[N];
uint8_t       buf[N][10];
unsigned long lastMs[N] = { 0, 0 }, lastHeartbeat = 0;

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

bool serviceSide(Stream& port, int idx)
{
    if (port.available() > 50) while (port.available()) port.read();
    if (port.available() >= 10)
    {
        port.readBytes(buf[idx], 10);
        if (parseESC(buf[idx], tlm[idx])) { sendFrame(SIDE_CHAR[idx], tlm[idx]); tlm[idx].valid = false; return true; }
    }
    return false;
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    SerialRight.begin(ESC_BAUD, SERIAL_8N1, RIGHT_TLM_PIN, -1, false);
    SerialLeft.begin(ESC_BAUD, SERIAL_8N1, LEFT_TLM_PIN,  -1, false);
    SerialBT.begin(BT_DEVICE_NAME);
    Serial.println("# SF_TelemTest dual-drive online");
    Serial.printf("# RIGHT pin: %d (UART1)   LEFT pin: %d (UART2)\n", RIGHT_TLM_PIN, LEFT_TLM_PIN);
}

void loop()
{
    if (serviceSide(SerialRight, R)) lastMs[R] = millis();
    if (serviceSide(SerialLeft,  L)) lastMs[L] = millis();

    if (millis() - lastHeartbeat > 1000)
    {
        const bool rOk = (millis() - lastMs[R]) < STALE_MS;
        const bool lOk = (millis() - lastMs[L]) < STALE_MS;
        char s[64];
        snprintf(s, sizeof(s), "# R=%s L=%s bt=%s\n",
                 rOk ? "ok" : "ERR", lOk ? "ok" : "ERR", SerialBT.connected() ? "yes" : "no");
        broadcastPrint(s);
        lastHeartbeat = millis();
    }
}
