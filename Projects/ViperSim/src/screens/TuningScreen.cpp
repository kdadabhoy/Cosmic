// TuningScreen.cpp — see header.

#include "screens/TuningScreen.h"

#include <algorithm>
#include <cstdio>

namespace Viper
{
	TuningScreen::TuningScreen(SimHub& hub) : m_Hub(hub) {}

	void TuningScreen::OnUpdate(float ts)
	{
		m_Clock += ts;

		const auto& t = m_Hub.FcTelem();
		const glm::vec3 e = Cosmic::Math::EulerZYXFromQuat(m_Hub.Truth().attNed);

		m_Hist.push_back({ m_Clock, glm::degrees(t.attErrRad), e.x, e.y, e.z,
		                   t.motor[0], t.motor[1], t.servo[0], t.servo[1] });
		if (m_Hist.size() > 3600)   // ~60 s at 60 fps
			m_Hist.erase(m_Hist.begin(), m_Hist.begin() + 600);
	}

	void TuningScreen::OnImGuiRender()
	{
		ImGui::Begin("Tuning");

		Cosmic::UI::Fonts::Push("Roboto-Bold", Cosmic::UI::Fonts::SizeHeading);
		ImGui::TextUnformatted("Tuning (P2)");
		Cosmic::UI::Fonts::Pop();
		ImGui::TextDisabled("Live gains -> viper-fc; versioned values belong in viper.toml [fc] (playbook \xC2\xA7""7).");
		ImGui::Separator();

		DrawGains();
		ImGui::Separator();
		DrawStepResponse();
		ImGui::Separator();
		DrawReplayThroughFc();

		ImGui::End();
	}

	void TuningScreen::DrawGains()
	{
		viperfc::FlightComputer* fc = m_Hub.LocalFc();
		if (!fc)
		{
			ImGui::TextColored({ 1.0f, 0.6f, 0.2f, 1.0f },
				"HIL backend active — gains live on the Teensy, not here.");
			return;
		}

		viperfc::FcParams& p = fc->Params();
		bool dirty = false;

		if (ImGui::CollapsingHeader("Attitude / rate loop", ImGuiTreeNodeFlags_DefaultOpen))
		{
			dirty |= ImGui::SliderFloat("att_kp (rad->rad/s)", &p.att_kp, 1.0f, 15.0f, "%.1f");
			dirty |= ImGui::SliderFloat("rate_max (rad/s)",    &p.rate_max_rads, 1.0f, 8.0f, "%.1f");
			ImGui::TextDisabled("rate PIDs (x=roll about nose, y=pitch, z=yaw/tip):");
			dirty |= ImGui::SliderFloat("kp x", &p.rate_kp_x, 0.0f, 0.5f, "%.3f");
			ImGui::SameLine(); dirty |= ImGui::SliderFloat("ki x", &p.rate_ki_x, 0.0f, 0.3f, "%.3f");
			ImGui::SameLine(); dirty |= ImGui::SliderFloat("kd x", &p.rate_kd_x, 0.0f, 0.02f, "%.4f");
			dirty |= ImGui::SliderFloat("kp y", &p.rate_kp_y, 0.0f, 0.5f, "%.3f");
			ImGui::SameLine(); dirty |= ImGui::SliderFloat("ki y", &p.rate_ki_y, 0.0f, 0.3f, "%.3f");
			ImGui::SameLine(); dirty |= ImGui::SliderFloat("kd y", &p.rate_kd_y, 0.0f, 0.02f, "%.4f");
			dirty |= ImGui::SliderFloat("kp z", &p.rate_kp_z, 0.0f, 0.5f, "%.3f");
			ImGui::SameLine(); dirty |= ImGui::SliderFloat("ki z", &p.rate_ki_z, 0.0f, 0.3f, "%.3f");
		}

		if (ImGui::CollapsingHeader("Position / velocity loop"))
		{
			dirty |= ImGui::SliderFloat("pos_kp", &p.pos_kp, 0.1f, 3.0f, "%.2f");
			dirty |= ImGui::SliderFloat("vel_kp", &p.vel_kp, 0.5f, 6.0f, "%.2f");
			dirty |= ImGui::SliderFloat("vel_ki", &p.vel_ki, 0.0f, 2.0f, "%.2f");
			dirty |= ImGui::SliderFloat("tilt_max (rad)", &p.tilt_max_rad, 0.1f, 0.8f, "%.2f");
		}

		if (dirty)
			fc->ApplyParams();
	}

	void TuningScreen::DrawStepResponse()
	{
		ImGui::TextDisabled("Step commands (hover first: Flight screen -> Takeoff):");
		ImGui::SetNextItemWidth(110.0f);
		ImGui::SliderFloat("roll step (deg)", &m_StepRollDeg, -45.0f, 45.0f, "%.0f");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(110.0f);
		ImGui::SliderFloat("pitch offset (deg)", &m_StepPitchDeg, -30.0f, 30.0f, "%.0f");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(90.0f);
		ImGui::SliderFloat("hold (s)", &m_StepHold, 0.5f, 8.0f, "%.1f");

		viperfc::FlightComputer* fc = m_Hub.LocalFc();
		const bool canStep = fc && m_Hub.Armed() && m_Hub.FcMode() == viperfc::FlightMode::Hover;
		if (!canStep)
			ImGui::TextColored({ 1.0f, 0.6f, 0.2f, 1.0f }, "Arm + hover to enable steps.");
		else if (ImGui::Button("Apply attitude step (P2 acceptance: 20\xC2\xB0 roll)"))
			fc->CommandAttitudeStep(m_StepRollDeg, m_StepPitchDeg, m_StepHold);

		if (m_Hist.size() >= 2 && ImPlot::BeginPlot("Step response", ImVec2(-1, 260)))
		{
			ImPlot::SetupAxes("t (s)", "deg / cmd");
			const int n = static_cast<int>(m_Hist.size());
			ImPlotSpec spec;
			spec.Stride = sizeof(Sample);   // struct-of-samples ring buffer
			ImPlot::PlotLine("att err (deg)", &m_Hist[0].t, &m_Hist[0].attErrDeg, n, spec);
			ImPlot::PlotLine("roll (deg)",    &m_Hist[0].t, &m_Hist[0].rollDeg,   n, spec);
			ImPlot::PlotLine("pitch (deg)",   &m_Hist[0].t, &m_Hist[0].pitchDeg,  n, spec);
			ImPlot::PlotLine("motor R",       &m_Hist[0].t, &m_Hist[0].motorR,    n, spec);
			ImPlot::PlotLine("motor L",       &m_Hist[0].t, &m_Hist[0].motorL,    n, spec);
			ImPlot::EndPlot();
		}
		if (ImGui::Button("Clear trace"))
			m_Hist.clear();
	}

	// =========================================================================
	// Replay-through-FC (offline regression; doc 04 §1)
	// =========================================================================

	void TuningScreen::RunReplayThroughFc()
	{
		m_ReplayRan = false;

		std::string path = m_Hub.LastRecordingPath();
		if (path.empty())
			path = Cosmic::FileSystem::Resolve("user://recordings/viper_session/session");

		Cosmic::DataPlayer player;
		if (!player.Load(path))
		{
			CS_WARN("Replay-through-FC: no recording at '{}'.", path);
			return;
		}

		// Fresh FC with the CURRENT gains; recorded fc channels are the baseline.
		viperfc::FlightComputer fc(m_Hub.LocalFc() ? m_Hub.LocalFc()->Params() : viperfc::FcParams{});
		fc.Reset();
		fc.Arm(true);

		const float dt = 1.0f / 60.0f;   // sensors were recorded at tick rate
		const float dur = player.GetDuration();

		m_ReplayT.clear(); m_ReplayLiveM0.clear(); m_ReplayOffM0.clear();
		m_ReplayMaxMotorDiff = m_ReplayMaxServoDiff = 0.0f;
		m_ReplayFrames = 0;

		Cosmic::TelemetryFrame sen, fcRec;
		for (float t = 0.0f; t <= dur; t += dt)
		{
			if (!player.SampleAt("sensors", t, sen) ||
			    sen.values.size() < static_cast<size_t>(kSensorChannelCount))
				continue;

			const viperfc::SensorFrame frame = ReadSensorRow(sen.values, t);
			viperfc::ActuatorFrame out{};
			fc.Step(frame, out, dt);
			++m_ReplayFrames;

			if (player.SampleAt("fc", t, fcRec) && fcRec.values.size() >= 18)
			{
				// fc channel order: ... motor_r=14, motor_l=15, servo_r=16, servo_l=17
				m_ReplayMaxMotorDiff = std::max({ m_ReplayMaxMotorDiff,
					std::fabs(out.motor[0] - fcRec.values[14]),
					std::fabs(out.motor[1] - fcRec.values[15]) });
				m_ReplayMaxServoDiff = std::max({ m_ReplayMaxServoDiff,
					std::fabs(out.servo[0] - fcRec.values[16]),
					std::fabs(out.servo[1] - fcRec.values[17]) });

				m_ReplayT.push_back(t);
				m_ReplayLiveM0.push_back(fcRec.values[14]);
				m_ReplayOffM0.push_back(out.motor[0]);
			}
		}

		m_ReplayRan = m_ReplayFrames > 0;
		CS_INFO("Replay-through-FC: {} frames, max motor diff {:.3f}, max servo diff {:.3f}.",
			m_ReplayFrames, m_ReplayMaxMotorDiff, m_ReplayMaxServoDiff);
	}

	void TuningScreen::DrawReplayThroughFc()
	{
		if (!ImGui::CollapsingHeader("Replay-through-FC (offline regression)"))
			return;

		ImGui::TextWrapped(
			"Streams the last recording's raw SensorFrames through a fresh viper-fc "
			"instance with the CURRENT gains and diffs its outputs against what flew. "
			"Mode/stick commands are not in the stream, so expect divergence after "
			"pilot inputs — identical gains on an autonomous segment should match.");

		if (ImGui::Button("Run on last recording"))
			RunReplayThroughFc();

		if (m_ReplayRan)
		{
			ImGui::Text("%d frames   max motor diff %.3f   max servo diff %.3f",
				m_ReplayFrames, m_ReplayMaxMotorDiff, m_ReplayMaxServoDiff);

			if (!m_ReplayT.empty() && ImPlot::BeginPlot("motor R: recorded vs offline", ImVec2(-1, 200)))
			{
				ImPlot::SetupAxes("t (s)", "cmd");
				ImPlot::PlotLine("recorded", m_ReplayT.data(), m_ReplayLiveM0.data(),
					static_cast<int>(m_ReplayT.size()));
				ImPlot::PlotLine("offline",  m_ReplayT.data(), m_ReplayOffM0.data(),
					static_cast<int>(m_ReplayT.size()));
				ImPlot::EndPlot();
			}
		}
	}
}
