// EnergyScreen.cpp — see header.

#include "screens/EnergyScreen.h"

#include <algorithm>
#include <cmath>

namespace Viper
{
	// Proposal model numbers (doc 04 §0/§2.5) — the comparison baseline.
	namespace Model
	{
		constexpr float kHoverW  = 230.0f;
		constexpr float kCruiseW = 106.0f;
		constexpr float kOrbitW  = 110.0f;   // ~45 mph orbit ≈ cruise-class power
	}

	EnergyScreen::EnergyScreen(SimHub& hub) : m_Hub(hub) {}

	void EnergyScreen::OnUpdate(float ts)
	{
		m_Clock += ts;

		const auto& t = m_Hub.FcTelem();
		const float p = m_Hub.GetBattery().PowerW();

		m_T.push_back(m_Clock);
		m_P.push_back(p);
		m_Blend.push_back(t.blend * 100.0f);   // % on the same axis
		if (m_T.size() > 7200)
		{
			m_T.erase(m_T.begin(), m_T.begin() + 1200);
			m_P.erase(m_P.begin(), m_P.begin() + 1200);
			m_Blend.erase(m_Blend.begin(), m_Blend.begin() + 1200);
		}

		// Per-regime buckets (airborne only, so pad idle doesn't pollute).
		if (t.armed && m_Hub.AltitudeAgl() > 1.0f)
		{
			if (t.blend < 0.5f)                              { m_Hover.powerSum += p;  ++m_Hover.count; }
			else if (t.mode == viperfc::FlightMode::Orbit)   { m_Orbit.powerSum += p;  ++m_Orbit.count; }
			else                                             { m_Cruise.powerSum += p; ++m_Cruise.count; }
		}
	}

	void EnergyScreen::OnImGuiRender()
	{
		ImGui::Begin("Energy");

		Cosmic::UI::Fonts::Push("Roboto-Bold", Cosmic::UI::Fonts::SizeHeading);
		ImGui::TextUnformatted("Energy & endurance (P3)");
		Cosmic::UI::Fonts::Pop();
		ImGui::TextDisabled("Model vs measured — divergence is a finding, not a bug (plan \xC2\xA7""2.5).");
		ImGui::Separator();

		// --- live pack state --------------------------------------------------
		const Battery& b = m_Hub.GetBattery();
		const auto& t = m_Hub.FcTelem();

		ImGui::Text("Pack: %.1f V   %.1f A   %5.0f W", b.Voltage(), b.Current(), b.PowerW());
		const float usable = b.UsableWh();
		ImGui::ProgressBar(1.0f - b.UsedWh() / std::max(usable, 1.0f), ImVec2(-1, 0));
		ImGui::Text("Used %.1f / %.1f Wh usable (100 Wh pack x %.0f%%)",
			b.UsedWh(), usable, b.Params().usable_frac * 100.0f);

		// Hover budget — the enforced 3–5 min cap (plan §2.4.5).
		const float budget = m_Hub.LocalFc() ? m_Hub.LocalFc()->Params().hover_budget_s : 300.0f;
		ImGui::Text("Hover budget: %.0f / %.0f s (cap ENFORCED in software)", t.hoverElapsed_s, budget);
		ImGui::ProgressBar(t.hoverElapsed_s / std::max(budget, 1.0f), ImVec2(-1, 0));

		ImGui::Separator();

		// --- measured vs model table ---------------------------------------------
		if (ImGui::BeginTable("power", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Regime");
			ImGui::TableSetupColumn("Proposal model");
			ImGui::TableSetupColumn("Sim measured");
			ImGui::TableSetupColumn("Delta");
			ImGui::TableHeadersRow();

			auto row = [](const char* name, float model, const RegimeStat& s)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted(name);
				ImGui::TableNextColumn(); ImGui::Text("%.0f W", model);
				ImGui::TableNextColumn();
				if (s.Measured()) ImGui::Text("%.0f W", s.Avg(model));
				else              ImGui::TextDisabled("fly it first");
				ImGui::TableNextColumn();
				if (s.Measured())
				{
					const float d = 100.0f * (s.Avg(model) - model) / model;
					ImGui::TextColored(std::fabs(d) < 15.0f
						? ImVec4(0.3f, 0.9f, 0.4f, 1.0f) : ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
						"%+.0f%%", d);
				}
				else ImGui::TextDisabled("-");
			};
			row("Hover",  Model::kHoverW,  m_Hover);
			row("Cruise", Model::kCruiseW, m_Cruise);
			row("Orbit",  Model::kOrbitW,  m_Orbit);
			ImGui::EndTable();
		}

		// --- power history -----------------------------------------------------------
		if (m_T.size() >= 2 && ImPlot::BeginPlot("Power draw", ImVec2(-1, 220)))
		{
			ImPlot::SetupAxes("t (s)", "W  /  blend %");
			ImPlot::PlotLine("power (W)", m_T.data(), m_P.data(), static_cast<int>(m_T.size()));
			ImPlot::PlotLine("blend (%)", m_T.data(), m_Blend.data(), static_cast<int>(m_T.size()));
			ImPlot::EndPlot();
		}

		ImGui::Separator();

		// --- mission endurance calculator (plan §2.5) --------------------------------
		ImGui::TextUnformatted("Mission profile (uses measured power when available):");
		ImGui::SliderFloat("hover (min)",  &m_MinHover,  0.0f, 6.0f,  "%.1f");
		ImGui::SliderFloat("cruise (min)", &m_MinCruise, 0.0f, 60.0f, "%.0f");
		ImGui::SliderFloat("orbit (min)",  &m_MinOrbit,  0.0f, 45.0f, "%.0f");

		const float pH = m_Hover.Avg(Model::kHoverW);
		const float pC = m_Cruise.Avg(Model::kCruiseW);
		const float pO = m_Orbit.Avg(Model::kOrbitW);
		const float needWh = (pH * m_MinHover + pC * m_MinCruise + pO * m_MinOrbit) / 60.0f;

		ImGui::Text("Mission needs %.1f Wh of %.1f usable  ->  %s",
			needWh, usable, needWh <= usable ? "FITS" : "DOES NOT FIT");
		if (needWh > 0.1f)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(margin %+.1f Wh)", usable - needWh);
		}

		ImGui::Text("All-cruise endurance: %.0f min (proposal: ~45+)  |  all-orbit: %.0f min (proposal: ~35)",
			usable / pC * 60.0f, usable / pO * 60.0f);
		ImGui::TextDisabled("Hover-only would be %.0f min — the 3-5 min budget exists for a reason.",
			usable / pH * 60.0f);

		ImGui::End();
	}
}
