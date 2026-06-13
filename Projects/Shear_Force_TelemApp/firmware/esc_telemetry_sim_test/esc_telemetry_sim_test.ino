// ============================================================================
// esc_telemetry_sim_test.ino  —  Shear_Force_TelemApp BLUETOOTH TEST / SIM
// ============================================================================
//
// NO ESC REQUIRED. This sketch SYNTHESIZES realistic dual-ESC telemetry and
// streams it in the exact wire format the app expects, over Bluetooth-SPP (and
// USB). Use it to prove the BT link + the whole Shear_Force_TelemApp pipeline
// (parse -> dual plots -> robot visual -> record/replay) before any real ESC.
//
// It drives the two sides with a slowly-weaving throttle so Right and Left
// diverge — you should see the robot in the app drive forward and turn.
//
//   * Same BT_DEVICE_NAME as the real sketch -> reuses your existing pairing/COM.
//   * Frame format identical to firmware/esc_telemetry_esp32.ino:
//
//        $<S>,<temp>,<vraw>,<iraw>,<craw>,<erpmraw>*<HH>\n     (S = 'R' or 'L')
//
//   * Values are RAW KISS units (centi-volts, centi-amps, mAh, eRPM/100) — the
//     PC applies the conversion constants, exactly like real hardware.
//
// NOTE: keep this in its own sketch folder. The Arduino IDE compiles every .ino
// in a folder together, so it must not share a folder with the real sketch
// (both define setup()/loop()).
// ============================================================================

#include <Arduino.h>
#include "BluetoothSerial.h"
#include <math.h>

#define BT_DEVICE_NAME "ESC_Telemetry_Test2"
#define SEND_INTERVAL_MS 25      // ~40 Hz per side

BluetoothSerial SerialBT;

// Simulated per-side raw telemetry (same fields as the real ESC_Data).
struct SimEsc
{
    uint8_t  temp        = 30;
    uint16_t voltage     = 2500; // centi-volts
    uint16_t current     = 0;    // centi-amps
    uint16_t consumption = 0;    // mAh
    uint16_t erpm        = 0;    // eRPM / 100
};

SimEsc simR, simL;
float  mAhR = 0.0f, mAhL = 0.0f;   // running consumption accumulators
unsigned long lastSend = 0;

// ---- Output helpers (identical framing to the real firmware) ---------------
void broadcastPrint(const char* msg)
{
    Serial.print(msg);
    if (SerialBT.connected()) SerialBT.print(msg);
}

void sendFrame(char side, const SimEsc& d)
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

// Build one side's raw values from a 0..1 throttle, with a little noise.
void fillSide(SimEsc& d, float throttle, float& mAhAccum, float dtSec)
{
    throttle = constrain(throttle, 0.0f, 1.0f);

    // eRPM/100: ~0..2200 raw  (=> roughly 0..18 mph after host decode)
    int erpm = (int)(throttle * 2000.0f) + random(-30, 30);
    d.erpm   = (uint16_t)constrain(erpm, 0, 2400);

    // Current (centi-amps): rises with throttle, ~0..50 A
    int cur  = (int)(throttle * 4500.0f) + random(-60, 60);
    d.current = (uint16_t)constrain(cur, 0, 6000);

    // Voltage (centi-volts): ~25 V nominal, sags under load
    int volt = 2500 - (int)(throttle * 220.0f) + random(-8, 8);
    d.voltage = (uint16_t)constrain(volt, 2000, 2600);

    // Temperature (deg C): idles warm, climbs with load
    int temp = 32 + (int)(throttle * 18.0f) + random(-1, 1);
    d.temp   = (uint8_t)constrain(temp, 0, 120);

    // Consumption (mAh): integrate current over time
    float amps = d.current / 100.0f;
    mAhAccum  += amps * (dtSec / 3600.0f) * 1000.0f;
    if (mAhAccum > 60000.0f) mAhAccum = 0.0f; // wrap so it never overflows uint16
    d.consumption = (uint16_t)mAhAccum;
}

// ---- Setup -----------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    delay(500);
    randomSeed(analogRead(0));
    SerialBT.begin(BT_DEVICE_NAME);

    Serial.println("# SIMULATOR online — synthesizing dual-ESC telemetry");
    Serial.printf("# Bluetooth: %s  (no ESC needed)\n", BT_DEVICE_NAME);
}

// ---- Loop ------------------------------------------------------------------
void loop()
{
    const unsigned long now = millis();
    if (now - lastSend < SEND_INTERVAL_MS) return;
    const float dt = (now - lastSend) / 1000.0f;
    lastSend = now;

    const float t = now / 1000.0f;

    // Forward throttle that breathes up and down, plus a slow steering term
    // that pushes Right and Left apart so the robot weaves.
    const float base  = 0.5f + 0.4f * sinf(t * 0.8f);   // 0.1 .. 0.9
    const float steer = sinf(t * 0.35f);                // -1 .. 1

    fillSide(simR, base + 0.35f * steer, mAhR, dt);
    fillSide(simL, base - 0.35f * steer, mAhL, dt);

    sendFrame('R', simR);
    sendFrame('L', simL);
}
