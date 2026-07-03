// TransitionScreen.cpp — see header.

#include "screens/TransitionScreen.h"

#include <algorithm>

namespace Viper
{
	using viperfc::FlightMode;
	using viperfc::TransitionPhase;

	TransitionScreen::TransitionScreen(SimHub& hub) : m_Hub(hub) {}

	void TransitionScreen::OnUpdate(float ts)
	{
		m_Clock += ts;

		const auto& t = m_Hub.FcTelem();
		const glm::vec3 e = Cosmic::Math::EulerZYXFromQuat(m_Hub.Truth().attNed);
		m_Hist.push_back({ m_Clock, t.blend, m_Hub.Truth().airspeed, m_Hub.AltitudeAgl(), e.y });
		if (m_Hist.size() > 5400)   // ~90 s
			m_Hist.erase(m_Hist.begin(), m_Hist.begin() + 900);
	}

	void TransitionScreen::OnImGuiRender()
	{
		ImGui::Begin("Transition");

		Cosmic::UI::Fonts::Push("Roboto-Bold", Cosmic::UI::Fonts::SizeHeading);
		ImGui::TextUnformatted("Transition (P4)");
		Cosmic::UI::Fonts::Pop();
		ImGui::TextDisabled("VTOL <-> cruise state machine — the project's core research risk, instrumented.");
		ImGui::Separator();

		DrawStateMachine();
		ImGui::Separator();

		// Manual transition drivers + the G2 gate.
		if (ImGui::Button("Transition -> Cruise")) m_Hub.RequestMode(FlightMode::Cruise);
		ImGui::SameLine();
		if (ImGui::Button("Transition -> Hover"))  m_Hub.RequestMode(FlightMode::Hover);
		ImGui::SameLine();
		if (m_Hub.ActiveScenario() == Scenario::None)
		{
			if (ImGui::Button("Run gate G2 (scripted round trip)"))
				m_Hub.StartScenario(Scenario::G2Transition);
		}
		else
			ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, "scenario running...");

		const ScenarioReport& r = m_Hub.Report();
		if (r.complete && r.scenario == Scenario::G2Transition)
		{
			ImGui::TextColored(r.passed ? ImVec4(0.3f, 0.9f, 0.4f, 1.0f)
			                            : ImVec4(1.0f, 0.35f, 0.3f, 1.0f),
				"%s", r.passed ? "G2 PASSED" : "G2 FAILED");
			for (const std::string& line : r.lines)
				ImGui::TextUnformatted(line.c_str());
			if (!r.recordingPath.empty())
				ImGui::TextDisabled("blend traces recorded: %s", r.recordingPath.c_str());
		}

		ImGui::Separator();
		DrawTraces();

		ImGui::End();
	}

	void TransitionScreen::DrawStateMachine()
	{
		const auto& t = m_Hub.FcTelem();

		struct Box { const char* name; bool active; };
		const bool inTrans = t.mode == FlightMode::Transition;
		const Box boxes[] = {
			{ "HOVER",  t.mode == FlightMode::Hover || t.mode == FlightMode::Failsafe },
			{ "ACCEL",  inTrans && t.phase == TransitionPhase::Accel },
			{ "BLEND",  inTrans && t.phase == TransitionPhase::Blend },
			{ "CRUISE", t.mode == FlightMode::Cruise || t.mode == FlightMode::Orbit },
			{ "DECEL",  inTrans && t.phase == TransitionPhase::Decel },
			{ "FLARE",  inTrans && t.phase == TransitionPhase::Flare },
		};

		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float bw = 92.0f, bh = 40.0f, gap = 26.0f;
		const ImU32 on   = ImGui::ColorConvertFloat4ToU32({ 0.20f, 0.55f, 0.95f, 1.0f });
		const ImU32 off  = ImGui::GetColorU32(ImGuiCol_Button);
		const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
		const ImU32 line = ImGui::GetColorU32(ImGuiCol_TextDisabled);

		// Forward row: HOVER -> ACCEL -> BLEND -> CRUISE; back row underneath.
		const int fwd[] = { 0, 1, 2, 3 };
		for (int i = 0; i < 4; ++i)
		{
			const Box& bx = boxes[fwd[i]];
			const ImVec2 p0{ origin.x + i * (bw + gap), origin.y };
			const ImVec2 p1{ p0.x + bw, p0.y + bh };
			dl->AddRectFilled(p0, p1, bx.active ? on : off, 6.0f);
			dl->AddRect(p0, p1, line, 6.0f);
			const ImVec2 ts = ImGui::CalcTextSize(bx.name);
			dl->AddText({ (p0.x + p1.x - ts.x) * 0.5f, (p0.y + p1.y - ts.y) * 0.5f }, text, bx.name);
			if (i < 3)
				dl->AddText({ p1.x + 6.0f, p0.y + bh * 0.5f - 8.0f }, line, "->");
		}
		// Back row (CRUISE -> DECEL -> FLARE -> HOVER).
		const int back[] = { 3, 4, 5, 0 };
		for (int i = 0; i < 4; ++i)
		{
			const Box& bx = boxes[back[i]];
			const ImVec2 p0{ origin.x + (3 - i) * (bw + gap), origin.y + bh + 18.0f };
			const ImVec2 p1{ p0.x + bw, p0.y + bh };
			if (i == 0 || i == 3)   // CRUISE/HOVER already drawn above — arrow lane only
			{
				if (i < 3)
					dl->AddText({ p0.x - 20.0f, p0.y + bh * 0.5f - 8.0f }, line, "<-");
				continue;
			}
			dl->AddRectFilled(p0, p1, bx.active ? on : off, 6.0f);
			dl->AddRect(p0, p1, line, 6.0f);
			const ImVec2 ts = ImGui::CalcTextSize(bx.name);
			dl->AddText({ (p0.x + p1.x - ts.x) * 0.5f, (p0.y + p1.y - ts.y) * 0.5f }, text, bx.name);
		}

		ImGui::Dummy(ImVec2(4 * (bw + gap), 2 * bh + 26.0f));

		ImGui::Text("blend %.2f   airspeed %.1f m/s (gate: %.0f..%.0f)   phase %s",
			t.blend, m_Hub.Truth().airspeed,
			m_Hub.LocalFc() ? m_Hub.LocalFc()->Params().trans_v_blend_lo : 8.0f,
			m_Hub.LocalFc() ? m_Hub.LocalFc()->Params().trans_v_blend_hi : 14.0f,
			viperfc::PhaseName(t.phase));
		ImGui::ProgressBar(t.blend, ImVec2(-1, 0), "hover  <-  blend  ->  cruise");
	}

	void TransitionScreen::DrawTraces()
	{
		if (m_Hist.size() < 2)
			return;

		const int n = static_cast<int>(m_Hist.size());
		if (ImPlot::BeginPlot("Blend / airspeed / altitude / pitch", ImVec2(-1, 300)))
		{
			ImPlot::SetupAxes("t (s)", "value");
			ImPlotSpec spec;
			spec.Stride = sizeof(Sample);   // struct-of-samples ring buffer
			ImPlot::PlotLine("blend",          &m_Hist[0].t, &m_Hist[0].blend,    n, spec);
			ImPlot::PlotLine("airspeed (m/s)", &m_Hist[0].t, &m_Hist[0].airspeed, n, spec);
			ImPlot::PlotLine("alt AGL (m)",    &m_Hist[0].t, &m_Hist[0].altAgl,   n, spec);
			ImPlot::PlotLine("pitch (deg)",    &m_Hist[0].t, &m_Hist[0].pitchDeg, n, spec);
			ImPlot::EndPlot();
		}
		if (ImGui::Button("Clear traces"))
			m_Hist.clear();
	}
}
