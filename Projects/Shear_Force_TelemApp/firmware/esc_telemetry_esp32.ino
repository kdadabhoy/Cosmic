// ============================================================================
// esc_telemetry_esp32.ino  —  Shear_Force_TelemApp firmware (DUAL DRIVE ESC)
// ============================================================================
//
// One ESP32 reads TWO drive ESCs (RIGHT + LEFT) on separate UART RX pins and
// forwards RAW KISS telemetry to the host over Bluetooth-SPP (and USB). All
// engineering conversion (volts, amps, RPM, speed) is done on the PC in
// Shear_Force_TelemApp so the constants (poles, gear ratio, wheel size) can be
// tuned live without reflashing.
//
//                +-----------------------+
//   RIGHT ESC ---| TLM -> GPIO 18 (UART1) |
//    LEFT ESC ---| TLM -> GPIO 16 (UART2) |---  BT/USB --> PC
//                +-----------------------+
//
// WIRE FORMAT  (one ASCII line per packet, '\n' terminated):
//
//      $<S>,<temp>,<vraw>,<iraw>,<craw>,<erpmraw>*<HH>\n
//
//   $          start-of-frame
//   <S>        side tag: 'R' (right) or 'L' (left)  <-- routes to the right motor
//   <temp>     raw KISS temperature  (deg C)
//   <vraw>     raw voltage           (centi-volts)
//   <iraw>     raw current           (centi-amps)
//   <craw>     raw consumption       (mAh)
//   <erpmraw>  raw eRPM / 100
//   *          end-of-payload
//   <HH>       XOR checksum of every char between '$' and '*', 2 hex digits
//   \n         end-of-frame
//
// FAULT TOLERANCE: the two sides are read independently. If one ESC's telem
// wire dies, only that side stops sending '$' frames; the other keeps
// streaming and the host flags the dead side. The '#' heartbeat reports which
// sides are currently alive.
// ============================================================================

#include <Arduino.h>
#include "BluetoothSerial.h"

// ---- Configuration ---------------------------------------------------------
#define BT_DEVICE_NAME  "ESC_Telemetry_Test2"
#define RIGHT_TLM_PIN   18      // RIGHT ESC telemetry wire -> UART1 RX
#define LEFT_TLM_PIN    16      // LEFT  ESC telemetry wire -> UART2 RX
#define ESC_BAUD        115200
#define SIDE_STALE_MS   600     // no valid packet within this -> "ERR" in heartbeat

HardwareSerial  SerialRight(1); // UART1
HardwareSerial  SerialLeft(2);  // UART2
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

ESC_Data tlmRight, tlmLeft;
uint8_t  bufRight[10], bufLeft[10];
unsigned long lastRightMs = 0;
unsigned long lastLeftMs  = 0;
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
bool serviceSide(HardwareSerial& port, uint8_t* buf, ESC_Data& tlm, char side)
{
    // Drain backlog so a BT stall can't desync the 10-byte KISS framing.
    if (port.available() > 50)
        while (port.available()) port.read();

    bool sent = false;
    if (port.available() >= 10)
    {
        port.readBytes(buf, 10);
        if (parseESC(buf, tlm))
        {
            sendFrame(side, tlm);
            tlm.valid = false;
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
    SerialRight.begin(ESC_BAUD, SERIAL_8N1, RIGHT_TLM_PIN, -1, false);
    SerialLeft.begin(ESC_BAUD, SERIAL_8N1, LEFT_TLM_PIN,  -1, false);

    SerialBT.begin(BT_DEVICE_NAME);

    Serial.println("# Telemetry System Online (dual drive ESC)");
    Serial.printf("# Bluetooth: %s\n", BT_DEVICE_NAME);
    Serial.printf("# RIGHT pin: %d   LEFT pin: %d\n", RIGHT_TLM_PIN, LEFT_TLM_PIN);
}

// ---- Loop ------------------------------------------------------------------
void loop()
{
    if (serviceSide(SerialRight, bufRight, tlmRight, 'R')) lastRightMs = millis();
    if (serviceSide(SerialLeft,  bufLeft,  tlmLeft,  'L')) lastLeftMs  = millis();

    // Heartbeat — '#' comment line; the host logs but ignores it. Reports which
    // sides are alive so a dead telem wire is obvious at the bench.
    if (millis() - lastHeartbeat > 1000)
    {
        const bool rOk = (millis() - lastRightMs) < SIDE_STALE_MS;
        const bool lOk = (millis() - lastLeftMs)  < SIDE_STALE_MS;
        char status[64];
        snprintf(status, sizeof(status), "# R=%s L=%s bt=%s\n",
                 rOk ? "ok" : "ERR",
                 lOk ? "ok" : "ERR",
                 SerialBT.connected() ? "yes" : "no");
        broadcastPrint(status);
        lastHeartbeat = millis();
    }
}
