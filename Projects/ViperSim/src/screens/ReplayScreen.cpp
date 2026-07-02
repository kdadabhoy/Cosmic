// ReplayScreen.cpp — see header.

#include "screens/ReplayScreen.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace Viper
{
	// Truth channel indices (must match TruthChannels() order in the schema).
	namespace TruthCh { enum { PosN, PosE, PosD, VelN, VelE, VelD, Roll, Pitch, Yaw, Airspeed, Alpha, AltAgl }; }

	ReplayScreen::ReplayScreen(SimHub& hub) : m_Hub(hub) {}

	void ReplayScreen::OnAttach()
	{
		m_Body = Cosmic::Mesh::CreateBox({ 0.7f, 0.18f, 0.5f });
		m_Pad  = Cosmic::Mesh::CreatePlane(8.0f, 8.0f);

		m_Orbit.SetTarget({ 0.0f, 3.0f, 0.0f });
		m_Orbit.SetDistance(14.0f);
		m_Orbit.SetYawPitch(35.0f, 20.0f);

		Cosmic::Renderer3D::SetLightDirection({ -0.4f, -1.0f, -0.25f });
		Cosmic::Renderer3D::SetAmbient(0.28f);

		LoadLatest();
	}

	void ReplayScreen::OnDetach()
	{
		m_Body.reset();
		m_Pad.reset();
	}

	void ReplayScreen::LoadLatest()
	{
		// Prefer the in-session recording; fall back to the last flushed folder.
		std::string path = m_Hub.LastRecordingPath();
		if (path.empty())
			path = Cosmic::FileSystem::Resolve("user://recordings/viper_drop/session");

		m_Loaded = m_Hub.Player().Load(path);
		if (m_Loaded)
		{
			m_Hub.Player().SetPosition(0.0f);
			RebuildTrail();
			CS_INFO("ViperSim Replay: loaded '{}' ({:.2f} s).", path, m_Hub.Player().GetDuration());
		}
	}

	void ReplayScreen::RebuildTrail()
	{
		m_Trail.clear();
		if (!m_Loaded)
			return;

		const float dur = m_Hub.Player().GetDuration();
		Cosmic::TelemetryFrame f;
		for (float t = 0.0f; t <= dur; t += 0.02f)
		{
			if (m_Hub.Player().SampleAt("truth", t, f) && f.values.size() > TruthCh::PosD)
			{
				const glm::vec3 ned{ f.values[TruthCh::PosN], f.values[TruthCh::PosE], f.values[TruthCh::PosD] };
				m_Trail.push_back(Cosmic::Math::NedToRender(ned));
			}
		}
	}

	void ReplayScreen::OnUpdate(float ts)
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

		m_Hub.Player().Tick(ts);
		m_Orbit.OnUpdate(ts);

		// Airframe pose from the replayed truth frame.
		glm::vec3 posR{ 0.0f, 3.0f, 0.0f };
		glm::quat attR{ 1.0f, 0.0f, 0.0f, 0.0f };
		Cosmic::TelemetryFrame f;
		if (m_Loaded && m_Hub.Player().GetFrame("truth", f) && f.values.size() > TruthCh::Yaw)
		{
			const glm::vec3 ned{ f.values[TruthCh::PosN], f.values[TruthCh::PosE], f.values[TruthCh::PosD] };
			const glm::quat attNed = Cosmic::Math::QuatFromEulerZYX(
				{ f.values[TruthCh::Roll], f.values[TruthCh::Pitch], f.values[TruthCh::Yaw] });
			posR = Cosmic::Math::NedToRender(ned);
			attR = Cosmic::Math::NedQuatToRender(attNed);
		}

		Cosmic::Renderer3D::BeginScene(m_Orbit.GetCamera());
		Cosmic::Renderer3D::DrawGrid(24.0f, 1.0f,
			{ 0.17f, 0.18f, 0.21f, 1.0f }, { 0.30f, 0.32f, 0.36f, 1.0f }, 5);
		Cosmic::Renderer3D::DrawMesh(m_Pad,
			glm::translate(glm::mat4(1.0f), { 0.0f, 0.01f, 0.0f }), { 0.15f, 0.30f, 0.18f, 1.0f });
		if (m_Trail.size() >= 2)
			Cosmic::Renderer3D::DrawPolyline(m_Trail, { 0.35f, 0.75f, 0.95f, 1.0f });

		const glm::mat4 root = glm::translate(glm::mat4(1.0f), posR) * glm::mat4_cast(attR);
		Cosmic::Renderer3D::DrawMesh(m_Body, root, { 0.35f, 0.55f, 0.95f, 1.0f });
		Cosmic::Renderer3D::EndScene();
	}

	void ReplayScreen::OnImGuiRender()
	{
		ImGui::Begin("Project Inspector Top");

		Cosmic::UI::Fonts::Push("Roboto-Bold", Cosmic::UI::Fonts::SizeHeading);
		ImGui::TextUnformatted("Replay (P1)");
		Cosmic::UI::Fonts::Pop();
		ImGui::TextDisabled("DataPlayer drives the airframe from recorded truth.");
		ImGui::Separator();

		if (ImGui::Button("Load latest drop"))
			LoadLatest();

		if (!m_Loaded)
		{
			ImGui::TextColored({ 1.0f, 0.6f, 0.2f, 1.0f },
				"No recording loaded. Run a drop on the Flight screen, then Flush -> Replay.");
			ImGui::End();
			return;
		}

		auto& player = m_Hub.Player();

		if (ImGui::Button(player.IsPlaying() ? "Pause" : "Play"))
			player.IsPlaying() ? player.Pause() : player.Play();
		ImGui::SameLine();
		if (ImGui::Button("Restart")) { player.SetPosition(0.0f); player.Play(); }

		float pos = player.GetPosition();
		if (ImGui::SliderFloat("Scrub (s)", &pos, 0.0f, player.GetDuration(), "%.2f"))
			player.SetPosition(pos);

		float speed = player.GetSpeed();
		if (ImGui::SliderFloat("Speed", &speed, -2.0f, 2.0f, "%.2fx"))
			player.SetSpeed(speed);

		ImGui::Text("Duration: %.2f s", player.GetDuration());

		ImGui::End();
	}

	void ReplayScreen::OnEvent(Cosmic::Event& e)
	{
		m_Orbit.OnEvent(e);
	}
}
