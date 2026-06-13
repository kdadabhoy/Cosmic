// ============================================================================
// esc_telemetry_esp32.ino  —  Shear_Force_TelemApp firmware
// ============================================================================
//
// Forwards RAW KISS ESC telemetry to the host over Bluetooth-SPP (and USB).
// All engineering conversion (volts, amps, RPM, speed) is done on the PC in
// Shear_Force_TelemApp so the constants (motor poles, gear ratio, wheel size)
// can be tuned live without reflashing.
//
// WIRE FORMAT  (one ASCII line per packet, '\n' terminated):
//
//      $<id>,<temp>,<vraw>,<iraw>,<craw>,<erpmraw>*<HH>\n
//
//   $          start-of-frame
//   <id>       1-based ESC index (this sketch = ESC_ID)
//   <temp>     raw KISS temperature  (deg C)
//   <vraw>     raw voltage           (centi-volts)
//   <iraw>     raw current           (centi-amps)
//   <craw>     raw consumption       (mAh)
//   <erpmraw>  raw eRPM / 100
//   *          end-of-payload
//   <HH>       XOR checksum of every char between '$' and '*', 2 hex digits
//   \n         end-of-frame
//
// The PC validates the framing + checksum and ignores anything else (so the
// '#' heartbeat lines below are harmless).
//
// MULTI-ESC: today this flashes one board reading one TLM pin. For 3 ESCs,
// either flash three boards with ESC_ID = 1/2/3, or extend this sketch with
// three HardwareSerial inputs and call sendFrame() once per ESC — the host
// already keys incoming packets by <id>.
// ============================================================================

#include <Arduino.h>
#include "BluetoothSerial.h"

// ---- Configuration ---------------------------------------------------------
#define BT_DEVICE_NAME "ESC_Telemetry_Test2"
#define TLM_PIN        18      // ESC telemetry (one-wire) input
#define ESC_ID         1       // 1-based id sent on the wire
#define ESC_BAUD       115200

HardwareSerial SerialESC(1);
BluetoothSerial SerialBT;

struct ESC_Data
{
    uint8_t  temp        = 0;
    uint16_t voltage     = 0;   // raw centi-volts
    uint16_t current     = 0;   // raw centi-amps
    uint16_t consumption = 0;   // mAh
    uint16_t erpm        = 0;   // raw eRPM / 100
    bool     valid       = false;
};

ESC_Data tlm1;
uint8_t  buf1[10];
unsigned long lastTelemetryTime = 0;
unsigned long lastHeartbeat     = 0;

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

void parseESC(uint8_t* buffer, ESC_Data& tlm)
{
    if (get_crc8(buffer, 9) == buffer[9])
    {
        tlm.temp        = buffer[0];
        tlm.voltage     = (buffer[1] << 8) | buffer[2];
        tlm.current     = (buffer[3] << 8) | buffer[4];
        tlm.consumption = (buffer[5] << 8) | buffer[6];
        tlm.erpm        = (buffer[7] << 8) | buffer[8];
        tlm.valid       = true;
    }
}

// ---- Output helpers --------------------------------------------------------
void broadcastPrint(const char* msg)
{
    Serial.print(msg);
    if (SerialBT.connected()) SerialBT.print(msg);
}

// Build, checksum, and emit one framed RAW packet.
void sendFrame(uint8_t id, const ESC_Data& d)
{
    char payload[48];
    int n = snprintf(payload, sizeof(payload), "%u,%u,%u,%u,%u,%u",
                     (unsigned)id,
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

// ---- Setup -----------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    delay(1000);

    SerialESC.begin(ESC_BAUD, SERIAL_8N1, TLM_PIN, -1, false);
    SerialBT.begin(BT_DEVICE_NAME);

    Serial.println("# Telemetry System Online");
    Serial.printf("# Bluetooth: %s\n", BT_DEVICE_NAME);
    Serial.printf("# ESC_ID: %d  Pin: %d\n", ESC_ID, TLM_PIN);
}

// ---- Loop ------------------------------------------------------------------
void loop()
{
    // 1. Drain backlog so Bluetooth stalls don't desync the 10-byte framing.
    if (SerialESC.available() > 50)
        while (SerialESC.available()) SerialESC.read();

    // 2. Read one 10-byte KISS packet.
    if (SerialESC.available() >= 10)
    {
        SerialESC.readBytes(buf1, 10);
        parseESC(buf1, tlm1);
    }

    // 3. Forward RAW values (PC does all unit conversion).
    if (tlm1.valid)
    {
        sendFrame(ESC_ID, tlm1);
        tlm1.valid        = false;
        lastTelemetryTime = millis();
    }

    // 4. Heartbeat — '#' comment line; the host logs but ignores it.
    if (millis() - lastHeartbeat > 1000)
    {
        char status[64];
        snprintf(status, sizeof(status), "# wait esc=%d bt=%s\n",
                 ESC_ID, SerialBT.connected() ? "yes" : "no");
        broadcastPrint(status);
        lastHeartbeat = millis();
    }
}
