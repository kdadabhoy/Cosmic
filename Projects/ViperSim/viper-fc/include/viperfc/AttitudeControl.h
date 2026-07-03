#pragma once

// viperfc/AttitudeControl.h
//
// ============================================================================
// Cascaded attitude loop (doc 04 §2.4.1): quaternion attitude error →
// rate setpoints → rate PIDs → normalized body torques.
// ============================================================================
//
// Quaternion error via AttitudeErrorBody (shortest path, no Euler in the loop
// — a tailsitter lives at the pitch-90° singularity). rateFF adds a
// feed-forward (e.g. heading-spin rate in hover).
// ============================================================================

#include "viperfc/Pid.h"
#include "viperfc/Params.h"

namespace viperfc
{
	class AttitudeControl
	{
	public:
		void Configure(const FcParams& p)
		{
			m_RateX.SetGains(p.rate_kp_x, p.rate_ki_x, p.rate_kd_x);
			m_RateY.SetGains(p.rate_kp_y, p.rate_ki_y, p.rate_kd_y);
			m_RateZ.SetGains(p.rate_kp_z, p.rate_ki_z, p.rate_kd_z);
			m_RateX.SetLimits(p.rate_i_limit, 1.0f);
			m_RateY.SetLimits(p.rate_i_limit, 1.0f);
			m_RateZ.SetLimits(p.rate_i_limit, 1.0f);
		}

		void Reset()
		{
			m_RateX.Reset();
			m_RateY.Reset();
			m_RateZ.Reset();
			m_LastErr = {};
		}

		// q: current attitude estimate; qd: desired; gyro: body rates (rad/s).
		// Returns normalized torque demands in [-1, 1] per body axis.
		Vec3 Update(const Quat& q, const Quat& qd, const Vec3& gyro,
		            const Vec3& rateFF, const FcParams& p, float dt)
		{
			m_LastErr = AttitudeErrorBody(q, qd);

			Vec3 rateSp = m_LastErr * p.att_kp + rateFF;
			rateSp = ClampNorm(rateSp, p.rate_max_rads);
			m_LastRateSp = rateSp;

			return {
				m_RateX.Update(rateSp.x, gyro.x, dt),
				m_RateY.Update(rateSp.y, gyro.y, dt),
				m_RateZ.Update(rateSp.z, gyro.z, dt),
			};
		}

		float ErrorAngleRad() const { return Norm(m_LastErr); }
		const Vec3& RateSetpoint() const { return m_LastRateSp; }

	private:
		Pid  m_RateX, m_RateY, m_RateZ;
		Vec3 m_LastErr{};
		Vec3 m_LastRateSp{};
	};
}
