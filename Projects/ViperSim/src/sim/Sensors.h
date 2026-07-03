#pragma once

// Sensors.h
//
// ============================================================================
// Sensor models feeding viperfc::SensorFrame (plan §2.3)
// ============================================================================
//
// Sampled from IDynamics truth at each FC step; every parameter from
// viper.toml [sensors.*]; ALL randomness via E15 seeded RNG (one independent
// stream per sensor so adding a consumer never shifts another sensor's
// sequence — replays must reproduce).
//
//   gyro/accel — white noise + bias random-walk
//   baro       — noise + slow drift
//   GPS        — 10 Hz, ~150 ms latency (ring buffer), position/velocity noise
//   mag        — world field rotated to body + noise
//   pitot      — q = ½ρu|u| + noise; UNRELIABLE below ~5 m/s by physics
//                (tiny q vs noise floor) — it gates the transition logic
//   battery    — pack model (Battery.h) provides V/I
//
// `perfect` toggle (sim.perfect_sensors): truth passthrough, GPS instant —
// develop against perfect, then harden the estimator (P3).
// ============================================================================

#include "sim/ComposableDynamics.h"
#include "sim/Battery.h"

#include <viperfc/IHal.h>

#include <Cosmic.h>   // math/Random.h (E15)

#include <array>

namespace Viper
{
	struct SensorParams
	{
		bool  perfect         = true;
		float gyro_noise      = 0.006f;   // rad/s white
		float gyro_bias_walk  = 0.0003f;  // rad/s per sqrt(s)
		float accel_noise     = 0.06f;    // m/s^2
		float baro_noise_pa   = 4.0f;
		float baro_drift_pa_s = 0.15f;
		float mag_noise_uT    = 0.6f;
		float pitot_noise_pa  = 3.0f;
		float gps_rate_hz     = 10.0f;
		float gps_latency_s   = 0.15f;
		float gps_pos_noise   = 0.8f;     // m horizontal (1.5x vertical)
		float gps_vel_noise   = 0.15f;    // m/s
		uint32_t seed         = 42;
	};

	// Fault injection switches (doc 04 §2.4.5 — UI buttons drive these).
	struct SensorFaults
	{
		bool gpsDrop     = false;
		bool pitotFreeze = false;
		bool magFail     = false;
	};

	class Sensors
	{
	public:
		void Configure(const SensorParams& p)
		{
			m_P = p;
			Reset();
		}

		void Reset()
		{
			// Independent streams per sensor (E15 Random(seed, stream)).
			m_RngGyro  = Cosmic::Random(m_P.seed, 1);
			m_RngAccel = Cosmic::Random(m_P.seed, 2);
			m_RngBaro  = Cosmic::Random(m_P.seed, 3);
			m_RngMag   = Cosmic::Random(m_P.seed, 4);
			m_RngPitot = Cosmic::Random(m_P.seed, 5);
			m_RngGps   = Cosmic::Random(m_P.seed, 6);
			m_GyroBias = { 0, 0, 0 };
			m_BaroDrift = 0.0f;
			m_GpsClock = 0.0f;
			m_GpsQueue.fill({});
			m_GpsHead = m_GpsCount = 0;
			m_LastGps = {};
			m_LastPitotPa = 0.0f;
			m_Time = 0.0f;
		}

		SensorFaults& Faults() { return m_Faults; }
		const SensorParams& Params() const { return m_P; }
		void SetPerfect(bool perfect) { m_P.perfect = perfect; }

		// Sample one frame from truth. dt = FC step. homeD = NED D of the home
		// point (baro reference altitude).
		viperfc::SensorFrame Sample(const ComposableDynamics& dyn, const Battery& batt,
		                            float homeD, float dt)
		{
			const RigidState& s = dyn.GetTruth();
			const glm::quat attInv = glm::conjugate(s.attNed);
			m_Time += dt;

			viperfc::SensorFrame f;
			f.t_us = static_cast<uint64_t>(m_Time * 1e6);

			// --- gyro -----------------------------------------------------------
			glm::vec3 gyro = s.omegaBody;
			if (!m_P.perfect)
			{
				const float sw = m_P.gyro_bias_walk * std::sqrt(std::max(dt, 1e-4f));
				m_GyroBias += glm::vec3(m_RngGyro.Gaussian(0, sw), m_RngGyro.Gaussian(0, sw), m_RngGyro.Gaussian(0, sw));
				gyro += m_GyroBias + glm::vec3(
					m_RngGyro.Gaussian(0, m_P.gyro_noise),
					m_RngGyro.Gaussian(0, m_P.gyro_noise),
					m_RngGyro.Gaussian(0, m_P.gyro_noise));
			}
			f.gyro_rads = ToFc(gyro);

			// --- accel (specific force: a - g, in body) ---------------------------
			glm::vec3 accel = attInv * (dyn.LastAccelNed() - glm::vec3(0, 0, Cosmic::Math::GravityMss));
			if (!m_P.perfect)
				accel += glm::vec3(
					m_RngAccel.Gaussian(0, m_P.accel_noise),
					m_RngAccel.Gaussian(0, m_P.accel_noise),
					m_RngAccel.Gaussian(0, m_P.accel_noise));
			f.accel_mss = ToFc(accel);

			// --- mag (world field ~ (22, 0, 42) µT, declination folded to 0) -----
			if (!m_Faults.magFail)
			{
				glm::vec3 mag = attInv * glm::vec3(22.0f, 0.0f, 42.0f);
				if (!m_P.perfect)
					mag += glm::vec3(
						m_RngMag.Gaussian(0, m_P.mag_noise_uT),
						m_RngMag.Gaussian(0, m_P.mag_noise_uT),
						m_RngMag.Gaussian(0, m_P.mag_noise_uT));
				f.mag_uT = ToFc(mag);
			}
			else
			{
				f.mag_uT = { 0, 0, 0 };   // dead sensor — estimator ignores it
			}

			// --- baro (pressure altitude around home) -----------------------------
			const float altAboveHome = -(s.posNed.z - homeD);
			float baro = 101325.0f - 1.225f * Cosmic::Math::GravityMss * altAboveHome;
			if (!m_P.perfect)
			{
				m_BaroDrift += m_RngBaro.Gaussian(0, m_P.baro_drift_pa_s * dt);
				baro += m_BaroDrift + m_RngBaro.Gaussian(0, m_P.baro_noise_pa);
			}
			f.baro_pa = baro;

			// --- pitot (axial component only — it is a tube on the nose) ----------
			if (!m_Faults.pitotFreeze)
			{
				const glm::vec3 airBody = attInv * (s.velNed - dyn.CurrentWindNed());
				float q = 0.5f * 1.225f * airBody.x * std::fabs(airBody.x);
				if (!m_P.perfect)
					q += m_RngPitot.Gaussian(0, m_P.pitot_noise_pa);
				m_LastPitotPa = std::max(q, 0.0f);
			}
			f.airspeed_pa = m_LastPitotPa;   // frozen fault holds the last value

			// --- GPS: sampled at gps_rate into a latency queue ---------------------
			m_GpsClock += dt;
			const float gpsPeriod = 1.0f / std::max(m_P.gps_rate_hz, 1.0f);
			if (m_GpsClock >= gpsPeriod)
			{
				m_GpsClock -= gpsPeriod;
				GpsSample raw;
				raw.pos = s.posNed;
				raw.vel = s.velNed;
				raw.t   = m_Time;
				if (!m_P.perfect)
				{
					raw.pos += glm::vec3(
						m_RngGps.Gaussian(0, m_P.gps_pos_noise),
						m_RngGps.Gaussian(0, m_P.gps_pos_noise),
						m_RngGps.Gaussian(0, 1.5f * m_P.gps_pos_noise));
					raw.vel += glm::vec3(
						m_RngGps.Gaussian(0, m_P.gps_vel_noise),
						m_RngGps.Gaussian(0, m_P.gps_vel_noise),
						m_RngGps.Gaussian(0, m_P.gps_vel_noise));
				}
				Push(raw);
			}

			// Deliver the newest sample that is at least gps_latency old.
			const float latency = m_P.perfect ? 0.0f : m_P.gps_latency_s;
			for (int i = 0; i < m_GpsCount; ++i)
			{
				const GpsSample& g = At(i);
				if (m_Time - g.t >= latency)
					m_LastGps = g;
			}

			if (!m_Faults.gpsDrop && m_LastGps.t > 0.0f)
			{
				f.gps.valid  = true;
				f.gps.posNed = ToFc(m_LastGps.pos);
				f.gps.velNed = ToFc(m_LastGps.vel);
				f.gps.sats   = 14;
			}
			else
			{
				f.gps.valid = false;
			}

			// --- battery ------------------------------------------------------------
			f.vbat_V = batt.Voltage();
			f.ibat_A = batt.Current();

			return f;
		}

	private:
		struct GpsSample { glm::vec3 pos{}; glm::vec3 vel{}; float t = 0.0f; };

		static viperfc::Vec3 ToFc(const glm::vec3& v) { return { v.x, v.y, v.z }; }

		void Push(const GpsSample& g)
		{
			m_GpsQueue[m_GpsHead] = g;
			m_GpsHead = (m_GpsHead + 1) % static_cast<int>(m_GpsQueue.size());
			if (m_GpsCount < static_cast<int>(m_GpsQueue.size()))
				++m_GpsCount;
		}

		// Oldest-first access into the ring.
		const GpsSample& At(int i) const
		{
			const int start = (m_GpsHead - m_GpsCount + static_cast<int>(m_GpsQueue.size()) * 2)
				% static_cast<int>(m_GpsQueue.size());
			return m_GpsQueue[(start + i) % static_cast<int>(m_GpsQueue.size())];
		}

		SensorParams m_P{};
		SensorFaults m_Faults{};

		Cosmic::Random m_RngGyro, m_RngAccel, m_RngBaro, m_RngMag, m_RngPitot, m_RngGps;
		glm::vec3 m_GyroBias{ 0.0f };
		float m_BaroDrift = 0.0f;
		float m_GpsClock = 0.0f;
		float m_LastPitotPa = 0.0f;
		float m_Time = 0.0f;

		std::array<GpsSample, 16> m_GpsQueue{};
		int m_GpsHead = 0;
		int m_GpsCount = 0;
		GpsSample m_LastGps{};
	};
}
