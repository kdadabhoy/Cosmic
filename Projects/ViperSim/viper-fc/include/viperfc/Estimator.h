#pragma once

// viperfc/Estimator.h
//
// ============================================================================
// Complementary estimator (doc 04 §1: "estimator (complementary → EKF)")
// ============================================================================
//
// Attitude: gyro integration corrected by accelerometer tilt (Mahony-style
// vector feedback) and magnetometer yaw. Position/velocity: dead-reckoned from
// the attitude + accel, blended toward GPS and baro at fixed gains. Simple,
// deterministic, allocation-free — the EKF upgrade slots in behind the same
// State struct without touching consumers.
//
// TRUTH FIRST, ESTIMATION SECOND (doc 04 §2.1): with perfect sensors this
// filter converges to truth in well under a second, so P2 controller work is
// never tuned against an estimator bug; P3 turns the noise on and hardens it.
// ============================================================================

#include "viperfc/IHal.h"
#include "viperfc/Params.h"

namespace viperfc
{
	class Estimator
	{
	public:
		struct State
		{
			Quat  att{};            // body -> NED
			Vec3  posNed{};
			Vec3  velNed{};
			Vec3  gyroBias{};
			float airspeed  = 0.0f; // m/s (pitot below ~5 m/s reads unreliable)
			float altAgl    = 0.0f; // -posNed.z relative to home
			bool  airspeedValid = false;
		};

		void Reset(const Quat& att0 = {}, const Vec3& pos0 = {})
		{
			m_S = State{};
			m_S.att = att0;
			m_S.posNed = pos0;
			m_BaroRefPa = 0.0f;
			m_HaveBaroRef = false;
		}

		const State& Get() const { return m_S; }

		void Update(const SensorFrame& f, const FcParams& p, float dt)
		{
			if (dt <= 0.0f)
				return;

			// --- Attitude: Mahony complementary --------------------------------
			// Accel measures specific force; in unaccelerated flight that is
			// -gravity (body frame). Compare against the attitude's idea of
			// "up" and steer the gyro integration by the error.
			Vec3 corr{};

			const float aN = Norm(f.accel_mss);
			if (aN > 0.5f * kGravity && aN < 1.5f * kGravity)
			{
				const Vec3 upMeas = Normalized(f.accel_mss);              // body-frame up (≈ -g dir)
				const Vec3 upEst  = RotateInv(m_S.att, { 0, 0, -1 });     // world up in body frame
				corr += Cross(upMeas, upEst) * p.est_acc_gain;
			}

			// Yaw from the magnetometer: compare the HORIZONTAL projection of
			// the measured field with world north.
			const float mN = Norm(f.mag_uT);
			if (mN > 1e-3f)
			{
				const Vec3 magWorld = Rotate(m_S.att, f.mag_uT);
				const float yawErr = WrapPi(std::atan2(magWorld.y, magWorld.x));   // declination handled sim-side
				corr += RotateInv(m_S.att, { 0, 0, -yawErr }) * p.est_mag_gain;
			}

			const Vec3 omega = f.gyro_rads - m_S.gyroBias + corr;
			m_S.att = IntegrateBodyRate(m_S.att, omega, dt);

			// --- Velocity/position: inertial + GPS/baro blends ------------------
			const Vec3 accelWorld = Rotate(m_S.att, f.accel_mss) + Vec3{ 0, 0, kGravity };
			m_S.velNed += accelWorld * dt;
			m_S.posNed += m_S.velNed * dt;

			if (f.gps.valid)
			{
				const float kp = Clampf(p.est_gps_pos_gain * dt, 0.0f, 1.0f);
				const float kv = Clampf(p.est_gps_vel_gain * dt, 0.0f, 1.0f);
				m_S.posNed += (f.gps.posNed - m_S.posNed) * kp;
				m_S.velNed += (f.gps.velNed - m_S.velNed) * kv;
			}

			// Baro altitude (relative to the first sample = home).
			if (!m_HaveBaroRef)
			{
				m_BaroRefPa = f.baro_pa;
				m_HaveBaroRef = true;
			}
			const float baroAlt = (m_BaroRefPa - f.baro_pa) / (kRhoAir * kGravity);
			const float kb = Clampf(p.est_baro_gain * dt, 0.0f, 1.0f);
			m_S.posNed.z += ((-baroAlt) - m_S.posNed.z) * kb;

			m_S.altAgl = -m_S.posNed.z;

			// --- Airspeed from pitot (q = ½ρV²) — flagged unreliable < ~5 m/s ---
			const float q = f.airspeed_pa > 0.0f ? f.airspeed_pa : 0.0f;
			m_S.airspeed = std::sqrt(2.0f * q / kRhoAir);
			m_S.airspeedValid = m_S.airspeed > 5.0f;
		}

	private:
		State m_S{};
		float m_BaroRefPa = 0.0f;
		bool  m_HaveBaroRef = false;
	};
}
