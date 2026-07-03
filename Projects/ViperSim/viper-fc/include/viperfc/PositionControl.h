#pragma once

// viperfc/PositionControl.h
//
// ============================================================================
// Hover position/velocity loop (doc 04 §2.4.1 "position hold on top")
// ============================================================================
//
// pos error → velocity setpoint (clamped) → velocity PI → specific-force
// demand → (desired tailsitter attitude, collective thrust). The attitude
// target maps body +X (the thrust axis — nose up in hover) onto the demanded
// thrust direction, with the remaining freedom (spin about the thrust axis)
// pinned by the commanded hover heading.
// ============================================================================

#include "viperfc/Pid.h"
#include "viperfc/Params.h"

namespace viperfc
{
	class PositionControl
	{
	public:
		struct Output
		{
			Quat  attSp{};          // desired attitude (body->NED)
			float thrust = 0.0f;    // normalized collective [0, 1]
			Vec3  velSp{};          // for telemetry/plots
		};

		void Configure(const FcParams& p)
		{
			m_VelN.SetGains(p.vel_kp, p.vel_ki, 0.0f);
			m_VelE.SetGains(p.vel_kp, p.vel_ki, 0.0f);
			m_VelD.SetGains(p.vel_kp, p.vel_ki, 0.0f);
			m_VelN.SetLimits(p.vel_i_limit, 8.0f);
			m_VelE.SetLimits(p.vel_i_limit, 8.0f);
			m_VelD.SetLimits(p.vel_i_limit, 8.0f);
		}

		void Reset()
		{
			m_VelN.Reset(); m_VelE.Reset(); m_VelD.Reset();
		}

		// posSp/pos, vel in NED; headingRad = desired hover heading (belly
		// direction); velFF adds a feed-forward velocity command (stick flying).
		Output Update(const Vec3& posSp, const Vec3& pos, const Vec3& vel,
		              float headingRad, const Vec3& velFF, const FcParams& p, float dt)
		{
			// --- pos -> vel setpoint --------------------------------------------
			Vec3 velSp = (posSp - pos) * p.pos_kp + velFF;
			Vec3 velXy{ velSp.x, velSp.y, 0.0f };
			velXy = ClampNorm(velXy, p.vel_max_xy);
			velSp.x = velXy.x;
			velSp.y = velXy.y;
			velSp.z = Clampf(velSp.z, -p.vel_max_up, p.vel_max_dn);   // NED: -z = climb

			// --- vel -> specific-force demand (m/s^2, NED) ------------------------
			const Vec3 accSp{
				m_VelN.Update(velSp.x, vel.x, dt),
				m_VelE.Update(velSp.y, vel.y, dt),
				m_VelD.Update(velSp.z, vel.z, dt),
			};

			// Specific force the vehicle must produce: cancel gravity + accSp.
			// f = accSp - g  (g = +9.81 D). Hover: f = (0,0,-9.81) — straight up.
			Vec3 f = accSp - Vec3{ 0, 0, kGravity };

			// Tilt limit: cap the horizontal component against the vertical.
			const float fUp = -f.z > 0.1f ? -f.z : 0.1f;
			const float maxHoriz = fUp * std::tan(p.tilt_max_rad);
			Vec3 fXy{ f.x, f.y, 0.0f };
			fXy = ClampNorm(fXy, maxHoriz);
			f.x = fXy.x;
			f.y = fXy.y;

			Output out;
			out.velSp = velSp;

			// --- attitude target: body +X onto f̂, heading pins the roll DOF -----
			const Vec3 xb = Normalized(f);                            // thrust dir (world, ≈ up)
			const Vec3 heading{ std::cos(headingRad), std::sin(headingRad), 0.0f };
			Vec3 zb = heading - xb * Dot(heading, xb);                // belly toward heading
			if (NormSq(zb) < 1e-8f)
				zb = { 1, 0, 0 };                                     // degenerate: nose horizontal
			zb = Normalized(zb);
			const Vec3 yb = Cross(zb, xb);                            // right wing (x × y = z holds)
			out.attSp = FromBasis(xb, yb, zb);

			// --- collective: |f|·m against total installed thrust ----------------
			const float totalMax = p.max_thrust_N * p.motor_count;
			out.thrust = Clampf(Norm(f) * p.mass_kg / (totalMax > 1.0f ? totalMax : 1.0f), 0.0f, 1.0f);

			return out;
		}

	private:
		Pid m_VelN, m_VelE, m_VelD;
	};
}
