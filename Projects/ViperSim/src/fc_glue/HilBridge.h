#pragma once

// HilBridge.h
//
// ============================================================================
// HIL backend (plan P6): sim physics on the PC, the REAL Teensy 4.1 runs
// viper-fc (viper-fc/firmware/), frames over USB-serial — E5 COBS+CRC framing,
// ≥2 Mbaud, sim-clock timestamps, LATENCY DISPLAYED (all doc 04 §1 table).
// ============================================================================
//
// Per FC step: encode a SensorPacket (stamped with sim time) → framed write.
// Drain the RX side; ActuatorPackets echo the sensor timestamp → round-trip
// latency = now - echo. Under latency the sim flies on the last received
// frame — exactly the staleness the real vehicle would see.
//
// The engine's SerialLink supplies port discovery/async connect/reconnect UI.
// ============================================================================

#include "fc_glue/FcBackend.h"

#include <viperfc/wire/HilProtocol.h>

#include <Cosmic.h>   // SerialLink, Framing

#include <cstring>
#include <vector>

namespace Viper
{
	class HilBackend : public IFcBackend
	{
	public:
		const char* Name() const override { return "HIL (Teensy over serial)"; }

		Cosmic::SerialLink& Link() { return m_Link; }

		void Reset(const viperfc::Quat&, const viperfc::Vec3&) override
		{
			// The board resets its own FC on arm; clear link-side state.
			m_Rx.clear();
			m_LatencyMs = 0.0f;
			m_LastActuators = {};
			m_Telem = {};
		}

		void ApplyCommand(const FcCommand& cmd) override
		{
			m_PendingCmd = cmd;
			m_CmdDirty = true;
		}

		void Step(const viperfc::SensorFrame& f, viperfc::ActuatorFrame& out, float dt) override
		{
			m_Link.OnUpdate(dt);
			m_SimTimeUs = f.t_us;

			if (m_Link.IsOpen())
			{
				SendSensorPacket(f);

				if (m_CmdDirty)
				{
					SendCommandPacket(m_PendingCmd);
					m_CmdDirty = false;
				}
				if (m_PendingCmd.heartbeat)
				{
					m_HeartbeatClock += dt;
					if (m_HeartbeatClock > 0.2f)   // 5 Hz is plenty vs 1.5 s timeout
					{
						m_HeartbeatClock = 0.0f;
						viperfc::wire::HeartbeatPacket hb;
						SendFramed(reinterpret_cast<const uint8_t*>(&hb), sizeof(hb));
					}
				}
			}

			DrainRx();
			out = m_LastActuators;
		}

		const viperfc::TelemetrySnapshot& Telemetry() const override { return m_Telem; }

		viperfc::FcAlert ConsumeAlert() override
		{
			const viperfc::FcAlert a = m_PendingAlert;
			m_PendingAlert = viperfc::FcAlert::None;
			return a;
		}

		viperfc::FlightComputer* Local() override { return nullptr; }
		float LatencyMs() const override { return m_LatencyMs; }
		bool  IsConnected() const override { return m_Link.IsOpen(); }

	private:
		void SendFramed(const uint8_t* payload, size_t len)
		{
			uint8_t frame[Cosmic::Framing::MaxFrameSize(sizeof(viperfc::wire::SensorPacket))];
			const size_t n = Cosmic::Framing::EncodeFrame(payload, len, frame, sizeof(frame));
			if (n > 0)
				m_Link.Write(reinterpret_cast<const char*>(frame), n);
		}

		void SendSensorPacket(const viperfc::SensorFrame& f)
		{
			viperfc::wire::SensorPacket sp;
			sp.t_us = f.t_us;
			sp.gyro[0] = f.gyro_rads.x;  sp.gyro[1] = f.gyro_rads.y;  sp.gyro[2] = f.gyro_rads.z;
			sp.accel[0] = f.accel_mss.x; sp.accel[1] = f.accel_mss.y; sp.accel[2] = f.accel_mss.z;
			sp.mag[0] = f.mag_uT.x;      sp.mag[1] = f.mag_uT.y;      sp.mag[2] = f.mag_uT.z;
			sp.baro_pa = f.baro_pa;
			sp.airspeed_pa = f.airspeed_pa;
			sp.gps_valid = f.gps.valid ? 1 : 0;
			sp.gps_pos[0] = f.gps.posNed.x; sp.gps_pos[1] = f.gps.posNed.y; sp.gps_pos[2] = f.gps.posNed.z;
			sp.gps_vel[0] = f.gps.velNed.x; sp.gps_vel[1] = f.gps.velNed.y; sp.gps_vel[2] = f.gps.velNed.z;
			sp.gps_sats = f.gps.sats;
			sp.vbat_V = f.vbat_V;
			sp.ibat_A = f.ibat_A;
			SendFramed(reinterpret_cast<const uint8_t*>(&sp), sizeof(sp));
		}

		void SendCommandPacket(const FcCommand& c)
		{
			viperfc::wire::CommandPacket cp;
			cp.arm = c.armRequest < 0 ? 0 : (c.armRequest != 0 ? 1 : 2);
			cp.requestMode = c.requestMode;
			cp.velCmd[0] = c.pilot.velCmdNed.x;
			cp.velCmd[1] = c.pilot.velCmdNed.y;
			cp.velCmd[2] = c.pilot.velCmdNed.z;
			cp.yawRateCmd = c.pilot.yawRateCmd;
			cp.roi[0] = c.roi.x; cp.roi[1] = c.roi.y; cp.roi[2] = c.roi.z;
			SendFramed(reinterpret_cast<const uint8_t*>(&cp), sizeof(cp));
		}

		void DrainRx()
		{
			if (m_Link.ConsumeJustConnected())
				m_Rx.clear();

			const std::string chunk = m_Link.Poll();
			for (const char ch : chunk)
			{
				const uint8_t b = static_cast<uint8_t>(ch);
				if (b == 0x00)
				{
					if (!m_Rx.empty())
					{
						uint8_t payload[256];
						const size_t n = Cosmic::Framing::DecodeFrame(
							m_Rx.data(), m_Rx.size(), payload, sizeof(payload));
						if (n > 0)
							HandlePayload(payload, n);
					}
					m_Rx.clear();
				}
				else if (m_Rx.size() < 512)
				{
					m_Rx.push_back(b);
				}
				else
				{
					m_Rx.clear();   // overflow — resync at next delimiter
				}
			}
		}

		void HandlePayload(const uint8_t* p, size_t len)
		{
			viperfc::wire::ActuatorPacket ap;
			viperfc::wire::StatusPacket st;

			if (viperfc::wire::DecodePacket(p, len, viperfc::wire::ActuatorPacketId, ap))
			{
				for (int i = 0; i < 4; ++i)
				{
					m_LastActuators.motor[i] = ap.motor[i];
					m_LastActuators.servo[i] = ap.servo[i];
				}
				// Latency = sim-now minus the echoed sensor stamp.
				if (m_SimTimeUs >= ap.t_echo_us)
					m_LatencyMs = static_cast<float>(m_SimTimeUs - ap.t_echo_us) * 1e-3f;

				m_Telem.motor[0] = ap.motor[0]; m_Telem.motor[1] = ap.motor[1];
				m_Telem.servo[0] = ap.servo[0]; m_Telem.servo[1] = ap.servo[1];
			}
			else if (viperfc::wire::DecodePacket(p, len, viperfc::wire::StatusPacketId, st))
			{
				const auto newMode = static_cast<viperfc::FlightMode>(st.mode);
				if (newMode != m_Telem.mode)
					m_PendingAlert = viperfc::FcAlert::ModeChange;
				m_Telem.mode  = newMode;
				m_Telem.phase = static_cast<viperfc::TransitionPhase>(st.phase);
				m_Telem.blend = st.blend;
				m_Telem.failsafeFlags  = st.failsafeFlags;
				m_Telem.hoverElapsed_s = st.hoverElapsed_s;
				m_Telem.energyUsed_wh  = st.energyUsed_wh;
				m_Telem.armed = st.armed != 0;
			}
		}

		Cosmic::SerialLink m_Link;
		std::vector<uint8_t> m_Rx;
		viperfc::ActuatorFrame m_LastActuators{};
		viperfc::TelemetrySnapshot m_Telem{};
		viperfc::FcAlert m_PendingAlert = viperfc::FcAlert::None;
		FcCommand m_PendingCmd{};
		bool  m_CmdDirty = false;
		float m_HeartbeatClock = 0.0f;
		float m_LatencyMs = 0.0f;
		uint64_t m_SimTimeUs = 0;
	};
}
