// ============================================================================
// weapon_tlm_sniffer.ino  —  SF_Telem_Weapon RAW TELEMETRY SNIFFER (diagnostic)
// ============================================================================
//
// Dumps EVERY byte the ESP32 receives on the telemetry pin — valid KISS frame
// or not — as a hex + ASCII dump, with a bytes-per-second counter once a second.
// It does NO checksum and NO parsing, so garbage/partial/non-KISS data is shown
// too. Use it to figure out why the real sketch reports "weapon=ERR".
//
// Output goes to BOTH USB Serial (open the Arduino Serial Monitor @ 115200) and
// Bluetooth-SPP (so it also shows in the app's Serial Link raw monitor).
//
// HOW TO READ THE RESULT
// ----------------------
//   "0 bytes/s"  every second   -> nothing is reaching the pin:
//                                  telemetry not enabled in AM32, ESC not
//                                  powered from the main battery, wrong pad,
//                                  or no common ground.
//   bytes arriving               -> the wire is good. A healthy KISS stream is
//                                  repeating 10-byte frames. If it looks like
//                                  constant random hex, it's a baud / inverted-
//                                  logic / wrong-protocol issue (try the alt
//                                  bauds below, or check logic level).
//
//   >>> Change WEAPON_TLM_PIN to move the wire. Change TLM_BAUD if you suspect
//       a baud mismatch (AM32/KISS telemetry is 115200). <<<
// ============================================================================

#include <Arduino.h>
#include "BluetoothSerial.h"

// ---- Configuration ---------------------------------------------------------
#define BT_DEVICE_NAME  "SF_Weapon_Telem"
#define WEAPON_TLM_PIN  13       // telemetry wire -> UART1 RX (GPIO13 / "D13")
#define TLM_BAUD        115200   // AM32/KISS telemetry baud (try 19200/38400/57600 if garbage)
#define TLM_INVERT      false    // set true to test inverted logic
#define REPORT_MS       1000     // bytes/sec summary interval

HardwareSerial  SerialTLM(1);    // UART1
BluetoothSerial SerialBT;

unsigned long totalBytes  = 0;
unsigned long windowBytes = 0;
unsigned long lastReport  = 0;

uint8_t rowBuf[16];
uint8_t rowLen = 0;

// ---- Output to USB + Bluetooth --------------------------------------------
void out(const char* s)
{
    Serial.print(s);
    if (SerialBT.connected()) SerialBT.print(s);
}

// ---- Emit one hexdump row: "XX XX .. | ascii" -----------------------------
void flushRow()
{
    if (rowLen == 0) return;

    char line[96];
    int p = 0;
    for (uint8_t i = 0; i < 16; i++)
    {
        if (i < rowLen) p += snprintf(line + p, sizeof(line) - p, "%02X ", rowBuf[i]);
        else            p += snprintf(line + p, sizeof(line) - p, "   ");
    }
    p += snprintf(line + p, sizeof(line) - p, "| ");
    for (uint8_t i = 0; i < rowLen; i++)
    {
        char c = (rowBuf[i] >= 32 && rowBuf[i] < 127) ? (char)rowBuf[i] : '.';
        p += snprintf(line + p, sizeof(line) - p, "%c", c);
    }
    snprintf(line + p, sizeof(line) - p, "\n");

    out(line);
    rowLen = 0;
}

// ---- Setup -----------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    delay(1000);

    // One-wire telemetry: RX only (TX pin = -1).
    SerialTLM.begin(TLM_BAUD, SERIAL_8N1, WEAPON_TLM_PIN, -1, TLM_INVERT);
    SerialBT.begin(BT_DEVICE_NAME);

    out("# RAW TLM SNIFFER online\n");
    char b[80];
    snprintf(b, sizeof(b), "# pin=%d  baud=%d  invert=%d  (dumping all RX bytes)\n",
             WEAPON_TLM_PIN, TLM_BAUD, (int)TLM_INVERT);
    out(b);
    lastReport = millis();
}

// ---- Loop ------------------------------------------------------------------
void loop()
{
    // Drain everything available and hex-dump it.
    while (SerialTLM.available())
    {
        rowBuf[rowLen++] = (uint8_t)SerialTLM.read();
        totalBytes++;
        windowBytes++;
        if (rowLen == 16) flushRow();
    }

    // Once a second: flush a partial row, then print the throughput summary.
    if (millis() - lastReport >= REPORT_MS)
    {
        flushRow();
        char b[80];
        snprintf(b, sizeof(b), "# --- %lu bytes/s (total %lu) ---\n",
                 windowBytes, totalBytes);
        out(b);
        windowBytes = 0;
        lastReport  = millis();
    }
}
