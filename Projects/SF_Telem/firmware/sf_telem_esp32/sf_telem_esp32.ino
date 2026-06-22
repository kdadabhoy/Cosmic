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

ESC_Data      tlm[N];
uint8_t       buf[N][10];
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

// Read + forward one side. Returns true if a fresh valid packet was sent.
bool serviceSide(Stream& port, int idx)
{
    // Drain backlog so a BT stall can't desync the 10-byte KISS framing.
    if (port.available() > 50)
        while (port.available()) port.read();

    bool sent = false;
    if (port.available() >= 10)
    {
        port.readBytes(buf[idx], 10);
        if (parseESC(buf[idx], tlm[idx]))
        {
            sendFrame(SIDE_CHAR[idx], tlm[idx]);
            tlm[idx].valid = false;
            sent = true;
        }
    }
    return sent;
}

// ---- Setup -----------------------------------------------------------------
void setup()
{
    // UART0 / Serial: RX remapped to the WEAPON pin, TX kept on GPIO1 for USB.
    Serial.begin(115200, SERIAL_8N1, WEAPON_TLM_PIN, 1);
    delay(1000);

    // One-wire telemetry on the two drive UARTs (RX only, TX pin = -1).
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
    if (serviceSide(SerialRight, R)) lastMs[R] = millis();
    if (serviceSide(SerialLeft,  L)) lastMs[L] = millis();
    if (serviceSide(Serial,      W)) lastMs[W] = millis();

    // Heartbeat — '#' comment line; the host logs but ignores it. Shows which
    // sides are alive so a dead telem wire is obvious at the bench.
    if (millis() - lastHeartbeat > 1000)
    {
        const bool rOk = (millis() - lastMs[R]) < SIDE_STALE_MS;
        const bool lOk = (millis() - lastMs[L]) < SIDE_STALE_MS;
        const bool wOk = (millis() - lastMs[W]) < SIDE_STALE_MS;
        char status[80];
        snprintf(status, sizeof(status), "# R=%s L=%s W=%s bt=%s\n",
                 rOk ? "ok" : "ERR", lOk ? "ok" : "ERR", wOk ? "ok" : "ERR",
                 SerialBT.connected() ? "yes" : "no");
        broadcastPrint(status);
        lastHeartbeat = millis();
    }
}
