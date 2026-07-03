#pragma once

// viperfc/Failsafe.h
//
// ============================================================================
// Failsafe supervisor + energy accounting (doc 04 §2.4.5, §2.5)
// ============================================================================
//
// Sits ABOVE the mode machine (playbook §6.1) and can only escalate:
//   link loss            → RTL
//   battery reserve      → RTL
//   battery critical     → LAND (in place)
//   geofence (alt/radius)→ RTL
//   hover budget hit     → forced OUT of hover (cruise if flying, else land)
//   envelope (alpha)     → alert (recovery is the controllers' job)
//
// The HOVER-BUDGET accounting is unique to Viper's energy story (3–5 min
// cumulative cap enforced in software) and gets its own unit tests. Energy
// integration: Wh += V·I·dt/3600 from the battery sense lines.
// ============================================================================

#include "viperfc/Params.h"
#include "viperfc/TelemetrySchema.h"

namespace viperfc
{
	class FailsafeSupervisor
	{
	public:
		// What the supervisor demands of the mode machine this step. Numeric
		// order IS the priority order (the latch only escalates).
		enum class Demand : int32_t
		{
			None = 0,
			ExitHover,  // hover budget exhausted — transition out or land
			Rtl,        // return home
			Land,       // vertical land in place
		};

		struct Inputs
		{
			FlightMode mode = FlightMode::Idle;
			float blend     = 0.0f;    // <0.5 counts against the hover budget
			bool  armed     = false;
			bool  airborne  = false;
			float vbat_V    = 16.8f;
			float ibat_A    = 0.0f;
			float altAgl    = 0.0f;
			float distHome  = 0.0f;
			float alphaRad  = 0.0f;
			bool  gpsValid  = true;
			float sinceHeartbeat_s = 0.0f;
		};

		void Reset()
		{
			m_HoverElapsed = 0.0f;
			m_EnergyWh = 0.0f;
			m_Flags = 0;
			m_LastAlert = FcAlert::None;
			m_LatchedDemand = Demand::None;
			m_LowVTimer = m_CritVTimer = 0.0f;
		}

		float HoverElapsed() const { return m_HoverElapsed; }
		float EnergyUsedWh() const { return m_EnergyWh; }
		int32_t Flags() const      { return m_Flags; }

		// Alert raised THIS step (edge-triggered — one event per condition onset).
		FcAlert PendingAlert() const { return m_PendingAlert; }

		Demand Update(const Inputs& in, const FcParams& p, float dt)
		{
			m_PendingAlert = FcAlert::None;

			// --- energy accounting (§2.5) --------------------------------------
			if (in.armed)
				m_EnergyWh += in.vbat_V * in.ibat_A * dt / 3600.0f;

			// --- hover-budget clock ----------------------------------------------
			const bool hoverSide = in.armed && in.airborne && in.blend < 0.5f;
			if (hoverSide)
				m_HoverElapsed += dt;

			// --- evaluate conditions (edge-triggered flags) -----------------------
			Demand demand = Demand::None;

			const float cellV = in.vbat_V / (p.batt_cells > 0.5f ? p.batt_cells : 4.0f);
			const float usableWh = p.batt_capacity_wh * p.batt_usable_frac;

			// Voltage thresholds are TIME-QUALIFIED: a punch-out or gust fight
			// sags the pack through IR for a second or two, and these flags
			// latch — reacting to the instantaneous dip would land a healthy
			// aircraft (exactly what G1 caught in sim).
			m_CritVTimer = (in.armed && cellV < p.batt_crit_v_cell) ? m_CritVTimer + dt : 0.0f;
			m_LowVTimer  = (in.armed && cellV < p.batt_low_v_cell)  ? m_LowVTimer  + dt : 0.0f;

			if (Raise(FcAlert::BatteryCritical, m_CritVTimer > p.batt_v_qualify_s))
				{}
			if (Raise(FcAlert::BatteryLow, m_LowVTimer > p.batt_v_qualify_s))
				{}
			if (Raise(FcAlert::BatteryReserve,
				in.armed && usableWh - m_EnergyWh < p.batt_low_wh_frac * usableWh))
				{}
			if (Raise(FcAlert::LinkLost, in.armed && in.sinceHeartbeat_s > p.link_timeout_s))
				{}
			if (Raise(FcAlert::GeofenceAlt, in.airborne && in.altAgl > p.geofence_agl_m))
				{}
			if (Raise(FcAlert::GeofenceRadius, in.airborne && in.distHome > p.geofence_radius_m))
				{}
			if (Raise(FcAlert::HoverBudgetWarn,
				m_HoverElapsed > p.hover_warn_frac * p.hover_budget_s))
				{}
			if (Raise(FcAlert::HoverBudgetHit, m_HoverElapsed > p.hover_budget_s))
				{}
			if (Raise(FcAlert::EnvelopeAlpha,
				in.blend > 0.5f && std::fabs(in.alphaRad) > p.envelope_alpha_max))
				{}
			if (Raise(FcAlert::GpsLost, in.armed && in.airborne && !in.gpsValid))
				{}

			// --- demands, highest priority first ---------------------------------
			if (Active(FcAlert::BatteryCritical))
				demand = Demand::Land;
			else if (Active(FcAlert::LinkLost) || Active(FcAlert::BatteryReserve) ||
			         Active(FcAlert::GeofenceAlt) || Active(FcAlert::GeofenceRadius))
				demand = Demand::Rtl;
			else if (Active(FcAlert::HoverBudgetHit))
				demand = Demand::ExitHover;

			// Demands never de-escalate mid-flight: latch the strongest one.
			if (static_cast<int>(demand) > static_cast<int>(m_LatchedDemand))
				m_LatchedDemand = demand;

			// Link recovery releases a pure link-loss RTL (standard behaviour).
			if (m_LatchedDemand == Demand::Rtl &&
			    !Active(FcAlert::LinkLost) && !Active(FcAlert::BatteryReserve) &&
			    !Active(FcAlert::GeofenceAlt) && !Active(FcAlert::GeofenceRadius))
				m_LatchedDemand = Demand::None;
			if (!in.armed)
				m_LatchedDemand = Demand::None;

			return m_LatchedDemand;
		}

		bool Active(FcAlert a) const { return (m_Flags & Bit(a)) != 0; }

	private:
		static int32_t Bit(FcAlert a) { return 1 << static_cast<int32_t>(a); }

		// Set/clear a condition flag; returns true and queues the alert on the
		// rising edge only.
		bool Raise(FcAlert a, bool condition)
		{
			const int32_t bit = Bit(a);
			const bool was = (m_Flags & bit) != 0;
			if (condition && !was)
			{
				m_Flags |= bit;
				m_PendingAlert = a;   // last onset this step wins the chime
				return true;
			}
			if (!condition && was)
			{
				// Hover-budget + battery flags stay latched (they cannot "get
				// better" mid-flight); transient ones clear.
				if (a == FcAlert::LinkLost || a == FcAlert::GeofenceAlt ||
				    a == FcAlert::GeofenceRadius || a == FcAlert::EnvelopeAlpha ||
				    a == FcAlert::GpsLost)
					m_Flags &= ~bit;
			}
			return false;
		}

		float   m_HoverElapsed = 0.0f;
		float   m_EnergyWh     = 0.0f;
		float   m_LowVTimer    = 0.0f;   // continuous time under batt_low_v_cell
		float   m_CritVTimer   = 0.0f;   // continuous time under batt_crit_v_cell
		int32_t m_Flags        = 0;
		FcAlert m_PendingAlert = FcAlert::None;
		FcAlert m_LastAlert    = FcAlert::None;
		Demand  m_LatchedDemand = Demand::None;
	};
}
