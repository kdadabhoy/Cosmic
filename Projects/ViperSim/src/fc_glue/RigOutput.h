#pragma once

// RigOutput.h
//
// ============================================================================
// P7 — gimbal rig ("iron bird lite"): mirror the sim/HIL attitude onto a
// 2–3-axis servo gimbal holding a foam model (doc 04 §1 integration modes).
// ============================================================================
//
// Streams rate-clamped Euler angles over a SerialLink at 50 Hz as
// newline-terminated ASCII (hobby-servo-controller friendly, trivially parsed
// on an Arduino/Teensy):
//
//     RIG,<roll_deg>,<pitch_deg>,<yaw_deg>\n
//
// The RATE CLAMP lives here AND must live in the rig firmware (the plan's
// "rate-clamped in firmware" is the safety of record; this one keeps commands
// sane even against buggy firmware). E12 RateLimiter per axis.
// ============================================================================

#include <Cosmic.h>   // SerialLink, math/Filters.h (E12), math/Spatial.h

#include <cstdio>

namespace Viper
{
	class RigOutput
	{
	public:
		RigOutput()
		{
			SetMaxRateDps(120.0f);
		}

		Cosmic::SerialLink& Link() { return m_Link; }

		bool  enabled = false;
		float sendRateHz = 50.0f;

		void SetMaxRateDps(float dps)
		{
			m_MaxRateDps = dps;
			m_Roll.SetMaxRate(dps);
			m_Pitch.SetMaxRate(dps);
			m_Yaw.SetMaxRate(dps);
		}
		float MaxRateDps() const { return m_MaxRateDps; }

		// Call every frame with the attitude to mirror (sim truth or HIL est).
		void Update(const glm::quat& attNed, float dt)
		{
			m_Link.OnUpdate(dt);
			(void)m_Link.Poll();   // keep the receive path drained

			const glm::vec3 eulerDeg = Cosmic::Math::EulerZYXFromQuat(attNed);
			const float r = m_Roll.Update(eulerDeg.x, dt);
			const float p = m_Pitch.Update(eulerDeg.y, dt);
			const float y = m_Yaw.Update(eulerDeg.z, dt);
			m_LastSent = { r, p, y };

			if (!enabled || !m_Link.IsOpen())
				return;

			m_SendClock += dt;
			if (m_SendClock < 1.0f / sendRateHz)
				return;
			m_SendClock = 0.0f;

			char line[96];
			const int n = std::snprintf(line, sizeof(line), "RIG,%.2f,%.2f,%.2f\n", r, p, y);
			if (n > 0)
				m_Link.Write(line, static_cast<size_t>(n));
		}

		const glm::vec3& LastSentDeg() const { return m_LastSent; }

	private:
		Cosmic::SerialLink   m_Link;
		Cosmic::RateLimiter  m_Roll, m_Pitch, m_Yaw;   // degrees, per axis
		glm::vec3            m_LastSent{ 0.0f };
		float m_SendClock = 0.0f;
		float m_MaxRateDps = 120.0f;
	};
}
