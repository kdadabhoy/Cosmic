// viper-fc firmware — Teensy 4.1 HIL node (doc 04 P6).
//
// ============================================================================
// The EXACT flying binary, driven by simulated sensors:
//   ViperSim streams SensorPackets over USB-CDC (COBS+CRC16 framing, the
//   engine's serial/Framing.h); this loop feeds them through the SAME
//   viperfc::FlightComputer that flies in SITL and answers with
//   ActuatorPackets echoing the sim timestamp (latency on screen, plan §1).
//
// Real-sensor flight swaps ReadSensors for driver code behind TeensyHal —
// the FC itself does not change. That is the whole point of the HAL.
// ============================================================================

#include <Arduino.h>

#include "viperfc/ViperFc.h"
#include "viperfc/wire/HilProtocol.h"
#include "serial/Framing.h"      // Cosmic engine — freestanding by contract

using namespace viperfc;

namespace
{
	FlightComputer fc;

	// RX framing accumulator (frames end at 0x00).
	constexpr size_t kRxCap = 512;
	uint8_t rxBuf[kRxCap];
	size_t  rxLen = 0;

	uint8_t payload[kRxCap];
	uint8_t txBuf[Cosmic::Framing::MaxFrameSize(sizeof(wire::SensorPacket))];

	uint64_t lastSensorEcho = 0;
	uint32_t lastStatusMs = 0;

	void SendFrame(const uint8_t* data, size_t len)
	{
		const size_t n = Cosmic::Framing::EncodeFrame(data, len, txBuf, sizeof(txBuf));
		if (n > 0)
			Serial.write(txBuf, n);
	}

	void HandlePayload(const uint8_t* p, size_t len)
	{
		wire::SensorPacket sp;
		wire::HeartbeatPacket hb;
		wire::CommandPacket cp;

		if (wire::DecodePacket(p, len, wire::SensorPacketId, sp))
		{
			SensorFrame f;
			f.t_us      = sp.t_us;
			f.gyro_rads = { sp.gyro[0],  sp.gyro[1],  sp.gyro[2] };
			f.accel_mss = { sp.accel[0], sp.accel[1], sp.accel[2] };
			f.mag_uT    = { sp.mag[0],   sp.mag[1],   sp.mag[2] };
			f.baro_pa     = sp.baro_pa;
			f.airspeed_pa = sp.airspeed_pa;
			f.gps.valid   = sp.gps_valid != 0;
			f.gps.posNed  = { sp.gps_pos[0], sp.gps_pos[1], sp.gps_pos[2] };
			f.gps.velNed  = { sp.gps_vel[0], sp.gps_vel[1], sp.gps_vel[2] };
			f.gps.sats    = sp.gps_sats;
			f.vbat_V = sp.vbat_V;
			f.ibat_A = sp.ibat_A;

			// dt from consecutive sim timestamps (sim clock is the truth here).
			const float dt = lastSensorEcho > 0 && sp.t_us > lastSensorEcho
				? (sp.t_us - lastSensorEcho) * 1e-6f
				: 1.0f / fc.Params().fc_rate_hz;
			lastSensorEcho = sp.t_us;

			ActuatorFrame out;
			fc.Step(f, out, dt);

			wire::ActuatorPacket ap;
			ap.t_echo_us = sp.t_us;
			for (int i = 0; i < 4; ++i) { ap.motor[i] = out.motor[i]; ap.servo[i] = out.servo[i]; }
			SendFrame(reinterpret_cast<const uint8_t*>(&ap), sizeof(ap));
		}
		else if (wire::DecodePacket(p, len, wire::HeartbeatPacketId, hb))
		{
			fc.Heartbeat();
		}
		else if (wire::DecodePacket(p, len, wire::CommandPacketId, cp))
		{
			if (cp.arm == 1)      fc.Arm(true);
			else if (cp.arm == 2) fc.Arm(false);
			if (cp.requestMode >= 0)
				fc.RequestMode(static_cast<FlightMode>(cp.requestMode));
			FlightComputer::PilotInput pi;
			pi.velCmdNed  = { cp.velCmd[0], cp.velCmd[1], cp.velCmd[2] };
			pi.yawRateCmd = cp.yawRateCmd;
			fc.SetPilotInput(pi);
			fc.SetRoi({ cp.roi[0], cp.roi[1], cp.roi[2] });
		}
	}
}

void setup()
{
	Serial.begin(2000000);   // USB-CDC ignores the number but ≥2 Mbaud by contract
	fc.Reset();
}

void loop()
{
	// Drain USB, split on 0x00 delimiters, decode COBS+CRC frames.
	while (Serial.available() > 0)
	{
		const uint8_t b = static_cast<uint8_t>(Serial.read());
		if (b == 0x00)
		{
			if (rxLen > 0)
			{
				const size_t n = Cosmic::Framing::DecodeFrame(rxBuf, rxLen, payload, sizeof(payload));
				if (n > 0)
					HandlePayload(payload, n);
			}
			rxLen = 0;
		}
		else if (rxLen < kRxCap)
		{
			rxBuf[rxLen++] = b;
		}
		else
		{
			rxLen = 0;   // overflow — resync at the next delimiter
		}
	}

	// Low-rate status downlink (10 Hz).
	const uint32_t now = millis();
	if (now - lastStatusMs >= 100)
	{
		lastStatusMs = now;
		const TelemetrySnapshot& t = fc.Telemetry();
		wire::StatusPacket st;
		st.mode  = static_cast<int32_t>(t.mode);
		st.phase = static_cast<int32_t>(t.phase);
		st.blend = t.blend;
		st.failsafeFlags  = t.failsafeFlags;
		st.hoverElapsed_s = t.hoverElapsed_s;
		st.energyUsed_wh  = t.energyUsed_wh;
		st.armed = t.armed ? 1 : 0;
		SendFrame(reinterpret_cast<const uint8_t*>(&st), sizeof(st));
	}
}
