// Engine3DDemo.cpp
// See Engine3DDemo.h for what this app proves.

#include "Engine3DDemo.h"

#include "layers/WorkspaceLayer.h"   // dock-port registration (Application.h only forward-declares it)

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Workspace
{
	// =========================================================================
	// Lifecycle
	// =========================================================================

	Engine3DDemo::Engine3DDemo()
		: Cosmic::Layer("Engine3DDemo")
	{
	}

	void Engine3DDemo::OnAttach()
	{
		CS_INFO("Engine3DDemo attached — Phase 4 acceptance scene.");

		// Left-column inspector panel (engine dock slot by window NAME).
		if (auto* ws = Cosmic::Application::Get().GetWorkspaceLayer())
			ws->DockWindow("Project Inspector Top", Cosmic::DockPort::LeftTop);

		BuildAircraftMeshes();

		// Frame the action: look slightly down at the orbit circle.
		m_Orbit.SetTarget({ 0.0f, 4.0f, 0.0f });
		m_Orbit.SetDistance(18.0f);
		m_Orbit.SetYawPitch(35.0f, 25.0f);

		// Push the default lighting state into the renderer.
		Cosmic::Renderer3D::SetLightDirection(m_LightDir);
		Cosmic::Renderer3D::SetAmbient(m_Ambient);

		m_FixedHzUi = Cosmic::Application::Get().GetFixedTimestepHz();
	}

	void Engine3DDemo::OnDetach()
	{
		// Free GPU meshes while the GL context is live (README §24 client-dev rule).
		m_Fuselage.reset();
		m_Nose.reset();
		m_Canopy.reset();
		m_Wing.reset();
		m_Tailplane.reset();
		m_Fin.reset();
		m_Pod.reset();
		m_Pad.reset();

		// Leave the engine tick rate the way we found the machine.
		Cosmic::Application::Get().SetFixedTimestepHz(60.0f);
	}

	// =========================================================================
	// Placeholder aircraft (S2 primitives) — model frame: nose -Z, up +Y
	// =========================================================================

	void Engine3DDemo::BuildAircraftMeshes()
	{
		m_Fuselage  = Cosmic::Mesh::CreateCylinder(0.18f, 2.4f, 24);
		m_Nose      = Cosmic::Mesh::CreateCone(0.18f, 0.6f, 24);
		m_Canopy    = Cosmic::Mesh::CreateUVSphere(0.16f, 12, 18);
		m_Wing      = Cosmic::Mesh::CreateBox({ 3.2f, 0.06f, 0.55f });
		m_Tailplane = Cosmic::Mesh::CreateBox({ 1.2f, 0.05f, 0.32f });
		m_Fin       = Cosmic::Mesh::CreateBox({ 0.05f, 0.55f, 0.35f });
		m_Pod       = Cosmic::Mesh::CreateCylinder(0.09f, 0.5f, 16);
		m_Pad       = Cosmic::Mesh::CreatePlane(4.0f, 4.0f);
	}

	void Engine3DDemo::DrawAircraft()
	{
		namespace Math = Cosmic::Math;

		// NED sim state -> render frame (E3). The model is authored nose -Z /
		// up +Y / right +X, which is exactly what NedQuatToRender maps the NED
		// body axes (fwd/right/down) onto — see Spatial.h.
		const glm::vec3 posR = Math::NedToRender(m_PosNed);
		const glm::quat attR = Math::NedQuatToRender(m_AttNed);

		const glm::mat4 root = glm::translate(glm::mat4(1.0f), posR) * glm::mat4_cast(attR);

		const glm::vec4 hull  { 0.78f, 0.80f, 0.84f, 1.0f };
		const glm::vec4 accent{ 0.92f, 0.34f, 0.18f, 1.0f };
		const glm::vec4 dark  { 0.22f, 0.24f, 0.28f, 1.0f };
		const glm::vec4 glass { 0.35f, 0.62f, 0.80f, 1.0f };

		auto part = [&](const Cosmic::Ref<Cosmic::Mesh>& mesh, const glm::mat4& local, const glm::vec4& color)
		{
			Cosmic::Renderer3D::DrawMesh(mesh, root * local, color);
		};

		// Fuselage: engine cylinders stand along +Y — pitch 90° about X lays it along Z.
		const glm::mat4 alongZ = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), { 1, 0, 0 });
		part(m_Fuselage, alongZ, hull);

		// Nose cone: cone apex points +Y; rotate so the apex faces -Z, then push forward.
		part(m_Nose,
			glm::translate(glm::mat4(1.0f), { 0.0f, 0.0f, -1.5f }) *
			glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), { 1, 0, 0 }),
			accent);

		// Canopy bump above the forward fuselage.
		part(m_Canopy, glm::translate(glm::mat4(1.0f), { 0.0f, 0.16f, -0.55f }), glass);

		// Main wing through the fuselage midpoint.
		part(m_Wing, glm::translate(glm::mat4(1.0f), { 0.0f, 0.0f, -0.10f }), accent);

		// Tailplane + vertical fin at the rear.
		part(m_Tailplane, glm::translate(glm::mat4(1.0f), { 0.0f, 0.0f, 1.05f }), hull);
		part(m_Fin, glm::translate(glm::mat4(1.0f), { 0.0f, 0.28f, 1.05f }), accent);

		// Wingtip motor pods (VTOL tailsitter flavor) — cylinders laid along Z.
		for (float side : { -1.0f, 1.0f })
		{
			part(m_Pod,
				glm::translate(glm::mat4(1.0f), { side * 1.55f, 0.0f, -0.10f }) * alongZ,
				dark);
		}

		// Body axes + optional bounds, drawn under the same transform.
		if (m_ShowAxes)
			Cosmic::Renderer3D::DrawAxes(root, 1.2f);
		if (m_ShowWireBox)
			Cosmic::Renderer3D::DrawWireBox(
				root * glm::scale(glm::mat4(1.0f), { 3.4f, 1.0f, 3.2f }),
				{ 1.0f, 0.9f, 0.2f, 1.0f });
	}

	// =========================================================================
	// Simulation (E1 fixed step + E3 quaternion kinematics, all in NED)
	// =========================================================================

	void Engine3DDemo::OnFixedUpdate(float dt)
	{
		// E1 instrumentation: count ticks; OnUpdate closes the 1 s window.
		m_FixedTickCounter++;

		if (m_SimPaused || dt == 0.0f)
			return;

		namespace Math = Cosmic::Math;

		// Constant-bank coordinated turn: yaw rate = g * tan(bank) / V.
		const float bankRad = glm::radians(m_BankDeg);
		const float yawRate = Math::GravityMss * std::tan(bankRad) / std::max(m_SpeedMs, 0.1f);

		// Body rates for a coordinated turn (mostly yaw, expressed in body axes).
		const glm::vec3 omegaBody =
			glm::inverse(m_AttNed) * glm::vec3(0.0f, 0.0f, yawRate);

		m_AttNed = Math::IntegrateBodyRate(m_AttNed, omegaBody, dt);

		// Hold the commanded bank/pitch exactly (kinematic demo, not dynamics):
		// rebuild attitude from the integrated heading so error cannot accumulate.
		const glm::vec3 euler = Math::EulerZYXFromQuat(m_AttNed);
		m_AttNed = Math::QuatFromEulerZYX({ m_BankDeg, 0.0f, euler.z });

		// Translate along body-forward.
		const glm::vec3 velNed = m_AttNed * glm::vec3(m_SpeedMs, 0.0f, 0.0f);
		m_PosNed += velNed * dt;

		// Keep altitude locked for a clean circle (D = -altitude).
		m_PosNed.z = -6.0f;
	}

	// =========================================================================
	// Frame update + render passes
	// =========================================================================

	void Engine3DDemo::OnUpdate(float ts)
	{
		auto& app = Cosmic::Application::Get();

		// ---- Viewport/FBO size sync (drives the perspective aspect) ----
		auto fb = app.GetFrameBuffer();
		const float w = static_cast<float>(fb->GetWidth());
		const float h = static_cast<float>(fb->GetHeight());
		if ((m_ViewportSize.x != w || m_ViewportSize.y != h) && w > 0.0f && h > 0.0f)
		{
			m_ViewportSize = { w, h };
			m_Orbit.OnResize(w, h);
		}

		// ---- E1 measurement window (GetAbsoluteTime is real, unscaled seconds) ----
		const float now = app.GetAbsoluteTime();
		if (m_WindowStartTime < 0.0f)
			m_WindowStartTime = now;
		if (now - m_WindowStartTime >= 1.0f)
		{
			m_MeasuredFixedHz  = static_cast<float>(m_FixedTickCounter) / (now - m_WindowStartTime);
			m_FixedTickCounter = 0;
			m_WindowStartTime  = now;
		}

		// ---- Camera: user drag always wins; auto-orbit adds a slow yaw drift ----
		if (m_AutoOrbit && !Cosmic::Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_LEFT))
			m_Orbit.SetYawPitch(m_Orbit.GetYaw() + m_AutoOrbitSpeed * ts, m_Orbit.GetPitch());
		m_Orbit.OnUpdate(ts);

		// ---- Trajectory ribbon (render-frame samples, ~20 Hz) ----
		m_TrailTimer += ts;
		if (!m_SimPaused && m_TrailTimer >= 0.05f)
		{
			m_TrailTimer = 0.0f;
			m_Trail.push_back(Cosmic::Math::NedToRender(m_PosNed));
			if (m_Trail.size() > k_TrailMax)
				m_Trail.erase(m_Trail.begin());   // 600 points; fine to shift
		}

		// =====================================================================
		// 3D pass (S1 + S2) — the viewport FBO is already bound and cleared
		// by the engine's WorkspaceLayer before client layers update.
		// =====================================================================
		Cosmic::Renderer3D::BeginScene(m_Orbit.GetCamera());

		if (m_ShowGrid)
		{
			Cosmic::Renderer3D::DrawGrid(24.0f, 1.0f,
				{ 0.18f, 0.19f, 0.22f, 1.0f },   // minor
				{ 0.32f, 0.34f, 0.38f, 1.0f },   // major
				5);
			Cosmic::Renderer3D::DrawAxes(glm::mat4(1.0f), 2.0f);   // world origin tripod
		}

		// Landing pad mesh under the orbit center (proves CreatePlane + Lambert
		// on a big flat face).
		Cosmic::Renderer3D::DrawMesh(m_Pad,
			glm::translate(glm::mat4(1.0f), { 0.0f, 0.01f, 0.0f }),
			{ 0.16f, 0.35f, 0.20f, 1.0f });

		if (m_ShowTrail && m_Trail.size() >= 2)
			Cosmic::Renderer3D::DrawPolyline(m_Trail, { 0.95f, 0.75f, 0.20f, 1.0f });

		DrawAircraft();

		Cosmic::Renderer3D::EndScene();

		// =====================================================================
		// 2D overlay pass — no 2D regressions (doc 05 contract rule 6).
		// =====================================================================
		if (m_Show2D)
			Draw2DOverlay();
	}

	void Engine3DDemo::Draw2DOverlay()
	{
		// A fixed ortho HUD frame drawn over the 3D world: corner brackets +
		// a subtle bottom banner. If these render, the 2D pipeline is healthy
		// in the same frame as the 3D pass.
		static Cosmic::OrthographicCamera hudCam(-8.0f, 8.0f, -4.5f, 4.5f);

		Cosmic::Renderer2D::BeginScene(hudCam);

		const glm::vec4 hud{ 0.55f, 0.95f, 0.75f, 0.85f };
		const float x = 7.5f, y = 4.0f, len = 0.6f;

		// Corner brackets (8 lines).
		for (float sx : { -1.0f, 1.0f })
		{
			for (float sy : { -1.0f, 1.0f })
			{
				Cosmic::Renderer2D::DrawLine({ sx * x, sy * y, 0.1f }, { sx * (x - len), sy * y, 0.1f }, hud);
				Cosmic::Renderer2D::DrawLine({ sx * x, sy * y, 0.1f }, { sx * x, sy * (y - len), 0.1f }, hud);
			}
		}

		// Bottom status banner.
		Cosmic::Renderer2D::DrawQuad(glm::vec3{ 0.0f, -4.2f, 0.1f }, glm::vec2{ 6.0f, 0.35f },
			glm::vec4{ 0.10f, 0.12f, 0.14f, 0.55f });

		Cosmic::Renderer2D::EndScene();
	}

	// =========================================================================
	// Inspector UI
	// =========================================================================

	void Engine3DDemo::OnImGuiRender()
	{
		ImGui::Begin("Project Inspector Top");

		Cosmic::UI::Fonts::Push("Roboto-Bold", Cosmic::UI::Fonts::SizeHeading);
		ImGui::TextUnformatted("3D Engine Demo");
		Cosmic::UI::Fonts::Pop();
		ImGui::TextDisabled("Phase 4 acceptance: orbit camera + shaded aircraft over a grid");
		ImGui::Separator();

		// ---------------- Camera (S1) ----------------
		if (ImGui::CollapsingHeader("Camera  (LMB orbit / RMB pan / scroll zoom)", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Auto-orbit", &m_AutoOrbit);
			if (m_AutoOrbit)
				ImGui::SliderFloat("Orbit speed (deg/s)", &m_AutoOrbitSpeed, 1.0f, 60.0f, "%.0f");

			ImGui::Text("Yaw %.1f  Pitch %.1f  Dist %.1f",
				m_Orbit.GetYaw(), m_Orbit.GetPitch(), m_Orbit.GetDistance());

			if (ImGui::Button("Reset view"))
			{
				m_Orbit.SetTarget({ 0.0f, 4.0f, 0.0f });
				m_Orbit.SetDistance(18.0f);
				m_Orbit.SetYawPitch(35.0f, 25.0f);
			}
		}

		// ---------------- Simulation (E3) ----------------
		if (ImGui::CollapsingHeader("Simulation  (NED + quaternions, Spatial.h)", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Pause", &m_SimPaused);
			ImGui::SliderFloat("Speed (m/s)", &m_SpeedMs, 2.0f, 20.0f, "%.1f");
			ImGui::SliderFloat("Bank (deg)", &m_BankDeg, -45.0f, 45.0f, "%.0f");

			const glm::vec3 euler = Cosmic::Math::EulerZYXFromQuat(m_AttNed);
			ImGui::Text("NED pos  N %.1f  E %.1f  D %.1f", m_PosNed.x, m_PosNed.y, m_PosNed.z);
			ImGui::Text("Attitude R %.0f  P %.0f  Y %.0f", euler.x, euler.y, euler.z);

			if (ImGui::Button("Clear trail"))
				m_Trail.clear();
		}

		// ---------------- Fixed timestep (E1) ----------------
		if (ImGui::CollapsingHeader("Fixed Timestep  (E1)", ImGuiTreeNodeFlags_DefaultOpen))
		{
			auto& app = Cosmic::Application::Get();
			if (ImGui::SliderFloat("Engine rate (Hz)", &m_FixedHzUi, 30.0f, 480.0f, "%.0f"))
				app.SetFixedTimestepHz(m_FixedHzUi);

			ImGui::Text("Configured: %.0f Hz", app.GetFixedTimestepHz());
			ImGui::Text("Measured OnFixedUpdate: %.0f Hz", m_MeasuredFixedHz);
			ImGui::TextDisabled("240 Hz should read ~4x the 60 Hz default.");
		}

		// ---------------- Rendering (S1/S2) ----------------
		if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Grid", &m_ShowGrid);           ImGui::SameLine();
			ImGui::Checkbox("Body axes", &m_ShowAxes);      ImGui::SameLine();
			ImGui::Checkbox("Bounds", &m_ShowWireBox);
			ImGui::Checkbox("Trail", &m_ShowTrail);         ImGui::SameLine();
			ImGui::Checkbox("2D overlay", &m_Show2D);

			if (ImGui::SliderFloat3("Light dir", &m_LightDir.x, -1.0f, 1.0f, "%.2f"))
				Cosmic::Renderer3D::SetLightDirection(m_LightDir);
			if (ImGui::SliderFloat("Ambient", &m_Ambient, 0.0f, 1.0f, "%.2f"))
				Cosmic::Renderer3D::SetAmbient(m_Ambient);
		}

		ImGui::End();
	}

	// =========================================================================
	// Events
	// =========================================================================

	void Engine3DDemo::OnEvent(Cosmic::Event& e)
	{
		m_Orbit.OnEvent(e);
	}

} // namespace Workspace

// =============================================================================
// Required C-linkage DLL entry points — do not rename or remove
// =============================================================================
extern "C"
{
	__declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context)
	{
		ImGui::SetCurrentContext(context.ImGuiCtx);
		ImPlot::SetCurrentContext(context.ImPlotCtx);
	}

	__declspec(dllexport) Cosmic::Layer* CreatePluginLayer()
	{
		return new Workspace::Engine3DDemo();
	}
}
