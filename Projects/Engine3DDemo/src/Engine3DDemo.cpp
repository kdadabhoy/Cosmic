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

		// ---- Material pad (S4.2): custom material on the DemoChecker3D shader ----
		if (auto shader = Cosmic::Shader::Create("assets/shaders/DemoChecker3D.glsl"))
		{
			m_PadMaterial = Cosmic::Material::Create(shader, "Checker Pad");
			m_PadMaterial->Set("u_Color", glm::vec4{ 0.30f, 0.65f, 0.95f, 1.0f });
			m_PadMaterial->Set("u_Tiling", 8.0f);

			// Build a 2x2 texture in code to prove BindFull's texture path without
			// shipping an asset: a soft 2x2 checker the shader modulates with.
			auto tex = Cosmic::Texture2D::Create(2, 2);
			uint32_t px[4] = { 0xffb0b0b0, 0xffffffff, 0xffffffff, 0xffb0b0b0 }; // grey/white (RGBA bytes)
			tex->SetData(px, sizeof(px));
			m_PadMaterial->Set("u_Texture", tex);
		}
		else
		{
			CS_WARN("Engine3DDemo: DemoChecker3D shader failed to load — material pad disabled.");
		}

		// ---- ECS scene (S4.3): a few MeshRendererComponent entities ----
		BuildEcsScene();

		// ---- Lighting v1 (S4.5): a MeshLit material for the aircraft ----
		if (auto litShader = Cosmic::Shader::Create("assets/shaders/MeshLit.glsl"))
		{
			m_LitMaterial = Cosmic::Material::Create(litShader, "Aircraft Lit");
			m_LitMaterial->Set("u_Color", glm::vec4{ 0.8f, 0.8f, 0.84f, 1.0f });
			m_LitMaterial->Set("u_Shininess", m_Shininess);
		}
		else
		{
			CS_WARN("Engine3DDemo: MeshLit shader failed to load — lit aircraft disabled.");
		}

		// ---- Compute + SSBO (S4.7): 1M-point compute particle system ----
		m_ComputeShader = Cosmic::Shader::Create("assets/shaders/ComputeParticles.glsl");
		m_PointShader   = Cosmic::Shader::Create("assets/shaders/ParticlePoints.glsl");
		if (m_ComputeShader && m_PointShader)
			m_ParticleSSBO = Cosmic::StorageBuffer::Create(k_ParticleCount * sizeof(glm::vec4), 0);
		else
			CS_WARN("Engine3DDemo: compute/point shaders failed to load — compute demo disabled.");

		// ---- Picking / MRT (S4.6): own FBO with an entity-ID attachment ----
		{
			Cosmic::FramebufferSpecification pickSpec;
			pickSpec.Width  = 512;
			pickSpec.Height = 512;
			pickSpec.Attachments = {
				Cosmic::FramebufferTextureFormat::RGBA8,
				Cosmic::FramebufferTextureFormat::RED_INTEGER,
				Cosmic::FramebufferTextureFormat::DEPTH24STENCIL8
			};
			m_PickFbo = Cosmic::FrameBuffer::Create(pickSpec);
		}
		m_PickCam.LookAt({ 0.0f, 1.5f, 16.0f }, { 0.0f, 0.0f, 0.0f });

		// ---- glTF model (S4.4b): load the Duck through the asset cache ----
		m_DuckModel = Cosmic::AssetLibrary::GetModel("engine://models/Duck.glb");
		{
			auto again = Cosmic::AssetLibrary::GetModel("engine://models/Duck.glb");
			if (m_DuckModel && again && m_DuckModel.get() == again.get())
				m_GltfCacheResult = "PASS: reload returned the same Ref.";
			else if (!m_DuckModel)
				m_GltfCacheResult = "FAIL: Duck.glb failed to load.";
			else
				m_GltfCacheResult = "FAIL: reload returned a different Ref.";
			CS_INFO("glTF Duck load: {}", m_GltfCacheResult);
		}

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
		m_PadMaterial.reset();
		m_Scene.reset();
		m_DuckModel.reset();
		m_LitMaterial.reset();
		m_PickFbo.reset();
		m_ComputeShader.reset();
		m_PointShader.reset();
		m_ParticleSSBO.reset();

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

	// =========================================================================
	// ECS scene (S4.3) — a few MeshRendererComponent entities rendered through
	// Scene::OnRender3D. Reuses the aircraft primitive meshes; entities sit off
	// to the side (x < 0, y = 3) so they read as a distinct little display.
	// =========================================================================

	void Engine3DDemo::BuildEcsScene()
	{
		m_Scene = Cosmic::Scene::Create();

		auto add = [&](const char* name, const Cosmic::Ref<Cosmic::Mesh>& mesh,
		               const glm::vec3& pos, const glm::vec3& scale, const glm::vec4& color)
		{
			Cosmic::Entity e = m_Scene->CreateEntity(name);
			auto& t = e.GetComponent<Cosmic::TransformComponent>();
			t.Position = pos;
			t.Scale    = scale;                 // vec3 scale (S4.3)

			auto& mr = e.AddComponent<Cosmic::MeshRendererComponent>();
			mr.MeshAsset = mesh;
			mr.Color     = color;               // Lambert color path (no material)
			return e;
		};

		// Box with a non-uniform vec3 scale.
		add("ECS Box",    m_Wing,     { -9.0f, 3.0f, 0.0f }, { 0.9f, 2.4f, 1.4f }, { 0.85f, 0.28f, 0.24f, 1.0f });
		// Sphere scaled up uniformly (canopy is a small-radius UV sphere).
		add("ECS Sphere", m_Canopy,   { -6.0f, 3.0f, 0.0f }, { 6.0f, 6.0f, 6.0f }, { 0.30f, 0.72f, 0.36f, 1.0f });
		// Cone scaled up.
		add("ECS Cone",   m_Nose,     { -3.0f, 3.0f, 0.0f }, { 2.4f, 2.4f, 2.4f }, { 0.92f, 0.78f, 0.24f, 1.0f });

		// Cylinder using the QUATERNION rotation path (45 deg about world +Z).
		Cosmic::Entity spun = add("ECS Cylinder (quat)", m_Fuselage, { 0.0f, 3.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.34f, 0.55f, 0.90f, 1.0f });
		auto& st = spun.GetComponent<Cosmic::TransformComponent>();
		st.UseQuatRotation = true;
		st.RotationQuat    = glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 0.0f, 1.0f));
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
			// Lighting v1 (S4.5): route each part through the shared MeshLit material,
			// re-tinting u_Color per part. Otherwise use the flat Lambert color path.
			if (m_LitAircraft && m_LitMaterial)
			{
				m_LitMaterial->Set("u_Color", color);
				Cosmic::Renderer3D::DrawMesh(mesh, root * local, m_LitMaterial);
			}
			else
			{
				Cosmic::Renderer3D::DrawMesh(mesh, root * local, color);
			}
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
	// Picking / MRT (S4.6) — render the aircraft parts into an entity-ID FBO,
	// each with a distinct ID (1..N), so hovering the panel can read the ID back.
	// =========================================================================

	namespace
	{
		const char* PickPartName(int id)
		{
			switch (id)
			{
			case 1:  return "Fuselage";
			case 2:  return "Nose";
			case 3:  return "Canopy";
			case 4:  return "Wing";
			case 5:  return "Tailplane";
			case 6:  return "Fin";
			default: return "(empty)";
			}
		}
	}

	void Engine3DDemo::RenderPickPass()
	{
		if (!m_PickFbo)
			return;

		// Six parts laid out in a row; ID = column index + 1.
		const Cosmic::Ref<Cosmic::Mesh> parts[6] = {
			m_Fuselage, m_Nose, m_Canopy, m_Wing, m_Tailplane, m_Fin
		};
		const glm::vec4 colors[6] = {
			{ 0.78f, 0.80f, 0.84f, 1.0f }, { 0.92f, 0.34f, 0.18f, 1.0f },
			{ 0.35f, 0.62f, 0.80f, 1.0f }, { 0.55f, 0.75f, 0.40f, 1.0f },
			{ 0.80f, 0.70f, 0.30f, 1.0f }, { 0.60f, 0.40f, 0.85f, 1.0f },
		};

		m_PickFbo->Bind();
		Cosmic::RenderCommand::SetViewport(0, 0, m_PickFbo->GetWidth(), m_PickFbo->GetHeight());
		Cosmic::RenderCommand::SetClearColor({ 0.10f, 0.11f, 0.13f, 1.0f });
		Cosmic::RenderCommand::Clear();
		// glClear does NOT reliably clear the integer attachment — do it explicitly.
		m_PickFbo->ClearAttachment(1, -1);

		Cosmic::Renderer3D::BeginScene(m_PickCam);
		for (int i = 0; i < 6; ++i)
		{
			const float x = -6.0f + static_cast<float>(i) * 2.4f;
			const glm::mat4 xform =
				glm::translate(glm::mat4(1.0f), { x, 0.0f, 0.0f }) *
				glm::scale(glm::mat4(1.0f), glm::vec3(0.7f));
			Cosmic::Renderer3D::DrawMesh(parts[i], xform, colors[i], i + 1);   // entity ID = i+1
		}
		Cosmic::Renderer3D::EndScene();

		m_PickFbo->Unbind();

		// Restore the app viewport FBO + viewport for the rest of the frame
		// (copy of the proven inset rebind pattern, ViperSim FlightScreen S3.1).
		auto& app = Cosmic::Application::Get();
		auto  fb  = app.GetFrameBuffer();
		fb->Bind();
		Cosmic::RenderCommand::SetViewport(0, 0, fb->GetWidth(), fb->GetHeight());
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

		// Picking pre-pass (S4.6): render the ID scene into m_PickFbo first, then
		// rebind the app viewport FBO. Runs before the main pass, like the FPV inset.
		if (m_ShowPicking)
			RenderPickPass();

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
		// on a big flat face). Toggle m_MaterialPad to draw it through the S4.2
		// custom-material path (DemoChecker3D) instead of the flat Lambert color.
		{
			const glm::mat4 padXform = glm::translate(glm::mat4(1.0f), { 0.0f, 0.01f, 0.0f });
			if (m_MaterialPad && m_PadMaterial)
				Cosmic::Renderer3D::DrawMesh(m_Pad, padXform, m_PadMaterial);
			else
				Cosmic::Renderer3D::DrawMesh(m_Pad, padXform, { 0.16f, 0.35f, 0.20f, 1.0f });
		}

		if (m_ShowTrail && m_Trail.size() >= 2)
			Cosmic::Renderer3D::DrawPolyline(m_Trail, { 0.95f, 0.75f, 0.20f, 1.0f });

		// Imported glTF model (S4.4b) — sits beside the pad, world transform baked.
		if (m_ShowGltf && m_DuckModel)
			Cosmic::Renderer3D::DrawModel(m_DuckModel,
				glm::translate(glm::mat4(1.0f), { 5.0f, 0.0f, 0.0f }));

		// Lighting v1 (S4.5): push the sun + two point lights into the binding-0 UBO
		// before drawing anything through the MeshLit material.
		if (m_LitAircraft && m_LitMaterial)
		{
			m_LitMaterial->Set("u_Shininess", m_Shininess);

			Cosmic::Renderer3D::SceneLightsDesc lights;
			lights.SunDirection = m_LightDir;
			lights.SunColor     = m_SunColor;
			lights.SunIntensity = m_SunIntensity;
			lights.Ambient      = m_Ambient;
			lights.Points = {
				{ m_P0Pos, m_PointRadius, { 1.0f, 0.25f, 0.20f }, 1.0f },   // red
				{ m_P1Pos, m_PointRadius, { 0.25f, 0.40f, 1.0f }, 1.0f },   // blue
			};
			Cosmic::Renderer3D::SetLights(lights);
		}

		DrawAircraft();

		Cosmic::Renderer3D::EndScene();

		// ECS scene (S4.3): its own 3D pass (OnRender3D owns BeginScene/EndScene).
		if (m_EcsScene && m_Scene)
			m_Scene->OnRender3D(m_Orbit.GetCamera());

		// Compute + SSBO (S4.7): update 1M particles on the GPU, then draw them as
		// attribute-less points reading the same std430 buffer by gl_VertexID.
		if (m_ShowCompute && m_ComputeShader && m_PointShader && m_ParticleSSBO)
		{
			m_ComputeTime += ts;

			// 1) Compute pass: advance every particle position in the SSBO.
			m_ComputeShader->Bind();
			m_ComputeShader->SetFloat("u_Time", m_ComputeTime);
			m_ComputeShader->SetInt("u_Count", (int)k_ParticleCount);
			m_ParticleSSBO->Bind();
			Cosmic::RenderCommand::DispatchCompute((k_ParticleCount + 255) / 256, 1, 1);

			// 2) Make the compute writes visible to the vertex fetch + SSBO read.
			Cosmic::RenderCommand::MemoryBarrier(
				Cosmic::RenderCommand::GpuBarrier::VertexAttribArray |
				Cosmic::RenderCommand::GpuBarrier::ShaderStorage);

			// 3) Draw the points (no vertex attributes; positions come from the SSBO).
			m_PointShader->Bind();
			m_PointShader->SetMat4("u_ViewProjection", m_Orbit.GetCamera().GetViewProjectionMatrix());
			m_PointShader->SetFloat4("u_Color", { 0.55f, 0.85f, 1.0f, 1.0f });
			Cosmic::RenderCommand::DrawArrays(
				Cosmic::RenderCommand::PrimitiveTopology::Points, 0, k_ParticleCount);
		}

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

			ImGui::BeginDisabled(!m_PadMaterial);
			ImGui::Checkbox("Material pad (S4.2)", &m_MaterialPad);
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Draws the ground pad through the custom-material path\n"
				                  "(DemoChecker3D: tinted checker x 2x2 texture, Lambert-lit).");

			ImGui::Checkbox("ECS scene (S4.3)", &m_EcsScene);
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Renders 4 MeshRendererComponent entities via Scene::OnRender3D\n"
				                  "(off to the left, y=3). One cylinder uses the quaternion path.");

			ImGui::BeginDisabled(!m_DuckModel);
			ImGui::Checkbox("glTF Duck (S4.4b)", &m_ShowGltf);
			ImGui::EndDisabled();
			if (!m_GltfCacheResult.empty())
				ImGui::TextDisabled("%s", m_GltfCacheResult.c_str());

			if (ImGui::SliderFloat3("Light dir", &m_LightDir.x, -1.0f, 1.0f, "%.2f"))
				Cosmic::Renderer3D::SetLightDirection(m_LightDir);
			if (ImGui::SliderFloat("Ambient", &m_Ambient, 0.0f, 1.0f, "%.2f"))
				Cosmic::Renderer3D::SetAmbient(m_Ambient);
		}

		// ---------------- Lighting v1 (S4.5) ----------------
		if (ImGui::CollapsingHeader("Lighting v1  (MeshLit + UBO, S4.5)"))
		{
			ImGui::BeginDisabled(!m_LitMaterial);
			ImGui::Checkbox("Lit aircraft (S4.5)", &m_LitAircraft);
			ImGui::EndDisabled();
			ImGui::TextDisabled("Uses 'Light dir' above as the sun direction.");

			ImGui::SeparatorText("Sun");
			ImGui::ColorEdit3("Sun color", &m_SunColor.x);
			ImGui::SliderFloat("Sun intensity", &m_SunIntensity, 0.0f, 4.0f, "%.2f");
			ImGui::SliderFloat("Shininess", &m_Shininess, 1.0f, 128.0f, "%.0f");

			ImGui::SeparatorText("Point lights");
			ImGui::SliderFloat3("Red pos",  &m_P0Pos.x, -10.0f, 10.0f, "%.1f");
			ImGui::SliderFloat3("Blue pos", &m_P1Pos.x, -10.0f, 10.0f, "%.1f");
			ImGui::SliderFloat("Point radius", &m_PointRadius, 1.0f, 30.0f, "%.0f");
		}

		// ---------------- Assets (S4.4a) ----------------
		if (ImGui::CollapsingHeader("Assets  (AssetLibrary cache, S4.4a)"))
		{
			if (ImGui::Button("Cache check"))
			{
				// Ask twice for the same VFS path; a working cache hands back the
				// identical Ref both times. (Uses a shipped texture — no .obj ships
				// until models arrive in S4.4b.)
				auto a = Cosmic::AssetLibrary::GetTexture("engine://textures/Galaxy.png");
				auto b = Cosmic::AssetLibrary::GetTexture("engine://textures/Galaxy.png");

				if (a && b && a.get() == b.get())
					m_CacheCheckResult = "PASS: both requests returned the same Ref.";
				else if (!a || !b)
					m_CacheCheckResult = "FAIL: asset failed to load.";
				else
					m_CacheCheckResult = "FAIL: cache returned different pointers.";

				CS_INFO("AssetLibrary cache check: {}", m_CacheCheckResult);
			}
			if (!m_CacheCheckResult.empty())
				ImGui::TextWrapped("%s", m_CacheCheckResult.c_str());
		}

		// ---------------- Picking / MRT (S4.6) ----------------
		if (ImGui::CollapsingHeader("Picking  (MRT entity-ID readback, S4.6)"))
		{
			ImGui::Checkbox("Enable pick pass", &m_ShowPicking);

			if (m_ShowPicking && m_PickFbo)
			{
				const float aw = ImGui::GetContentRegionAvail().x;
				const ImVec2 size{ aw, aw };   // square FBO
				// Flip V so the GL bottom-left origin shows upright.
				ImGui::Image((ImTextureID)(intptr_t)m_PickFbo->GetColorAttachmentRendererID(0),
					size, ImVec2(0, 1), ImVec2(1, 0));

				if (ImGui::IsItemHovered() && size.x > 0.0f && size.y > 0.0f)
				{
					const ImVec2 imgMin = ImGui::GetItemRectMin();
					const ImVec2 mouse  = ImGui::GetMousePos();
					const float  u = (mouse.x - imgMin.x) / size.x;
					const float  v = (mouse.y - imgMin.y) / size.y;   // 0 at top

					const int w = (int)m_PickFbo->GetWidth();
					const int h = (int)m_PickFbo->GetHeight();
					const int fx = (int)(u * w);
					const int glY = h - 1 - (int)(v * h);   // caller flips Y (GL origin bottom-left)

					m_PickFbo->Bind();
					m_HoveredId = m_PickFbo->ReadPixel(1, fx, glY);
					m_PickFbo->Unbind();
				}

				ImGui::Text("Hovered ID: %d  (%s)", m_HoveredId, PickPartName(m_HoveredId));
				ImGui::TextDisabled("Hover a part; empty space reads -1.");
			}
		}

		// ---------------- Compute + SSBO (S4.7) ----------------
		if (ImGui::CollapsingHeader("Compute  (1M GPU particles, S4.7)"))
		{
			ImGui::BeginDisabled(!m_ParticleSSBO);
			ImGui::Checkbox("Compute particles (S4.7)", &m_ShowCompute);
			ImGui::EndDisabled();
			ImGui::Text("Particles: %u", k_ParticleCount);
			ImGui::Text("Frame rate: %.1f fps", ImGui::GetIO().Framerate);
			ImGui::TextDisabled("Target: 1M animated points at >= 60 fps.");
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
