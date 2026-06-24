// ============================================================================
// sf_test_sniffer.ino  —  SF_TelemTest: raw telemetry SNIFFER
// ============================================================================
//
// Goal: prove whether an ESC is emitting ANYTHING on its telemetry wire — valid
// KISS or pure garbage. It does NOT decode; it just counts raw bytes per wire
// and reports them, so you can tell "is telem even being sent?".
//
// Watches all three telemetry wires and, every ~150 ms, emits one line per wire
// over USB-Serial AND Bluetooth-SPP:
//
//      SNIFF,<tag>,<bytesThisInterval>,<totalBytes>,<hexSample>\n
//      tag = R / L / W ;  hexSample = first few bytes seen this interval (or '-')
//
// Wire the ESC telemetry pads to:
//      RIGHT  -> GPIO16 (UART1)   LEFT -> GPIO17 (UART2)   WEAPON -> GPIO13 (UART0 RX remapped)
//      GND    -> GND    (common ground, required)
//
// Open the SF_TelemTest app, "Sniffer" screen, connect the COM port.
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

unsigned long total[N]    = { 0, 0, 0 };
unsigned long interval[N] = { 0, 0, 0 };
char          sample[N][24];      // up to 8 bytes -> 16 hex chars, no spaces
int           sampleLen[N] = { 0, 0, 0 };
unsigned long lastReport = 0;

void broadcastPrint(const char* msg)
{
    Serial.print(msg);
    if (SerialBT.connected()) SerialBT.print(msg);
}

// Drain a wire: count every byte and capture a short hex sample of the interval.
void drain(Stream& port, int i)
{
    while (port.available())
    {
        int b = port.read();
        if (b < 0) break;
        total[i]++;
        interval[i]++;
        if (sampleLen[i] < 16)
        {
            char h[3];
            snprintf(h, sizeof(h), "%02X", (uint8_t)b);
            sample[i][sampleLen[i]++] = h[0];
            sample[i][sampleLen[i]++] = h[1];
            sample[i][sampleLen[i]]   = 0;
        }
    }
}

void setup()
{
    // Weapon on UART0: RX remapped to GPIO13, TX kept on GPIO1 for USB output.
    Serial.begin(115200, SERIAL_8N1, WEAPON_TLM_PIN, 1);
    delay(1000);
    SerialRight.begin(ESC_BAUD, SERIAL_8N1, RIGHT_TLM_PIN, -1, false);
    SerialLeft.begin(ESC_BAUD, SERIAL_8N1, LEFT_TLM_PIN,  -1, false);
    SerialBT.begin(BT_DEVICE_NAME);
    Serial.println("# SF_TelemTest sniffer online");
    Serial.printf("# Sniffing R=%d (UART1) L=%d (UART2) W=%d (UART0)\n",
                  RIGHT_TLM_PIN, LEFT_TLM_PIN, WEAPON_TLM_PIN);
}

void loop()
{
    drain(SerialRight, R);
    drain(SerialLeft,  L);
    drain(Serial,      W);

    if (millis() - lastReport >= REPORT_MS)
    {
        for (int i = 0; i < N; i++)
        {
            char line[64];
            snprintf(line, sizeof(line), "SNIFF,%c,%lu,%lu,%s\n",
                     TAG[i], interval[i], total[i], sampleLen[i] ? sample[i] : "-");
            broadcastPrint(line);
            interval[i]  = 0;
            sampleLen[i] = 0;
            sample[i][0] = 0;
        }
        lastReport = millis();
    }
}
