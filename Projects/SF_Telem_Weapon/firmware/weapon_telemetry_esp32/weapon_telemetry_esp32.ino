// ============================================================================
// weapon_telemetry_esp32.ino  —  SF_Telem_Weapon firmware (SINGLE WEAPON ESC)
// ============================================================================
//
// One ESP32 reads a SINGLE weapon-motor ESC on one UART RX pin and forwards RAW
// KISS telemetry to the host over Bluetooth-SPP (and USB). All engineering
// conversion (volts, amps, RPM, tip speed) is done on the PC in SF_Telem_Weapon
// so the constants (poles, gear ratio, weapon diameter) can be tuned live
// without reflashing.
//
//                +------------------------------+
//   WEAPON ESC --| TLM -> GPIO 13 (UART1)       |---  BT / USB --> PC
//                +------------------------------+
//   (GPIO13 = the "D13" pad on an ESP32 DevKit V1)
//
//   >>> Change WEAPON_TLM_PIN below to move the telemetry wire to another pin. <<<
//
// WIRE FORMAT  (one ASCII line per packet, '\n' terminated):
//
//      $<temp>,<vraw>,<iraw>,<craw>,<erpmraw>*<HH>\n
//
//   $          start-of-frame
//   <temp>     raw KISS temperature  (deg C)
//   <vraw>     raw voltage           (centi-volts)
//   <iraw>     raw current           (centi-amps)
//   <craw>     raw consumption       (mAh)
//   <erpmraw>  raw eRPM / 100
//   *          end-of-payload
//   <HH>       XOR checksum of every char between '$' and '*', 2 hex digits
//   \n         end-of-frame
//
// If the ESC's telemetry wire dies, '$' frames simply stop and the host flags
// "NO SIGNAL". The '#' heartbeat reports whether the link is currently alive.
// ============================================================================

#include <Arduino.h>
#include "BluetoothSerial.h"

// ---- Configuration ---------------------------------------------------------
#define BT_DEVICE_NAME  "SF_Weapon_Telem"
#define WEAPON_TLM_PIN  13      // <-- WEAPON ESC telemetry wire -> UART1 RX = GPIO13 ("D13" on DevKit V1)
#define ESC_BAUD        115200
#define STALE_MS        600     // no valid packet within this -> "ERR" in heartbeat

HardwareSerial  SerialWeapon(1); // UART1
BluetoothSerial SerialBT;

struct ESC_Data
{
    uint8_t  temp        = 0;   // deg C
    uint16_t voltage     = 0;   // raw centi-volts
    uint16_t current     = 0;   // raw centi-amps
    uint16_t consumption = 0;   // mAh
    uint16_t erpm        = 0;   // raw eRPM / 100
    bool     valid       = false;
};

ESC_Data tlmWeapon;
uint8_t  bufWeapon[10];
unsigned long lastWeaponMs = 0;
unsigned long lastHeartbeat = 0;

// ---- KISS CRC8 (validates the ESC -> ESP link) -----------------------------
uint8_t update_crc8(uint8_t crc, uint8_t crc_seed)
{
    uint8_t crc_u = crc ^ crc_seed;
    for (int i = 0; i < 8; i++)
        crc_u = (crc_u & 0x80) ? (0x07 ^ (crc_u << 1)) : (crc_u << 1);
    return crc_u;
}

uint8_t get_crc8(uint8_t* buf, uint8_t size)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < size; i++) crc = update_crc8(buf[i], crc);
    return crc;
}

bool parseESC(uint8_t* buffer, ESC_Data& tlm)
{
    if (get_crc8(buffer, 9) != buffer[9]) return false;
    tlm.temp        = buffer[0];
    tlm.voltage     = (buffer[1] << 8) | buffer[2];
    tlm.current     = (buffer[3] << 8) | buffer[4];
    tlm.consumption = (buffer[5] << 8) | buffer[6];
    tlm.erpm        = (buffer[7] << 8) | buffer[8];
    tlm.valid       = true;
    return true;
}

// ---- Output helpers --------------------------------------------------------
void broadcastPrint(const char* msg)
{
    Serial.print(msg);
    if (SerialBT.connected()) SerialBT.print(msg);
}

void sendFrame(const ESC_Data& d)
{
    char payload[48];
    int n = snprintf(payload, sizeof(payload), "%u,%u,%u,%u,%u",
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

// Read + forward the weapon ESC. Returns true if a fresh valid packet was sent.
bool serviceWeapon()
{
    // Drain backlog so a BT stall can't desync the 10-byte KISS framing.
    if (SerialWeapon.available() > 50)
        while (SerialWeapon.available()) SerialWeapon.read();

    bool sent = false;
    if (SerialWeapon.available() >= 10)
    {
        SerialWeapon.readBytes(bufWeapon, 10);
        if (parseESC(bufWeapon, tlmWeapon))
        {
            sendFrame(tlmWeapon);
            tlmWeapon.valid = false;
            sent = true;
        }
    }
    return sent;
}

// ---- Setup -----------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    delay(1000);

    // One-wire telemetry: RX only (TX pin = -1), no inversion.
    SerialWeapon.begin(ESC_BAUD, SERIAL_8N1, WEAPON_TLM_PIN, -1, false);

    SerialBT.begin(BT_DEVICE_NAME);

    Serial.println("# Telemetry System Online (single weapon ESC)");
    Serial.printf("# Bluetooth: %s\n", BT_DEVICE_NAME);
    Serial.printf("# WEAPON pin: %d\n", WEAPON_TLM_PIN);
}

// ---- Loop ------------------------------------------------------------------
void loop()
{
    if (serviceWeapon()) lastWeaponMs = millis();

    // Heartbeat — '#' comment line; the host logs but ignores it. Reports whether
    // the weapon link is alive so a dead telem wire is obvious at the bench.
    if (millis() - lastHeartbeat > 1000)
    {
        const bool ok = (millis() - lastWeaponMs) < STALE_MS;
        char status[64];
        snprintf(status, sizeof(status), "# weapon=%s bt=%s\n",
                 ok ? "ok" : "ERR",
                 SerialBT.connected() ? "yes" : "no");
        broadcastPrint(status);
        lastHeartbeat = millis();
    }
}
