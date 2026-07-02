// FlightScreen.cpp — see header.

#include "screens/FlightScreen.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace Viper
{
	FlightScreen::FlightScreen(SimHub& hub) : m_Hub(hub) {}

	void FlightScreen::OnAttach()
	{
		BuildMeshes();
		m_Orbit.SetTarget({ 0.0f, 3.0f, 0.0f });
		m_Orbit.SetDistance(14.0f);
		m_Orbit.SetYawPitch(35.0f, 20.0f);

		Cosmic::Renderer3D::SetLightDirection({ -0.4f, -1.0f, -0.25f });
		Cosmic::Renderer3D::SetAmbient(0.28f);
	}

	void FlightScreen::OnDetach()
	{
		m_Body.reset();
		m_Pad.reset();
	}

	void FlightScreen::BuildMeshes()
	{
		// Airframe stand-in: a flat-ish box roughly the Viper's footprint.
		m_Body = Cosmic::Mesh::CreateBox({ 0.7f, 0.18f, 0.5f });
		m_Pad  = Cosmic::Mesh::CreatePlane(8.0f, 8.0f);
	}

	void FlightScreen::OnUpdate(float ts)
	{
		auto& app = Cosmic::Application::Get();

		// Viewport aspect sync.
		auto fb = app.GetFrameBuffer();
		const float w = static_cast<float>(fb->GetWidth());
		const float h = static_cast<float>(fb->GetHeight());
		if ((m_ViewportSize.x != w || m_ViewportSize.y != h) && w > 0.0f && h > 0.0f)
		{
			m_ViewportSize = { w, h };
			m_Orbit.OnResize(w, h);
		}

		// Track the falling body with the camera target (render frame).
		const glm::vec3 bodyR = Cosmic::Math::NedToRender(m_Hub.Truth().posNed);
		if (m_AutoFollow)
			m_Orbit.SetTarget(glm::mix(m_Orbit.GetTarget(), bodyR, std::min(ts * 4.0f, 1.0f)));
		m_Orbit.OnUpdate(ts);

		// Fall-path ribbon.
		if (m_Hub.IsRunning())
		{
			if (m_Trail.empty() || glm::distance(m_Trail.back(), bodyR) > 0.02f)
				m_Trail.push_back(bodyR);
			if (m_Trail.size() > 800) m_Trail.erase(m_Trail.begin());
		}

		RenderScene();
	}

	void FlightScreen::RenderScene()
	{
		Cosmic::Renderer3D::BeginScene(m_Orbit.GetCamera());

		Cosmic::Renderer3D::DrawGrid(24.0f, 1.0f,
			{ 0.17f, 0.18f, 0.21f, 1.0f }, { 0.30f, 0.32f, 0.36f, 1.0f }, 5);
		Cosmic::Renderer3D::DrawAxes(glm::mat4(1.0f), 2.0f);

		// Ground pad at y=0 (render up), matching the sim's ground plane.
		Cosmic::Renderer3D::DrawMesh(m_Pad,
			glm::translate(glm::mat4(1.0f), { 0.0f, 0.01f, 0.0f }),
			{ 0.15f, 0.30f, 0.18f, 1.0f });

		if (m_Trail.size() >= 2)
			Cosmic::Renderer3D::DrawPolyline(m_Trail, { 0.95f, 0.75f, 0.20f, 1.0f });

		// Airframe under the NED->render transform.
		const glm::vec3 posR = Cosmic::Math::NedToRender(m_Hub.Truth().posNed);
		const glm::quat attR = Cosmic::Math::NedQuatToRender(m_Hub.Truth().attNed);
		const glm::mat4 root = glm::translate(glm::mat4(1.0f), posR) * glm::mat4_cast(attR);

		Cosmic::Renderer3D::DrawMesh(m_Body, root, { 0.85f, 0.35f, 0.18f, 1.0f });
		Cosmic::Renderer3D::DrawAxes(root, 1.0f);

		Cosmic::Renderer3D::EndScene();
	}

	void FlightScreen::OnImGuiRender()
	{
		ImGui::Begin("Project Inspector Top");

		Cosmic::UI::Fonts::Push("Roboto-Bold", Cosmic::UI::Fonts::SizeHeading);
		ImGui::TextUnformatted("Flight — Drop Test (P1)");
		Cosmic::UI::Fonts::Pop();
		ImGui::TextDisabled("IDynamics + ComposableDynamics (E11 RK4); recorded for Replay.");
		ImGui::Separator();

		ImGui::SliderFloat("Drop height (m)", &m_DropHeight, 1.0f, 20.0f, "%.1f");
		if (ImGui::Button("Drop"))
		{
			m_Trail.clear();
			m_Hub.StartDrop(m_DropHeight);
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset"))
		{
			m_Trail.clear();
			m_Hub.ResetDrop();
		}
		ImGui::SameLine();
		if (ImGui::Button("Flush -> Replay"))
			m_Hub.FlushRecording();

		ImGui::Checkbox("Camera follows body", &m_AutoFollow);

		ImGui::Separator();
		const RigidState& s = m_Hub.Truth();
		const glm::vec3 e = Cosmic::Math::EulerZYXFromQuat(s.attNed);
		ImGui::Text("t = %.2f s   %s", m_Hub.RunTime(), m_Hub.IsRunning() ? "[running]" : "[settled]");
		ImGui::Text("Altitude AGL : %6.3f m", m_Hub.AltitudeAgl());
		ImGui::Text("Vertical vel : %6.3f m/s", s.velNed.z);
		ImGui::Text("NED pos      : N %.2f  E %.2f  D %.2f", s.posNed.x, s.posNed.y, s.posNed.z);
		ImGui::Text("Attitude     : R %.1f  P %.1f  Y %.1f", e.x, e.y, e.z);

		if (auto cfg = m_Hub.GetConfig())
			ImGui::TextDisabled("config: %s", cfg->GetSource().c_str());

		ImGui::End();
	}

	void FlightScreen::OnEvent(Cosmic::Event& e)
	{
		m_Orbit.OnEvent(e);
	}
}
