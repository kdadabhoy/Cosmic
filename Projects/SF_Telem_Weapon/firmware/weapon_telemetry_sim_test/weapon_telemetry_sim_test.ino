// ============================================================================
// weapon_telemetry_sim_test.ino  —  SF_Telem_Weapon BLUETOOTH TEST / SIM
// ============================================================================
//
// NO ESC REQUIRED. This sketch SYNTHESIZES realistic single weapon-motor ESC
// telemetry and streams it in the exact wire format the app expects, over
// Bluetooth-SPP (and USB). Use it to prove the BT link + the whole
// SF_Telem_Weapon pipeline (parse -> plots -> record/replay -> CSV/bin export)
// before any real ESC.
//
// It drives the weapon with repeated spin-up / hold / spin-down cycles so you
// can watch RPM, current, power and tip speed ramp on the charts.
//
//   * Frame format identical to firmware/weapon_telemetry_esp32:
//
//        $<temp>,<vraw>,<iraw>,<craw>,<erpmraw>*<HH>\n
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

#define BT_DEVICE_NAME   "SF_Weapon_Telem"
#define SEND_INTERVAL_MS 25      // ~40 Hz

BluetoothSerial SerialBT;

// Simulated raw telemetry (same fields as the real ESC_Data).
struct SimEsc
{
    uint8_t  temp        = 30;   // deg C
    uint16_t voltage     = 2500; // centi-volts
    uint16_t current     = 0;    // centi-amps
    uint16_t consumption = 0;    // mAh
    uint16_t erpm        = 0;    // eRPM / 100
};

SimEsc sim;
float  mAh = 0.0f;               // running consumption accumulator
unsigned long lastSend = 0;

// ---- Output helpers (identical framing to the real firmware) ---------------
void broadcastPrint(const char* msg)
{
    Serial.print(msg);
    if (SerialBT.connected()) SerialBT.print(msg);
}

void sendFrame(const SimEsc& d)
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

// Build raw values from a 0..1 throttle, with a little noise.
void fillSim(SimEsc& d, float throttle, float dtSec)
{
    throttle = constrain(throttle, 0.0f, 1.0f);

    // eRPM/100: a weapon spins fast — ~0..6000 raw (=> ~0..600k eRPM, big tip speed)
    int erpm = (int)(throttle * 6000.0f) + random(-40, 40);
    d.erpm   = (uint16_t)constrain(erpm, 0, 9000);

    // Current (centi-amps): rises with throttle, spikes on spin-up; ~0..80 A
    int cur  = (int)(throttle * 7000.0f) + random(-80, 80);
    d.current = (uint16_t)constrain(cur, 0, 12000);

    // Voltage (centi-volts): ~25 V nominal, sags under load
    int volt = 2500 - (int)(throttle * 300.0f) + random(-8, 8);
    d.voltage = (uint16_t)constrain(volt, 1800, 2600);

    // Temperature (deg C): idles warm, climbs with sustained load
    int temp = 32 + (int)(throttle * 28.0f) + random(-1, 1);
    d.temp   = (uint8_t)constrain(temp, 0, 120);

    // Consumption (mAh): integrate current over time
    float amps = d.current / 100.0f;
    mAh += amps * (dtSec / 3600.0f) * 1000.0f;
    if (mAh > 60000.0f) mAh = 0.0f; // wrap so it never overflows uint16
    d.consumption = (uint16_t)mAh;
}

// ---- Setup -----------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    delay(500);
    randomSeed(analogRead(0));
    SerialBT.begin(BT_DEVICE_NAME);

    Serial.println("# SIMULATOR online — synthesizing single weapon-ESC telemetry");
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

    // Spin-up / hold / spin-down cycle on a ~12 s period. A sharp ramp up, a
    // held plateau near full throttle, then a coast down — like real matches.
    const float phase = fmodf(t, 12.0f);
    float throttle;
    if      (phase < 2.0f)  throttle = phase / 2.0f;                 // 0 -> 1 spin-up
    else if (phase < 8.0f)  throttle = 0.92f + 0.05f * sinf(t * 4.0f); // held, slight ripple
    else                    throttle = 1.0f - (phase - 8.0f) / 4.0f; // 1 -> 0 spin-down

    fillSim(sim, throttle, dt);
    sendFrame(sim);
}
