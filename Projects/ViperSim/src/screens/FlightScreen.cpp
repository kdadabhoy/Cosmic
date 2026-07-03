// FlightScreen.cpp — see header.

#include "screens/FlightScreen.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstdio>

namespace Viper
{
	using viperfc::FlightMode;

	FlightScreen::FlightScreen(SimHub& hub) : m_Hub(hub) {}

	void FlightScreen::OnAttach()
	{
		BuildMeshes();
		m_Orbit.SetTarget({ 0.0f, 3.0f, 0.0f });
		m_Orbit.SetDistance(18.0f);
		m_Orbit.SetYawPitch(35.0f, 20.0f);

		Cosmic::Renderer3D::SetLightDirection({ -0.4f, -1.0f, -0.25f });
		Cosmic::Renderer3D::SetAmbient(0.28f);

		Cosmic::FramebufferSpecification fpvSpec;
		fpvSpec.Width = 384;
		fpvSpec.Height = 216;
		m_FpvFbo = Cosmic::FrameBuffer::Create(fpvSpec);
	}

	void FlightScreen::OnDetach()
	{
		m_Fuselage.reset();
		m_WingMesh.reset();
		m_Pad.reset();
		m_FpvFbo.reset();
	}

	void FlightScreen::BuildMeshes()
	{
		// Tailsitter stand-in in the body-render frame (identity attitude =
		// nose north = render -Z): fuselage along -Z, wing spanning +X/-X.
		m_Fuselage = Cosmic::Mesh::CreateBox({ 0.12f, 0.12f, 0.72f });
		m_WingMesh = Cosmic::Mesh::CreateBox({ 1.34f, 0.03f, 0.24f });
		m_Pad      = Cosmic::Mesh::CreatePlane(10.0f, 10.0f);
	}

	// =========================================================================
	// Rendering — FPV pass first (own FBO), then the main viewport pass.
	// =========================================================================

	void FlightScreen::DrawWorld()
	{
		Cosmic::Renderer3D::DrawGrid(60.0f, 2.0f,
			{ 0.17f, 0.18f, 0.21f, 1.0f }, { 0.30f, 0.32f, 0.36f, 1.0f }, 5);

		// Ground pad + home axes at the origin.
		Cosmic::Renderer3D::DrawMesh(m_Pad,
			glm::translate(glm::mat4(1.0f), { 0.0f, 0.01f, 0.0f }),
			{ 0.15f, 0.30f, 0.18f, 1.0f });
		Cosmic::Renderer3D::DrawAxes(glm::mat4(1.0f), 2.0f);

		// ROI marker (orbit target).
		const glm::vec3 roiR = Cosmic::Math::NedToRender(m_Hub.RoiNed());
		Cosmic::Renderer3D::DrawWireBox(
			glm::translate(glm::mat4(1.0f), roiR + glm::vec3(0.0f, 1.0f, 0.0f)) *
			glm::scale(glm::mat4(1.0f), { 2.0f, 2.0f, 2.0f }),
			{ 0.95f, 0.55f, 0.15f, 1.0f });

		if (m_Trail.size() >= 2)
			Cosmic::Renderer3D::DrawPolyline(m_Trail, { 0.95f, 0.75f, 0.20f, 1.0f });

		// Airframe under the NED->render transform.
		const RigidState& s = m_Hub.Truth();
		const glm::vec3 posR = Cosmic::Math::NedToRender(s.posNed);
		const glm::quat attR = Cosmic::Math::NedQuatToRender(s.attNed);
		const glm::mat4 root = glm::translate(glm::mat4(1.0f), posR) * glm::mat4_cast(attR);

		Cosmic::Renderer3D::DrawMesh(m_Fuselage, root, { 0.85f, 0.35f, 0.18f, 1.0f });
		Cosmic::Renderer3D::DrawMesh(m_WingMesh,
			root * glm::translate(glm::mat4(1.0f), { 0.0f, 0.0f, 0.10f }),
			{ 0.75f, 0.30f, 0.15f, 1.0f });
		Cosmic::Renderer3D::DrawAxes(root, 1.0f);
	}

	void FlightScreen::RenderFpvPass()
	{
		// Belly camera (v1 fixed camera + aircraft pointing, doc 04 §2.4.4):
		// in a banked orbit this face points at the ROI.
		const RigidState& s = m_Hub.Truth();
		const glm::vec3 bellyNed = s.attNed * glm::vec3(0, 0, 1);
		const glm::vec3 noseNed  = s.attNed * glm::vec3(1, 0, 0);
		const glm::vec3 eyeR    = Cosmic::Math::NedToRender(s.posNed + noseNed * 0.15f);
		const glm::vec3 targetR = Cosmic::Math::NedToRender(s.posNed + bellyNed * 25.0f);
		const glm::vec3 upR     = Cosmic::Math::NedToRender(noseNed);
		m_FpvCam.LookAt(eyeR, targetR, upR);

		m_FpvFbo->Bind();
		Cosmic::RenderCommand::SetViewport(0, 0, m_FpvFbo->GetWidth(), m_FpvFbo->GetHeight());
		Cosmic::RenderCommand::SetClearColor({ 0.35f, 0.50f, 0.72f, 1.0f });   // sky-ish
		Cosmic::RenderCommand::Clear();

		Cosmic::Renderer3D::BeginScene(m_FpvCam);
		DrawWorld();
		Cosmic::Renderer3D::EndScene();
		m_FpvFbo->Unbind();
	}

	void FlightScreen::RenderMainPass()
	{
		auto& app = Cosmic::Application::Get();
		auto fb = app.GetFrameBuffer();
		fb->Bind();
		Cosmic::RenderCommand::SetViewport(0, 0, fb->GetWidth(), fb->GetHeight());

		Cosmic::Renderer3D::BeginScene(m_Orbit.GetCamera());
		DrawWorld();
		Cosmic::Renderer3D::EndScene();
	}

	void FlightScreen::OnUpdate(float ts)
	{
		auto& app = Cosmic::Application::Get();
		auto fb = app.GetFrameBuffer();
		const float w = static_cast<float>(fb->GetWidth());
		const float h = static_cast<float>(fb->GetHeight());
		if ((m_ViewportSize.x != w || m_ViewportSize.y != h) && w > 0.0f && h > 0.0f)
		{
			m_ViewportSize = { w, h };
			m_Orbit.OnResize(w, h);
		}

		const glm::vec3 bodyR = Cosmic::Math::NedToRender(m_Hub.Truth().posNed);
		if (m_AutoFollow)
			m_Orbit.SetTarget(glm::mix(m_Orbit.GetTarget(), bodyR, std::min(ts * 4.0f, 1.0f)));
		m_Orbit.OnUpdate(ts);

		// Trajectory ribbon (S3.2 — ring-buffered polyline).
		if (m_Hub.IsRunning())
		{
			if (m_Trail.empty() || glm::distance(m_Trail.back(), bodyR) > 0.05f)
				m_Trail.push_back(bodyR);
			if (m_Trail.size() > 2500)
				m_Trail.erase(m_Trail.begin(), m_Trail.begin() + 500);
		}

		if (m_FpvEnabled)
			RenderFpvPass();
		RenderMainPass();
	}

	// =========================================================================
	// Inspector
	// =========================================================================

	void FlightScreen::OnImGuiRender()
	{
		ImGui::Begin("Project Inspector Top");

		Cosmic::UI::Fonts::Push("Roboto-Bold", Cosmic::UI::Fonts::SizeHeading);
		ImGui::TextUnformatted("Flight");
		Cosmic::UI::Fonts::Pop();

		DrawStatusBlock();
		ImGui::Separator();
		DrawFlightControls();
		ImGui::Separator();
		DrawEnvironmentAndFaults();
		ImGui::Separator();
		DrawBackendSection();
		ImGui::Separator();
		DrawGateSection();

		ImGui::Separator();
		if (ImGui::Button("Flush recording -> Replay"))
			m_Hub.FlushRecording();
		ImGui::SameLine();
		if (ImGui::Button("Reset to pad"))
		{
			m_Trail.clear();
			m_Hub.AbortScenario();
			m_Hub.ResetToPad();
		}

		// --- FPV inset (S3.1) --------------------------------------------------
		if (m_FpvEnabled && m_FpvFbo)
		{
			ImGui::Separator();
			ImGui::TextDisabled("FPV (belly camera — aircraft pointing)");
			const float aw = ImGui::GetContentRegionAvail().x;
			const ImVec2 size{ aw, aw * 9.0f / 16.0f };
			ImGui::Image((ImTextureID)(intptr_t)m_FpvFbo->GetColorAttachmentRendererID(),
				size, ImVec2(0, 1), ImVec2(1, 0));
		}

		ImGui::End();
	}

	void FlightScreen::DrawStatusBlock()
	{
		const auto& t = m_Hub.FcTelem();
		const RigidState& s = m_Hub.Truth();

		ImGui::Text("%s%s  blend %.2f  %s",
			viperfc::ModeName(t.mode),
			t.phase != viperfc::TransitionPhase::None ? "*" : "",
			t.blend, t.armed ? "[ARMED]" : "[disarmed]");
		if (t.phase != viperfc::TransitionPhase::None)
		{
			ImGui::SameLine();
			ImGui::TextColored({ 1.0f, 0.7f, 0.2f, 1.0f }, "%s", viperfc::PhaseName(t.phase));
		}

		ImGui::Text("AGL %5.1f m   V %4.1f m/s   P %5.0f W   %.1f Wh",
			m_Hub.AltitudeAgl(), s.airspeed, m_Hub.GetBattery().PowerW(), t.energyUsed_wh);
		ImGui::Text("hover budget %.0f / %.0f s   vbat %.1f V",
			t.hoverElapsed_s,
			m_Hub.LocalFc() ? m_Hub.LocalFc()->Params().hover_budget_s : 300.0f,
			m_Hub.GetBattery().Voltage());

		if (Cosmic::Input::IsGamepadConnected())
			ImGui::TextColored({ 0.3f, 0.9f, 0.4f, 1.0f }, "Gamepad: %s (E7 stick flying %s)",
				Cosmic::Input::GetGamepadName().c_str(), m_Hub.gamepadEnabled ? "ON" : "off");
		else
			ImGui::TextDisabled("No gamepad (UI buttons still fly it).");
	}

	void FlightScreen::DrawFlightControls()
	{
		const bool armed = m_Hub.Armed();

		if (ImGui::Button(armed ? "DISARM" : "ARM"))
			m_Hub.Arm(!armed);
		ImGui::SameLine();
		if (ImGui::Button("Takeoff"))
		{
			m_Trail.clear();
			m_Hub.Takeoff(m_TakeoffAlt);
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(90.0f);
		ImGui::SliderFloat("alt", &m_TakeoffAlt, 3.0f, 60.0f, "%.0f m");

		if (ImGui::Button("Hover"))  m_Hub.RequestMode(FlightMode::Hover);
		ImGui::SameLine();
		if (ImGui::Button("Cruise")) m_Hub.RequestMode(FlightMode::Cruise);
		ImGui::SameLine();
		if (ImGui::Button("Orbit"))  m_Hub.RequestMode(FlightMode::Orbit);
		ImGui::SameLine();
		if (ImGui::Button("RTL"))    m_Hub.RequestMode(FlightMode::Rtl);

		ImGui::SetNextItemWidth(160.0f);
		if (ImGui::InputFloat2("ROI N/E", &m_RoiField.x, "%.0f"))
			m_Hub.SetRoi({ m_RoiField.x, m_RoiField.y, 0.0f });
		ImGui::SameLine();
		ImGui::Checkbox("Pad flying", &m_Hub.gamepadEnabled);

		// Legacy P1 drop test lives on as a scenario.
		if (ImGui::TreeNode("Drop test (P1 regression)"))
		{
			ImGui::SliderFloat("Drop height (m)", &m_DropHeight, 1.0f, 20.0f, "%.1f");
			if (ImGui::Button("Drop"))
			{
				m_Trail.clear();
				m_Hub.StartDrop(m_DropHeight);
			}
			ImGui::TreePop();
		}
	}

	void FlightScreen::DrawEnvironmentAndFaults()
	{
		if (ImGui::CollapsingHeader("Wind & faults"))
		{
			auto& wind = m_Hub.Wind();
			ImGui::SliderFloat2("Steady N/E (m/s)", &wind.steadyNed.x, -10.0f, 10.0f, "%.1f");
			ImGui::SliderFloat("Gust sigma (m/s)", &wind.gustSigma, 0.0f, 5.0f, "%.1f");

			bool perfect = m_Hub.GetSensors().Params().perfect;
			if (ImGui::Checkbox("Perfect sensors", &perfect))
				m_Hub.GetSensors().SetPerfect(perfect);

			ImGui::TextDisabled("Fault injection (doc 04 \xC2\xA7""2.4.5):");
			ImGui::Checkbox("Kill link", &m_Hub.linkKilled);
			ImGui::SameLine();
			ImGui::Checkbox("Drop GPS", &m_Hub.Faults().gpsDrop);
			ImGui::SameLine();
			ImGui::Checkbox("Freeze pitot", &m_Hub.Faults().pitotFreeze);

			bool moR = m_Hub.MotorOut(0), moL = m_Hub.MotorOut(1);
			if (ImGui::Checkbox("Motor-out R", &moR)) m_Hub.SetMotorOut(0, moR);
			ImGui::SameLine();
			if (ImGui::Checkbox("Motor-out L", &moL)) m_Hub.SetMotorOut(1, moL);
			ImGui::SameLine();
			if (ImGui::Button("Battery -> 20%"))
				m_Hub.ForceBatteryLow();
		}
	}

	void FlightScreen::DrawBackendSection()
	{
		if (ImGui::CollapsingHeader("FC backend (SITL / HIL) + rig"))
		{
			int backend = m_Hub.UsingHil() ? 1 : 0;
			ImGui::TextDisabled("Where viper-fc runs:");
			if (ImGui::RadioButton("SITL (in-process)", backend == 0)) m_Hub.SetUseHil(false);
			ImGui::SameLine();
			if (ImGui::RadioButton("HIL (Teensy 4.1)", backend == 1)) m_Hub.SetUseHil(true);

			if (m_Hub.UsingHil())
			{
				m_Hub.Hil().Link().DrawConnectionUI();
				if (m_Hub.Backend().IsConnected())
					ImGui::TextColored({ 0.3f, 0.9f, 0.4f, 1.0f },
						"HIL round-trip latency: %.2f ms", m_Hub.Backend().LatencyMs());
				else
					ImGui::TextColored({ 1.0f, 0.6f, 0.2f, 1.0f },
						"Not connected — flash viper-fc/firmware first.");
			}

			ImGui::Separator();
			auto& rig = m_Hub.Rig();
			ImGui::Checkbox("Gimbal rig output (P7)", &rig.enabled);
			if (rig.enabled)
			{
				rig.Link().DrawConnectionUI();
				float maxRate = rig.MaxRateDps();
				if (ImGui::SliderFloat("Rate clamp (deg/s)", &maxRate, 10.0f, 360.0f, "%.0f"))
					rig.SetMaxRateDps(maxRate);
				const glm::vec3& e = rig.LastSentDeg();
				ImGui::Text("RIG,%.1f,%.1f,%.1f @ %.0f Hz%s",
					e.x, e.y, e.z, rig.sendRateHz,
					rig.Link().IsOpen() ? "" : "  (link closed)");
			}
		}
	}

	void FlightScreen::DrawGateSection()
	{
		if (ImGui::CollapsingHeader("Gate demos (playbook \xC2\xA7""5.2)", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const bool busy = m_Hub.ActiveScenario() != Scenario::None;
			if (busy)
			{
				ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, "Scenario running...");
				ImGui::SameLine();
				if (ImGui::Button("Abort"))
					m_Hub.AbortScenario();
			}
			else
			{
				if (ImGui::Button("Run G1 (hover vs gusts)"))
				{
					m_Trail.clear();
					m_Hub.StartScenario(Scenario::G1Hover);
				}
				ImGui::SameLine();
				if (ImGui::Button("Run G2 (transition)"))
				{
					m_Trail.clear();
					m_Hub.StartScenario(Scenario::G2Transition);
				}
				ImGui::SameLine();
				if (ImGui::Button("Run G3 (orbit+failsafe)"))
				{
					m_Trail.clear();
					m_Hub.StartScenario(Scenario::G3Failsafe);
				}
			}

			const ScenarioReport& r = m_Hub.Report();
			if (r.complete)
			{
				ImGui::TextColored(r.passed ? ImVec4(0.3f, 0.9f, 0.4f, 1.0f)
				                            : ImVec4(1.0f, 0.35f, 0.3f, 1.0f),
					"%s", r.passed ? "GATE PASSED" : "GATE FAILED");
				for (const std::string& line : r.lines)
					ImGui::TextUnformatted(line.c_str());
				if (!r.recordingPath.empty())
					ImGui::TextDisabled("recorded: %s", r.recordingPath.c_str());
			}

			// "Every failsafe path exercised" checklist (G3 wording).
			if (ImGui::TreeNode("Failsafe paths exercised this session"))
			{
				using A = viperfc::FcAlert;
				static const A kList[] = {
					A::HoverBudgetWarn, A::HoverBudgetHit, A::BatteryLow, A::BatteryCritical,
					A::BatteryReserve, A::LinkLost, A::GeofenceAlt, A::GeofenceRadius,
					A::EnvelopeAlpha, A::GpsLost };
				for (A a : kList)
				{
					const bool seen = (m_Hub.AlertsSeen() & (1 << static_cast<int>(a))) != 0;
					ImGui::TextColored(seen ? ImVec4(0.3f, 0.9f, 0.4f, 1.0f)
					                        : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
						"%s %s", seen ? "[x]" : "[ ]", viperfc::AlertName(a));
				}
				ImGui::TreePop();
			}
		}
	}

	void FlightScreen::OnEvent(Cosmic::Event& e)
	{
		m_Orbit.OnEvent(e);
	}
}
