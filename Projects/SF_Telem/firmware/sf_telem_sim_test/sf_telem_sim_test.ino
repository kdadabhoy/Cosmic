// ============================================================================
// sf_telem_sim_test.ino  —  SF_Telem BLUETOOTH TEST / SIMULATOR (no ESC needed)
// ============================================================================
//
// SYNTHESIZES telemetry for all three ESCs (2 drive + 1 weapon) and streams it
// in the exact wire format SF_Telem expects, over Bluetooth-SPP (and USB). Use
// it to prove the BT link + the whole pipeline (parse -> data boxes -> plots ->
// record/replay -> CSV) before any real ESC.
//
//   * Drive Right/Left weave (slow steering term) so they diverge.
//   * Weapon runs spin-up / hold / spin-down cycles.
//   * Frame format identical to sf_telem_esp32:
//        $<S>,<temp>,<vraw>,<iraw>,<craw>,<erpmraw>*<HH>\n     (S = 'R','L','W')
//   * Values are RAW KISS units; the PC applies the conversion constants.
//
// TIP: to exercise the "robust to missing ESCs" behaviour, set any of
// SEND_RIGHT / SEND_LEFT / SEND_WEAPON to 0 and that side simply won't stream.
// ============================================================================

#include <Arduino.h>
#include "BluetoothSerial.h"
#include <math.h>

#define BT_DEVICE_NAME   "SF_Telem"
#define SEND_INTERVAL_MS 25      // ~40 Hz per side

#define SEND_RIGHT   1
#define SEND_LEFT    1
#define SEND_WEAPON  1

BluetoothSerial SerialBT;

struct SimEsc
{
    uint8_t  temp        = 30;
    uint16_t voltage     = 2500; // centi-volts
    uint16_t current     = 0;    // centi-amps
    uint16_t consumption = 0;    // mAh
    uint16_t erpm        = 0;    // eRPM / 100
};

SimEsc simR, simL, simW;
float  mAhR = 0, mAhL = 0, mAhW = 0;
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
                     (unsigned)d.temp, (unsigned)d.voltage, (unsigned)d.current,
                     (unsigned)d.consumption, (unsigned)d.erpm);
    if (n <= 0) return;

    uint8_t crc = 0;
    for (int i = 0; i < n; i++) crc ^= (uint8_t)payload[i];

    char frame[64];
    snprintf(frame, sizeof(frame), "$%s*%02X\n", payload, crc);
    broadcastPrint(frame);
}

// Drive side from a 0..1 throttle (eRPM ~0..2000 raw, current ~0..45 A).
void fillDrive(SimEsc& d, float throttle, float& mAhAccum, float dt)
{
    throttle = constrain(throttle, 0.0f, 1.0f);
    d.erpm    = (uint16_t)constrain((int)(throttle * 2000.0f) + random(-30, 30), 0, 2400);
    d.current = (uint16_t)constrain((int)(throttle * 4500.0f) + random(-60, 60), 0, 6000);
    d.voltage = (uint16_t)constrain(2500 - (int)(throttle * 220.0f) + random(-8, 8), 2000, 2600);
    d.temp    = (uint8_t)constrain(32 + (int)(throttle * 18.0f) + random(-1, 1), 0, 120);
    mAhAccum += (d.current / 100.0f) * (dt / 3600.0f) * 1000.0f;
    if (mAhAccum > 60000.0f) mAhAccum = 0.0f;
    d.consumption = (uint16_t)mAhAccum;
}

// Weapon side from a 0..1 throttle (spins much faster: eRPM ~0..6000 raw).
void fillWeapon(SimEsc& d, float throttle, float& mAhAccum, float dt)
{
    throttle = constrain(throttle, 0.0f, 1.0f);
    d.erpm    = (uint16_t)constrain((int)(throttle * 6000.0f) + random(-40, 40), 0, 9000);
    d.current = (uint16_t)constrain((int)(throttle * 7000.0f) + random(-80, 80), 0, 12000);
    d.voltage = (uint16_t)constrain(2500 - (int)(throttle * 300.0f) + random(-8, 8), 1800, 2600);
    d.temp    = (uint8_t)constrain(32 + (int)(throttle * 28.0f) + random(-1, 1), 0, 120);
    mAhAccum += (d.current / 100.0f) * (dt / 3600.0f) * 1000.0f;
    if (mAhAccum > 60000.0f) mAhAccum = 0.0f;
    d.consumption = (uint16_t)mAhAccum;
}

// ---- Setup -----------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    delay(500);
    randomSeed(analogRead(0));
    SerialBT.begin(BT_DEVICE_NAME);

    Serial.println("# SIMULATOR online — synthesizing 2 drive + 1 weapon ESC");
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

    // --- Drive: forward throttle that breathes + a slow steering term ---
    const float base  = 0.5f + 0.4f * sinf(t * 0.8f);
    const float steer = sinf(t * 0.35f);
#if SEND_RIGHT
    fillDrive(simR, base + 0.35f * steer, mAhR, dt); sendFrame('R', simR);
#endif
#if SEND_LEFT
    fillDrive(simL, base - 0.35f * steer, mAhL, dt); sendFrame('L', simL);
#endif

    // --- Weapon: spin-up / hold / spin-down on a ~12 s period ---
#if SEND_WEAPON
    const float phase = fmodf(t, 12.0f);
    float wt;
    if      (phase < 2.0f) wt = phase / 2.0f;                  // spin-up
    else if (phase < 8.0f) wt = 0.92f + 0.05f * sinf(t * 4.0f);// held
    else                   wt = 1.0f - (phase - 8.0f) / 4.0f;  // spin-down
    fillWeapon(simW, wt, mAhW, dt); sendFrame('W', simW);
#endif
}
