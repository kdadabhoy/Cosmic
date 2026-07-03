#pragma once

// viperfc/Pid.h
//
// Portable PID with clamped integrator (anti-windup), derivative-on-measurement
// (no setpoint kick), and output limits. Allocation-free; Reset() re-seeds.

#include "viperfc/Math.h"

namespace viperfc
{
	class Pid
	{
	public:
		Pid() = default;
		Pid(float kp, float ki, float kd, float iLimit, float outLimit)
			: m_Kp(kp), m_Ki(ki), m_Kd(kd), m_ILimit(iLimit), m_OutLimit(outLimit) {}

		void SetGains(float kp, float ki, float kd) { m_Kp = kp; m_Ki = ki; m_Kd = kd; }
		void SetLimits(float iLimit, float outLimit) { m_ILimit = iLimit; m_OutLimit = outLimit; }

		void Reset()
		{
			m_I = 0.0f;
			m_PrevMeas = 0.0f;
			m_Primed = false;
		}

		// setpoint/measurement in the same unit; returns the clamped output.
		float Update(float setpoint, float measurement, float dt)
		{
			const float err = setpoint - measurement;
			if (dt <= 0.0f)
				return Clampf(m_Kp * err + m_I, -m_OutLimit, m_OutLimit);

			m_I = Clampf(m_I + m_Ki * err * dt, -m_ILimit, m_ILimit);

			float d = 0.0f;
			if (m_Primed && m_Kd > 0.0f)
				d = -m_Kd * (measurement - m_PrevMeas) / dt;   // derivative on measurement
			m_PrevMeas = measurement;
			m_Primed = true;

			return Clampf(m_Kp * err + m_I + d, -m_OutLimit, m_OutLimit);
		}

		float Integrator() const { return m_I; }

	private:
		float m_Kp = 0.0f, m_Ki = 0.0f, m_Kd = 0.0f;
		float m_ILimit = 1.0f, m_OutLimit = 1.0f;
		float m_I = 0.0f;
		float m_PrevMeas = 0.0f;
		bool  m_Primed = false;
	};
}
