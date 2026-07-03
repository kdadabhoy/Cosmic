#pragma once

// FcBackend.h
//
// ============================================================================
// The SITL ↔ HIL seam (plan §1: "SimHal and the HIL bridge implement the same
// internal interface → SITL↔HIL is a dropdown, not a rebuild").
// ============================================================================
//
// SimHub pushes one SensorFrame per FC step and pulls the latest ActuatorFrame
// + telemetry. SitlBackend runs viperfc::FlightComputer in-process (SimHal);
// HilBackend ships the same frames to a physical Teensy over the E5 framed
// serial link and reports round-trip latency for the on-screen figure.
// ============================================================================

#include <viperfc/ViperFc.h>

#include <string>

namespace Viper
{
	// Command mirror of viperfc's control surface — one struct both backends
	// accept, so the UI code doesn't care where the FC runs.
	struct FcCommand
	{
		// Edge-triggered so the FC's own auto-disarm-on-touchdown is never
		// fought by a stale level: -1 = no change, 0 = disarm, 1 = arm.
		int   armRequest = -1;
		int   requestMode = -1;             // viperfc::FlightMode, -1 = none
		viperfc::FlightComputer::PilotInput pilot{};
		viperfc::Vec3 roi{ 60.0f, 0.0f, 0.0f };
		bool  heartbeat = true;             // false = link-kill fault injection
	};

	class IFcBackend
	{
	public:
		virtual ~IFcBackend() = default;

		virtual const char* Name() const = 0;

		virtual void Reset(const viperfc::Quat& att0, const viperfc::Vec3& pos0) = 0;
		virtual void ApplyCommand(const FcCommand& cmd) = 0;

		// One FC step: sensors in; the actuator output lands in `out`.
		// HIL returns the LAST RECEIVED frame (one-step-old under latency).
		virtual void Step(const viperfc::SensorFrame& f, viperfc::ActuatorFrame& out, float dt) = 0;

		virtual const viperfc::TelemetrySnapshot& Telemetry() const = 0;
		virtual viperfc::FcAlert ConsumeAlert() = 0;

		// In-process FC when one exists (SITL: tuning screen edits its gains
		// live; HIL: nullptr — gains live on the board).
		virtual viperfc::FlightComputer* Local() = 0;

		// Round-trip latency figure (ms); SITL is by definition 0.
		virtual float LatencyMs() const { return 0.0f; }
		virtual bool  IsConnected() const { return true; }
	};

	// =========================================================================
	// SitlBackend — viper-fc in-process. This IS the plan's "SimHal": the sim
	// side of the HAL boundary. Sensor models (sim/Sensors.h) fill
	// viperfc::SensorFrame from IDynamics truth, this backend runs the
	// FlightComputer on them, and the actuator frame goes back into the
	// dynamics — the same seam TeensyHal implements on hardware.
	// =========================================================================
	class SitlBackend : public IFcBackend
	{
	public:
		const char* Name() const override { return "SITL (in-process)"; }

		void Reset(const viperfc::Quat& att0, const viperfc::Vec3& pos0) override
		{
			m_Fc.Reset(att0, pos0);
			m_Fc.SetHome({ pos0.x, pos0.y, 0.0f });
		}

		void ApplyCommand(const FcCommand& cmd) override
		{
			if (cmd.armRequest >= 0)
				m_Fc.Arm(cmd.armRequest != 0);
			if (cmd.requestMode >= 0)
				m_Fc.RequestMode(static_cast<viperfc::FlightMode>(cmd.requestMode));
			m_Fc.SetPilotInput(cmd.pilot);
			m_Fc.SetRoi(cmd.roi);
			if (cmd.heartbeat)
				m_Fc.Heartbeat();
		}

		void Step(const viperfc::SensorFrame& f, viperfc::ActuatorFrame& out, float dt) override
		{
			m_Fc.Step(f, out, dt);
		}

		const viperfc::TelemetrySnapshot& Telemetry() const override { return m_Fc.Telemetry(); }
		viperfc::FcAlert ConsumeAlert() override { return m_Fc.ConsumeAlert(); }
		viperfc::FlightComputer* Local() override { return &m_Fc; }

	private:
		viperfc::FlightComputer m_Fc;
	};

	// The plan's name for the in-process seam (doc 04 §1 architecture sketch).
	using SimHal = SitlBackend;
}
