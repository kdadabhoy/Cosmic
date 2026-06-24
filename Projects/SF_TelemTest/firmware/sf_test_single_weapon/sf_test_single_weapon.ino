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
ESC_Data      tlm;
uint8_t       buf[10];
unsigned long lastMs = 0, lastHeartbeat = 0;

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

bool serviceSide(Stream& port)
{
    if (port.available() > 50) while (port.available()) port.read();
    if (port.available() >= 10)
    {
        port.readBytes(buf, 10);
        if (parseESC(buf, tlm)) { sendFrame('W', tlm); tlm.valid = false; return true; }
    }
    return false;
}

void setup()
{
    // UART0 / Serial: RX remapped to the weapon pin, TX kept on GPIO1 for USB.
    Serial.begin(115200, SERIAL_8N1, WEAPON_TLM_PIN, 1);
    delay(1000);
    SerialBT.begin(BT_DEVICE_NAME);
    Serial.println("# SF_TelemTest single-weapon online");
    Serial.printf("# WEAPON telem pin: %d (UART0 RX remapped)\n", WEAPON_TLM_PIN);
}

void loop()
{
    if (serviceSide(Serial)) lastMs = millis();

    if (millis() - lastHeartbeat > 1000)
    {
        const bool ok = (millis() - lastMs) < STALE_MS;
        char s[48];
        snprintf(s, sizeof(s), "# W=%s bt=%s\n", ok ? "ok" : "ERR", SerialBT.connected() ? "yes" : "no");
        broadcastPrint(s);
        lastHeartbeat = millis();
    }
}
