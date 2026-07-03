#pragma once

// Battery.h
//
// Battery + electrical power model (doc 04 §2.5 — "EARLY: the first genuinely
// useful output"). Motor electrical power from momentum theory:
//
//   P_elec = ( T_tot^1.5 / sqrt(2 rho A_disc_tot)  +  T_tot * V_axial ) / eta(V)  +  P_base
//
// which lands on the proposal's numbers with the design-point inputs
// (~230 W hover, ~106 W cruise @ 20 m/s). Diverging from the spreadsheet is a
// FINDING, not a bug — the Energy screen surfaces both.
//
// Voltage: linear OCV over the usable window minus IR sag. All parameters from
// viper.toml [battery]/[power]; defaults are the tracker's values.

#include <algorithm>
#include <cmath>

namespace Viper
{
	struct BatteryParams
	{
		float capacity_wh  = 100.0f;
		float usable_frac  = 0.85f;
		float cells        = 4.0f;
		float r_int_ohm    = 0.08f;
		float v_full_cell  = 4.2f;
		float v_empty_cell = 3.3f;
		float base_load_w  = 6.0f;     // avionics/servos
		// power model
		float disc_area_m2 = 0.092f;   // TOTAL prop disc area (2 x r=0.121 m)
		float eta_hover    = 0.50f;    // electric->thrust chain efficiency, static
		float eta_cruise   = 0.70f;    // ... at cruise speed
	};

	class Battery
	{
	public:
		void Configure(const BatteryParams& p) { m_P = p; }
		const BatteryParams& Params() const { return m_P; }

		void Reset()
		{
			m_UsedWh = 0.0f;
			m_V = m_P.cells * m_P.v_full_cell;
			m_I = 0.0f;
			m_PowerW = 0.0f;
		}

		// thrustTotalN: current total prop thrust; vAxial: airspeed component
		// along the prop axis (m/s); airspeed for the efficiency schedule.
		void Update(float thrustTotalN, float vAxial, float airspeed, float dt)
		{
			const float T = std::max(thrustTotalN, 0.0f);
			const float rho2A = std::sqrt(2.0f * 1.225f * std::max(m_P.disc_area_m2, 1e-3f));
			const float pIdeal = std::pow(T, 1.5f) / rho2A + T * std::max(vAxial, 0.0f);

			const float eta = m_P.eta_hover +
				(m_P.eta_cruise - m_P.eta_hover) * std::clamp(airspeed / 20.0f, 0.0f, 1.0f);

			m_PowerW = pIdeal / std::max(eta, 0.2f) + m_P.base_load_w;
			m_UsedWh += m_PowerW * dt / 3600.0f;

			// OCV from state of charge over the USABLE window, minus IR sag.
			const float usable = m_P.capacity_wh * m_P.usable_frac;
			const float soc = std::clamp(1.0f - m_UsedWh / std::max(usable, 1.0f), 0.0f, 1.0f);
			const float ocv = m_P.cells * (m_P.v_empty_cell + (m_P.v_full_cell - m_P.v_empty_cell) * soc);
			m_I = m_PowerW / std::max(ocv, 1.0f);
			m_V = std::max(ocv - m_I * m_P.r_int_ohm, 0.0f);
		}

		// Fault injection: force the pack to a low state (failsafe testing).
		void ForceUsedWh(float wh) { m_UsedWh = wh; }

		float Voltage() const  { return m_V; }
		float Current() const  { return m_I; }
		float PowerW() const   { return m_PowerW; }
		float UsedWh() const   { return m_UsedWh; }
		float UsableWh() const { return m_P.capacity_wh * m_P.usable_frac; }

	private:
		BatteryParams m_P{};
		float m_UsedWh = 0.0f;
		float m_V = 16.8f;
		float m_I = 0.0f;
		float m_PowerW = 0.0f;
	};
}
