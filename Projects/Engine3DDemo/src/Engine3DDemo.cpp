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

		// Panels → engine dock ports (port mode: window names are free-form and
		// must match the ImGui::Begin calls in the Draw*Panel helpers). Left
		// column: interactive tools; right column: sim + acceptance rigs.
		if (auto* ws = Cosmic::Application::Get().GetWorkspaceLayer())
		{
			ws->ClearDockWindows();
			ws->DockWindow("Camera & Views",       Cosmic::DockPort::LeftTop);
			ws->DockWindow("Editor Tools",         Cosmic::DockPort::LeftMiddle);
			ws->DockWindow("Rendering & Lighting", Cosmic::DockPort::LeftBottom);
			ws->DockWindow("Simulation & Timing",  Cosmic::DockPort::RightTop);
			ws->DockWindow("Feature Demos",        Cosmic::DockPort::RightBottom);
		}

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

		// ---- PBR (S6.2): Cook-Torrance material + a shared sphere for the grid ----
		m_PbrSphere = Cosmic::Mesh::CreateUVSphere(0.6f, 32, 48);
		if (auto pbrShader = Cosmic::Shader::Create("assets/shaders/PBR.glsl"))
		{
			m_PbrMaterial = Cosmic::Material::Create(pbrShader, "PBR Grid");
			m_PbrMaterial->Set("u_Albedo",    glm::vec4{ m_PbrAlbedo, 1.0f });
			m_PbrMaterial->Set("u_AO",        1.0f);
			m_PbrMaterial->Set("u_Emissive",  glm::vec3{ 0.0f });
			// u_Metallic / u_Roughness are Set per-sphere in DrawPbrSpheres.

			// Emissive material for the bloom demo (S6.6): a dark surface with a
			// bright (HDR > 1) orange emission so it glows through the tonemap.
			m_EmitterMat = Cosmic::Material::Create(
				Cosmic::Shader::Create("assets/shaders/PBR.glsl"), "Bloom Emitter");
			m_EmitterMat->Set("u_Albedo",    glm::vec4{ 0.05f, 0.02f, 0.0f, 1.0f });
			m_EmitterMat->Set("u_Metallic",  0.0f);
			m_EmitterMat->Set("u_Roughness", 0.6f);
			m_EmitterMat->Set("u_AO",        1.0f);
			m_EmitterMat->Set("u_Emissive",  glm::vec3{ 6.0f, 2.4f, 0.6f });

			// Auto-instancing demo material (S12.3): ONE material shared by many
			// identical DrawMesh submissions, with the PBRInstanced twin
			// registered — the render queue collapses the run into a single
			// instanced draw (watch "Auto-instanced" in Performance (S12)).
			m_AutoInstMaterial = Cosmic::Material::Clone(m_PbrMaterial, "AutoInst Rocks");
			m_AutoInstMaterial->Set("u_Albedo",   glm::vec4{ 0.30f, 0.70f, 0.65f, 1.0f });
			m_AutoInstMaterial->Set("u_Metallic", 0.1f);
			m_AutoInstMaterial->Set("u_Roughness", 0.55f);
			if (auto twin = Cosmic::Shader::Create("assets/shaders/PBRInstanced.glsl"))
				m_AutoInstMaterial->SetInstancingShader(twin);
			else
				CS_WARN("Engine3DDemo: PBRInstanced shader failed to load — S12.3 runs fall back to singles.");
		}
		else
		{
			CS_WARN("Engine3DDemo: PBR shader failed to load — PBR spheres disabled.");
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

		// ---- HDR pipeline (S6.1): float scene target + ACES tonemap ----
		// Seed with the current viewport FBO size; OnUpdate resizes it each frame.
		{
			auto vfb = Cosmic::Application::Get().GetFrameBuffer();
			const uint32_t w = vfb->GetWidth()  > 0 ? vfb->GetWidth()  : 1280;
			const uint32_t h = vfb->GetHeight() > 0 ? vfb->GetHeight() : 720;
			m_PostFx.Init(w, h);
		}

		// ---- IBL + skybox (S6.3): bake the procedural-sky environment once ----
		m_Environment.Init();
		m_Environment.SetSunDirection(-m_LightDir);   // "direction TO the sun"
		m_Environment.Bake();

		// ---- Directional shadows (S6.4): 2k sun shadow map ----
		m_Shadow.Init(2048);

		// ---- World systems (Phase 10 / S8–S10). All CPU-side here — the GPU
		//      resources are lazy-created on each system's first Render. ----
		{
			// S8: a procedural island under/around the flight demo. The scene's
			// pad sits at y = 0; the island rolls from below the lake (-9 m) to
			// snow-capped hills (+11 m), with the auto-splat bands parameterized
			// around the -5 m waterline.
			Cosmic::TerrainSpecification tspec;
			tspec.Resolution  = 257;
			tspec.WorldSize   = 256.0f;
			tspec.HeightScale = 20.0f;
			tspec.BaseHeight  = -9.0f;
			tspec.Seed        = 20260703;
			tspec.Octaves     = 6;
			tspec.Frequency   = 4.0f;
			tspec.EdgeFalloff = 0.45f;                       // island: edges sink under the lake
			tspec.Material.HighHeight = 6.5f;                // snow above ~6.5 m
			tspec.Material.LowHeight  = -4.2f;               // sand near the waterline
			tspec.Material.LowBlend   = 0.8f;
			m_Terrain = Cosmic::Terrain::Create(tspec);

			// S9: the lake filling everything the island falloff sank.
			Cosmic::WaterSpecification wspec;
			wspec.Extent        = { 256.0f, 256.0f };
			wspec.SurfaceHeight = -5.0f;
			m_Water   = Cosmic::Water::Create(wspec);
			m_BuoyBox = Cosmic::Mesh::CreateBox({ 1.2f, 0.5f, 0.8f });

			// S10.1: a smoke plume (alpha flipbook) + HDR ember sparks (additive —
			// they feed bloom) rising from a fire pit at the pad's edge.
			Cosmic::ParticleEmitterSpec smoke;
			smoke.MaxParticles = 2048;
			smoke.SpawnRate    = 55.0f;
			smoke.Shape        = Cosmic::EmitterShape::Cone;
			smoke.ShapeRadius  = 0.35f;
			smoke.ConeAngleDeg = 14.0f;
			smoke.SpeedMin = 0.8f;  smoke.SpeedMax = 1.6f;
			smoke.LifeMin  = 2.5f;  smoke.LifeMax  = 4.0f;
			smoke.Gravity  = { 0.0f, 0.55f, 0.0f };          // buoyant
			smoke.Drag     = 0.35f;
			smoke.Wind     = { 0.35f, 0.0f, 0.12f };
			smoke.SizeStart = 0.45f; smoke.SizeEnd = 1.9f;
			smoke.ColorStart = { 0.62f, 0.62f, 0.64f, 0.42f };
			smoke.ColorEnd   = { 0.72f, 0.72f, 0.75f, 0.0f };
			smoke.FlipbookTilesX = 4; smoke.FlipbookTilesY = 4;
			smoke.FlipbookFps    = 9.0f;
			smoke.FlipbookBlend  = true;
			smoke.SoftFadeDistance = 0.6f;
			m_Smoke = Cosmic::ParticleEmitter::Create(smoke);

			Cosmic::ParticleEmitterSpec embers;
			embers.MaxParticles = 1024;
			embers.SpawnRate    = 90.0f;
			embers.Shape        = Cosmic::EmitterShape::Cone;
			embers.ShapeRadius  = 0.25f;
			embers.ConeAngleDeg = 28.0f;
			embers.SpeedMin = 2.0f;  embers.SpeedMax = 4.5f;
			embers.LifeMin  = 0.5f;  embers.LifeMax  = 1.3f;
			embers.Gravity  = { 0.0f, -4.0f, 0.0f };
			embers.Drag     = 0.6f;
			embers.SizeStart = 0.07f; embers.SizeEnd = 0.0f;
			embers.ColorStart = { 4.0f, 1.6f, 0.35f, 1.0f };  // HDR-hot: blooms
			embers.ColorEnd   = { 1.0f, 0.15f, 0.02f, 0.0f };
			embers.Blend = Cosmic::ParticleBlend::Additive;
			embers.SoftFadeDistance = 0.3f;
			m_Embers = Cosmic::ParticleEmitter::Create(embers);

			// S10.5: slow fat puffs that write the heat-haze distortion field.
			Cosmic::ParticleEmitterSpec haze;
			haze.MaxParticles = 256;
			haze.SpawnRate    = 18.0f;
			haze.Shape        = Cosmic::EmitterShape::Cone;
			haze.ShapeRadius  = 0.30f;
			haze.ConeAngleDeg = 10.0f;
			haze.SpeedMin = 0.7f;  haze.SpeedMax = 1.2f;
			haze.LifeMin  = 1.0f;  haze.LifeMax  = 1.8f;
			haze.Gravity  = { 0.0f, 0.8f, 0.0f };
			haze.Drag     = 0.3f;
			haze.SizeStart = 0.7f; haze.SizeEnd = 1.8f;
			haze.ColorStart = { 1.0f, 1.0f, 1.0f, 0.30f };
			haze.ColorEnd   = { 1.0f, 1.0f, 1.0f, 0.0f };
			haze.SoftFadeDistance = 0.5f;
			m_Haze = Cosmic::ParticleEmitter::Create(haze);

			// S10.2: a wingtip trail ribbon on the orbiting aircraft.
			Cosmic::RibbonSpec ribbon;
			ribbon.MaxPoints     = 96;
			ribbon.Width         = 0.14f;
			ribbon.PointLifetime = 1.4f;
			ribbon.ColorHead     = { 0.45f, 0.9f, 1.0f, 0.85f };
			ribbon.ColorTail     = { 0.45f, 0.9f, 1.0f, 0.0f };
			ribbon.Additive      = true;
			m_Ribbon = Cosmic::RibbonEmitter::Create(ribbon);
		}

		// ---- CAD tools (Phase 8 / S5): nav cube + scene picker ----
		m_NavCube = Cosmic::NavigationCube::Create(140);
		m_Picker  = Cosmic::ScenePicker::Create();

		// Orbit-about-surface (S5.1): reconstruct the world point under the cursor
		// from the editor id-pass depth. Only valid in editor mode (that's when the
		// picker FBO is rendered each frame); otherwise the controller falls back to
		// its ray/target-plane pivot. screenMouse and the viewport rect are both in
		// ImGui screen pixels, so the subtraction is a straight space-local remap.
		m_Orbit.SetPivotProbe([this](const glm::vec2& screenMouse, glm::vec3& out) -> bool
		{
			if (!m_EditorMode || !m_Picker)
				return false;
			auto& app = Cosmic::Application::Get();
			const glm::vec2 vpPos = app.GetViewportPos();
			const int px = static_cast<int>(screenMouse.x - vpPos.x);
			const int py = static_cast<int>(screenMouse.y - vpPos.y);
			return m_Picker->WorldPoint(m_Orbit.GetCamera(), px, py, out);
		});

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
		m_LitPartMaterials.clear();
		m_PbrSphere.reset();
		m_PbrMaterial.reset();
		m_PbrGridMaterials.clear();
		m_EmitterMat.reset();
		m_AutoInstMaterial.reset();
		m_PickFbo.reset();
		m_ComputeShader.reset();
		m_PointShader.reset();
		m_ParticleSSBO.reset();
		m_NavCube.reset();
		m_Picker.reset();
		m_Terrain.reset();     // Phase 10 world systems (GPU resources inside)
		m_Water.reset();
		m_BuoyBox.reset();
		m_Smoke.reset();
		m_Embers.reset();
		m_Haze.reset();
		m_Ribbon.reset();
		m_PostFx.Shutdown();   // release the HDR target + tonemap shader (context still live)
		m_Environment.Shutdown();     // release IBL cubes + BRDF LUT (S6.3)
		Cosmic::Renderer3D::ClearIBL();
		m_Shadow.Shutdown();          // release the shadow map (S6.4)
		Cosmic::Renderer3D::ClearShadow();

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

		// LOD group (S12.4): sphere (near) -> cone (mid) -> box (far), hard cuts
		// at 15 / 35 m, distance-culled past 90 m. Orbit-zoom away from the ECS
		// scene to watch the swaps; the shape change is deliberately obvious.
		{
			Cosmic::Entity e = m_Scene->CreateEntity("ECS LOD group");
			auto& t = e.GetComponent<Cosmic::TransformComponent>();
			t.Position = { 3.0f, 3.0f, 0.0f };
			t.Scale    = { 3.0f, 3.0f, 3.0f };

			auto& lod = e.AddComponent<Cosmic::LODGroupComponent>();
			lod.Levels = {
				{ m_Canopy, 15.0f },   // LOD0: UV sphere while close
				{ m_Nose,   35.0f },   // LOD1: cone at mid range
				{ m_Wing,   90.0f },   // LOD2: box far out; culled beyond
			};
			lod.Color = { 0.78f, 0.42f, 0.85f, 1.0f };
		}
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

		// Lighting v1 (S4.5) via ONE MeshLit clone per part color: the S12.2
		// queue reads material values at flush, so re-tinting a shared material
		// between draws would paint every part the last color (Renderer3D.h
		// queue-semantics contract). Clones are cached across frames; the
		// shininess slider pushes into them (see OnUpdate).
		auto litFor = [&](const glm::vec4& color) -> const Cosmic::Ref<Cosmic::Material>&
		{
			for (auto& [c, m] : m_LitPartMaterials)
				if (c == color)
					return m;
			auto clone = Cosmic::Material::Clone(m_LitMaterial, "MeshLit part");
			clone->Set("u_Color", color);
			m_LitPartMaterials.emplace_back(color, clone);
			return m_LitPartMaterials.back().second;
		};

		auto part = [&](const Cosmic::Ref<Cosmic::Mesh>& mesh, const glm::mat4& local, const glm::vec4& color)
		{
			if (m_LitAircraft && m_LitMaterial)
				Cosmic::Renderer3D::DrawMesh(mesh, root * local, litFor(color));
			else
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
	// Shadow casters (S6.4) — the aircraft parts + ECS meshes rendered from the
	// sun's POV into the shadow map. Transforms mirror DrawAircraft's; the ground
	// pad is a receiver (front-culled during the depth pass anyway), so it is not
	// cast here.
	// =========================================================================

	void Engine3DDemo::DrawShadowCasters()
	{
		namespace Math = Cosmic::Math;

		const glm::vec3 posR = Math::NedToRender(m_PosNed);
		const glm::quat attR = Math::NedQuatToRender(m_AttNed);
		const glm::mat4 root = glm::translate(glm::mat4(1.0f), posR) * glm::mat4_cast(attR);
		const glm::mat4 alongZ = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), { 1, 0, 0 });

		m_Shadow.DrawCaster(m_Fuselage, root * alongZ);
		m_Shadow.DrawCaster(m_Nose,
			root * glm::translate(glm::mat4(1.0f), { 0.0f, 0.0f, -1.5f }) *
			glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), { 1, 0, 0 }));
		m_Shadow.DrawCaster(m_Wing,      root * glm::translate(glm::mat4(1.0f), { 0.0f, 0.0f, -0.10f }));
		m_Shadow.DrawCaster(m_Tailplane, root * glm::translate(glm::mat4(1.0f), { 0.0f, 0.0f,  1.05f }));
		m_Shadow.DrawCaster(m_Fin,       root * glm::translate(glm::mat4(1.0f), { 0.0f, 0.28f, 1.05f }));

		// ECS meshes (respect CastShadows).
		if (m_Scene)
		{
			auto view = m_Scene->View<Cosmic::TransformComponent, Cosmic::MeshRendererComponent>();
			for (auto entity : view)
			{
				Cosmic::Entity e{ entity, m_Scene.get() };
				const auto& mr = e.GetComponent<Cosmic::MeshRendererComponent>();
				if (mr.MeshAsset && mr.CastShadows)
					m_Shadow.DrawCaster(mr.MeshAsset, e.GetComponent<Cosmic::TransformComponent>().GetTransform());
			}
		}
	}

	// =========================================================================
	// PBR sphere grid (S6.2) — the classic Cook-Torrance validation scene:
	// roughness 0->1 across (+x), metallic 0->1 up (+y). One clone of the PBR
	// material PER SPHERE: Renderer3D's S12.2 queue reads material values at
	// flush, not at DrawMesh, so mutating one shared material between draws
	// would render every sphere with the last-set values (the queue-semantics
	// contract in Renderer3D.h names this exact grid). Clones are built once
	// and re-tinted when the albedo picker changes. Lit by the same sun + two
	// point lights as the aircraft; best viewed with HDR on (S6.1).
	// =========================================================================

	void Engine3DDemo::DrawPbrSpheres()
	{
		constexpr int   N       = 5;
		constexpr float spacing = 1.5f;
		const glm::vec3 origin{ -0.5f * spacing * (N - 1), 5.0f, 0.0f };

		if (m_PbrGridMaterials.size() != static_cast<size_t>(N * N))
		{
			m_PbrGridMaterials.clear();
			m_PbrGridMaterials.reserve(N * N);
			for (int row = 0; row < N; ++row)      // metallic 0 (bottom) -> 1 (top)
			{
				const float metallic = static_cast<float>(row) / static_cast<float>(N - 1);
				for (int col = 0; col < N; ++col)  // roughness 0 (left) -> 1 (right)
				{
					const float roughness = glm::clamp(
						static_cast<float>(col) / static_cast<float>(N - 1), 0.05f, 1.0f);

					auto mat = Cosmic::Material::Clone(m_PbrMaterial,
						"PBR grid r" + std::to_string(row) + "c" + std::to_string(col));
					mat->Set("u_Metallic",  metallic);
					mat->Set("u_Roughness", roughness);
					m_PbrGridMaterials.push_back(mat);
				}
			}
		}

		for (int row = 0; row < N; ++row)
		{
			for (int col = 0; col < N; ++col)
			{
				auto& mat = m_PbrGridMaterials[row * N + col];
				mat->Set("u_Albedo", glm::vec4{ m_PbrAlbedo, 1.0f });

				const glm::vec3 pos = origin +
					glm::vec3{ col * spacing, row * spacing, 0.0f };
				Cosmic::Renderer3D::DrawMesh(m_PbrSphere,
					glm::translate(glm::mat4(1.0f), pos), mat);
			}
		}
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
	// CAD navigation / picking / gizmos (Phase 8 / S5)
	// =========================================================================

	void Engine3DDemo::RenderEditorIdPass()
	{
		auto& app = Cosmic::Application::Get();
		auto  fb  = app.GetFrameBuffer();

		// Render the ECS scene's entity IDs (+ depth) into the picker's own MRT FBO.
		m_Picker->RenderIdPass(*m_Scene, m_Orbit.GetCamera(), fb->GetWidth(), fb->GetHeight());

		// RenderIdPass unbinds to the default framebuffer — restore the app viewport.
		fb->Bind();
		Cosmic::RenderCommand::SetViewport(0, 0, fb->GetWidth(), fb->GetHeight());
	}

	void Engine3DDemo::HandleEditorPicking()
	{
		// Edge-detect a left click; route it to selection only when the click is
		// really meant for the 3D scene: viewport hovered (not a side panel or a
		// floating window on top of it), no gizmo handle under the cursor, and
		// not on the ViewCube corner.
		const bool lmb     = Cosmic::Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_LEFT);
		const bool clicked = lmb && !m_LmbWasDown;
		m_LmbWasDown = lmb;

		auto& app = Cosmic::Application::Get();
		auto* ws  = app.GetWorkspaceLayer();
		const bool vpHovered = ws && ws->IsViewportHovered();

		if (!clicked || !vpHovered || m_GizmoActive || m_GizmoOver || m_NavCubeHovered ||
		    !m_Picker || !m_Scene)
			return;

		const glm::vec2 vpPos  = app.GetViewportPos();
		const glm::vec2 vpSize = app.GetViewportSize();
		const glm::vec2 mouse  = Cosmic::Input::GetMouseScreenPosition();   // vpPos space
		const int px = static_cast<int>(mouse.x - vpPos.x);
		const int py = static_cast<int>(mouse.y - vpPos.y);
		if (px < 0 || py < 0 || px >= static_cast<int>(vpSize.x) || py >= static_cast<int>(vpSize.y))
			return;   // click landed outside the viewport image

		Cosmic::Entity hit = m_Picker->Pick(*m_Scene, px, py);
		if (hit)
		{
			const std::string name = hit.HasComponent<Cosmic::TagComponent>()
				? hit.GetComponent<Cosmic::TagComponent>().Tag
				: std::string("Entity");
			Cosmic::EntitySelection::Set(hit, name);   // feed the shared selection bus
		}
		else
		{
			Cosmic::EntitySelection::Clear();
		}
	}

	Cosmic::Entity Engine3DDemo::SelectedEntity() const
	{
		Cosmic::Entity e = Cosmic::EntitySelection::GetEntity();
		if (e && e.HasComponent<Cosmic::TransformComponent>() &&
		         e.HasComponent<Cosmic::MeshRendererComponent>())
			return e;
		return {};
	}

	bool Engine3DDemo::ComputeEntityWorldAABB(Cosmic::Entity e, glm::vec3& mn, glm::vec3& mx) const
	{
		if (!e || !e.HasComponent<Cosmic::TransformComponent>() ||
		          !e.HasComponent<Cosmic::MeshRendererComponent>())
			return false;

		const auto& mr = e.GetComponent<Cosmic::MeshRendererComponent>();
		if (!mr.MeshAsset)
			return false;

		const glm::vec3 lmin  = mr.MeshAsset->GetLocalMin();
		const glm::vec3 lmax  = mr.MeshAsset->GetLocalMax();
		const glm::mat4 model = e.GetComponent<Cosmic::TransformComponent>().GetTransform();

		// World AABB = bounds of the 8 transformed local-AABB corners.
		for (int i = 0; i < 8; ++i)
		{
			const glm::vec3 corner(
				(i & 1) ? lmax.x : lmin.x,
				(i & 2) ? lmax.y : lmin.y,
				(i & 4) ? lmax.z : lmin.z);
			const glm::vec3 wp = glm::vec3(model * glm::vec4(corner, 1.0f));
			if (i == 0) { mn = mx = wp; }
			else        { mn = glm::min(mn, wp); mx = glm::max(mx, wp); }
		}
		return true;
	}

	bool Engine3DDemo::ComputeSceneWorldAABB(glm::vec3& mn, glm::vec3& mx) const
	{
		if (!m_Scene)
			return false;

		bool any = false;
		auto view = m_Scene->View<Cosmic::TransformComponent, Cosmic::MeshRendererComponent>();
		for (auto entity : view)
		{
			Cosmic::Entity e{ entity, m_Scene.get() };
			glm::vec3 emn, emx;
			if (!ComputeEntityWorldAABB(e, emn, emx))
				continue;
			if (!any) { mn = emn; mx = emx; any = true; }
			else      { mn = glm::min(mn, emn); mx = glm::max(mx, emx); }
		}
		return any;
	}

	void Engine3DDemo::DrawSelectionOutline()
	{
		Cosmic::Entity e = SelectedEntity();
		if (!e)
			return;

		const auto& t  = e.GetComponent<Cosmic::TransformComponent>();
		const auto& mr = e.GetComponent<Cosmic::MeshRendererComponent>();
		if (!mr.MeshAsset)
			return;

		// Map the unit wire cube to the mesh's local AABB, then apply the model
		// matrix → an oriented box hugging the selected mesh (nicer than a loose
		// world AABB, and no post/stencil machinery pre-S6). Drawn depth-test-off so
		// it reads as an always-on-top highlight.
		const glm::vec3 lmin = mr.MeshAsset->GetLocalMin();
		const glm::vec3 lmax = mr.MeshAsset->GetLocalMax();
		const glm::vec3 c    = 0.5f * (lmin + lmax);
		const glm::vec3 s    = (lmax - lmin) * 1.03f;   // slight inflate to clear the surface
		const glm::mat4 box  = t.GetTransform()
			* glm::translate(glm::mat4(1.0f), c)
			* glm::scale(glm::mat4(1.0f), s);

		Cosmic::RenderCommand::SetDepthTest(false);
		Cosmic::Renderer3D::BeginScene(m_Orbit.GetCamera());
		Cosmic::Renderer3D::DrawWireBox(box, { 1.0f, 0.62f, 0.10f, 1.0f });
		Cosmic::Renderer3D::EndScene();
		Cosmic::RenderCommand::SetDepthTest(true);      // restore engine default (contract rule 6)
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

		// S12 telemetry: zero the frame counters before the first pass; every
		// scene this frame (reflection, main, ECS, picking) accumulates into
		// them and the "Performance (S12)" panel section reads them next.
		Cosmic::Renderer3D::ResetStats();

		// ---- Viewport/FBO size sync + CAD nav config (S5.1) ----
		auto fb = app.GetFrameBuffer();
		const float w = static_cast<float>(fb->GetWidth());
		const float h = static_cast<float>(fb->GetHeight());
		m_ViewportSize = { w, h };
		// SetViewportRect updates the aspect AND gives the controller the viewport
		// origin it needs for zoom-to-cursor / the pivot ray (superset of OnResize).
		m_Orbit.SetViewportRect(app.GetViewportPos(), app.GetViewportSize());
		m_Orbit.SetNavigationStyle(m_CadNav ? Cosmic::NavStyle::CAD : Cosmic::NavStyle::Classic);
		m_Orbit.SetInertiaEnabled(m_Inertia);

		// ---- Editor id pre-pass (S5.4): render the ECS scene's entity IDs (+ depth)
		//      BEFORE the orbit update so click-picking and orbit-about-surface read
		//      fresh data, then handle click-to-select. RenderEditorIdPass rebinds the
		//      app viewport FBO. Uses last frame's camera — a hair behind, fine here.
		if (m_EditorMode && m_Picker && m_Scene)
		{
			RenderEditorIdPass();
			HandleEditorPicking();
		}

		// Camera input gating. The controller POLLS the mouse, so it must be told
		// when the cursor isn't really on the 3D view: enable control only while
		// the viewport is hovered (or an in-progress drag ran off the panel edge),
		// and yield to the gizmo — hover included, so the grab frame never orbits —
		// and to the ViewCube corner. All flags are from last frame's ImGui pass.
		{
			auto* ws = app.GetWorkspaceLayer();
			const bool vpHovered = ws && ws->IsViewportHovered();
			m_Orbit.SetControlEnabled((vpHovered || m_Orbit.IsDragging()) &&
			                          !m_GizmoActive && !m_GizmoOver && !m_NavCubeHovered);
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
		// Auto-orbit is disabled in editor mode and while a snap/frame blend runs
		// (it hard-sets yaw/pitch, which would cancel the animation every frame).
		if (m_AutoOrbit && !m_EditorMode && !m_Orbit.IsAnimating() &&
		    !Cosmic::Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_LEFT))
			m_Orbit.SetYawPitch(m_Orbit.GetYaw() + m_AutoOrbitSpeed * ts, m_Orbit.GetPitch());
		m_Orbit.OnUpdate(ts);

		// ---- Frame & snap hotkeys (S5.2): F frames selection/scene, Home = iso.
		//      Ignored while ImGui is capturing the keyboard (e.g. typing in a field).
		if (!ImGui::GetIO().WantCaptureKeyboard)
		{
			const bool fDown = Cosmic::Input::IsKeyPressed(CS_KEY_F);
			if (fDown && !m_KeyFWasDown)
			{
				glm::vec3 mn, mx;
				if (m_EditorMode && ComputeEntityWorldAABB(SelectedEntity(), mn, mx))
					m_Orbit.FrameBounds(mn, mx);
				else if (ComputeSceneWorldAABB(mn, mx))
					m_Orbit.FrameBounds(mn, mx);
			}
			m_KeyFWasDown = fDown;

			const bool homeDown = Cosmic::Input::IsKeyPressed(CS_KEY_HOME);
			if (homeDown && !m_KeyHomeWasDown)
				m_Orbit.SnapView(Cosmic::ViewPreset::Iso);
			m_KeyHomeWasDown = homeDown;

			// W/E/R cycle the gizmo mode (editor mode only). Idempotent set while
			// held — no edge state needed.
			if (m_EditorMode)
			{
				if (Cosmic::Input::IsKeyPressed(CS_KEY_W)) m_GizmoOp = Cosmic::Gizmo::Operation::Translate;
				if (Cosmic::Input::IsKeyPressed(CS_KEY_E)) m_GizmoOp = Cosmic::Gizmo::Operation::Rotate;
				if (Cosmic::Input::IsKeyPressed(CS_KEY_R)) m_GizmoOp = Cosmic::Gizmo::Operation::Scale;
			}
		}

		// ---- Trajectory ribbon (render-frame samples, ~20 Hz) ----
		m_TrailTimer += ts;
		if (!m_SimPaused && m_TrailTimer >= 0.05f)
		{
			m_TrailTimer = 0.0f;
			m_Trail.push_back(Cosmic::Math::NedToRender(m_PosNed));
			if (m_Trail.size() > k_TrailMax)
				m_Trail.erase(m_Trail.begin());   // 600 points; fine to shift
		}

		// Off-screen pre-passes (each renders into its own FBO). The nav cube (S5.3)
		// tracks the current camera orientation; the S4.6 picking panel renders its
		// own ID scene. Both leave another FBO bound, so we restore the app viewport
		// FBO afterwards for the main pass.
		if (m_NavCube && m_ShowNavCube)
			m_NavCube->Render(m_Orbit.GetCamera().GetViewMatrix());
		if (m_ShowPicking)
			RenderPickPass();

		// ---- Time-of-day (S7.3): drive the sun from a clock; the sky rebakes and
		//      the directional light + shadows follow. Sunrise 6h, zenith 12h, set 18h.
		if (m_TimeOfDay)
		{
			constexpr float kPi = 3.14159265358979f;
			const float f   = (m_TimeHours - 6.0f) / 12.0f;   // daytime parameter
			const float alt = std::sin(f * kPi) * (kPi * 0.5f);
			const float azi = f * kPi;                         // east -> west
			const glm::vec3 toSun = glm::normalize(glm::vec3(
				std::cos(alt) * std::cos(azi), std::sin(alt), std::cos(alt) * std::sin(azi)));
			m_LightDir = -toSun;
			Cosmic::Renderer3D::SetLightDirection(m_LightDir);
		}

		// Fog color tracks the sun elevation (warm near the horizon, cool at noon,
		// dark below) — used by the S7.2 height fog and as a plausible aerial tint.
		{
			const glm::vec3 toSun = glm::length(m_LightDir) > 1e-4f
				? -glm::normalize(m_LightDir) : glm::vec3(0.0f, 1.0f, 0.0f);
			const float e = glm::clamp(toSun.y, -1.0f, 1.0f);
			const glm::vec3 day{ 0.72f, 0.82f, 0.95f };
			const glm::vec3 sunset{ 0.85f, 0.60f, 0.45f };
			const glm::vec3 night{ 0.05f, 0.07f, 0.13f };
			m_SkyFogColor = e >= 0.0f ? glm::mix(sunset, day, glm::clamp(e, 0.0f, 1.0f))
			                          : glm::mix(sunset, night, glm::clamp(-e * 3.0f, 0.0f, 1.0f));
		}

		// ---- IBL + skybox (S6.3): keep the environment synced to the sun and feed
		//      the IBL set to Renderer3D so PBR materials sample it. Bake() re-runs
		//      only when the sun moved and leaves the default FBO bound, so the
		//      render-target selection below re-binds cleanly.
		m_Environment.SetSunDirection(-m_LightDir);
		// X1/X2 — physical atmosphere (+ sun-disc size): disabled leaves the procedural bake identical.
		m_Environment.SetPhysicalSky({ m_PhysicalSky, m_Turbidity, m_RayleighScale, m_MieScale, m_MieG, m_SunAngularSize });
		m_Environment.Bake();
		// X2 — ambient-intensity multiplier (1.0 = the shipped ambient term).
		Cosmic::Renderer3D::SetAmbientIntensity(m_AmbientIntensity);
		if (m_UseIBL)
			m_Environment.PushToRenderer();
		else
			Cosmic::Renderer3D::ClearIBL();

		// ---- Shadow depth pass (S6.4): render casters from the sun into the shadow
		//      map, then hand it to Renderer3D so the lit pass PCF-shadows the sun.
		//      Leaves the default FBO bound; render-target selection re-binds below.
		if (m_Shadows)
		{
			m_Shadow.SetLight(m_LightDir, { 0.0f, 2.0f, 0.0f }, 24.0f);
			m_Shadow.BeginDepthPass();
			DrawShadowCasters();
			m_Shadow.EndDepthPass();
			m_Shadow.PushToRenderer(m_ShadowBias);
		}
		else
		{
			Cosmic::Renderer3D::ClearShadow();
		}

		// ---- World systems (Phase 10): advance particles + capture the water
		//      reflection. Both run BEFORE the render-target selection — emitter
		//      Update is FBO-neutral (compute dispatch / SSBO upload) and the
		//      reflection pass restores the framebuffer it found bound. ----
		m_WorldTime += ts;
		{
			namespace Math = Cosmic::Math;
			const glm::vec3 posR = Math::NedToRender(m_PosNed);
			const glm::quat attR = Math::NedQuatToRender(m_AttNed);

			if (m_ShowTerrain && m_Terrain)
				m_TerrainProbe = m_Terrain->SampleHeight(posR.x, posR.z);   // S8.3 readout

			if (m_ShowParticles)
			{
				// Fire pit at the pad's edge: smoke + embers share the spot.
				const glm::mat4 pit = glm::translate(glm::mat4(1.0f), { -5.0f, 0.15f, -5.0f });
				if (m_Smoke)  { m_Smoke->SetTransform(pit);  m_Smoke->Update(ts, m_WorldTime);  }
				if (m_Embers)
				{
					// X3 — swirl the ember cone with curl noise when enabled.
					m_Embers->SetTurbulence(m_EmberNoise, m_EmberNoiseStrength, 0.5f, 2);
					m_Embers->SetTransform(pit);
					m_Embers->Update(ts, m_WorldTime);
				}
			}
			if (m_HeatHaze && m_Haze)
			{
				m_Haze->SetTransform(glm::translate(glm::mat4(1.0f), { -5.0f, 0.4f, -5.0f }));
				m_Haze->Update(ts, m_WorldTime);
			}
			if (m_ShowRibbon && m_Ribbon)
			{
				// Trail off the tail of the orbiting aircraft.
				const glm::vec3 tail = posR + attR * glm::vec3(0.0f, 0.05f, 1.3f);
				if (!m_SimPaused)
					m_Ribbon->AddPoint(tail, m_WorldTime);
				m_Ribbon->Update(m_WorldTime);
			}
		}

		// S9.1 reflection pass: mirror the camera about the water plane and
		// re-render the reflection-relevant world subset into the water's target.
		if (m_ShowWater && m_Water && m_Hdr)
		{
			glm::mat4 reflVP;
			glm::vec3 reflCam;
			const auto& cam = m_Orbit.GetCamera();
			if (m_Water->BeginReflection(cam.GetViewMatrix(), cam.GetProjectionMatrix(),
			                             cam.GetPosition(), reflVP, reflCam))
			{
				Cosmic::Renderer3D::BeginScene(reflVP, reflCam);
				DrawWaterReflectionWorld(reflVP);
				Cosmic::Renderer3D::EndScene();
				m_Water->EndReflection();
			}
		}

		// Pick the world-pass render target. With HDR (S6.1) on, the ENTIRE 3D scene
		// renders into the float scene target and is tonemapped into the viewport FBO
		// afterwards (before the 2D overlay, contract rule 7). Otherwise render
		// straight into the viewport FBO in LDR — the pre-S6 path, for A/B comparison.
		const bool useHdr = m_Hdr && w > 0.0f && h > 0.0f;
		if (useHdr)
		{
			m_PostFx.SetViewportSize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
			m_PostFx.BeginHDR({ 0.1f, 0.1f, 0.1f, 1.0f });   // bind + clear the HDR target
		}
		else
		{
			// Both pre-passes leave their own FBO bound; restore the viewport FBO.
			auto vfb = app.GetFrameBuffer();
			vfb->Bind();
			Cosmic::RenderCommand::SetViewport(0, 0, vfb->GetWidth(), vfb->GetHeight());
		}

		// =====================================================================
		// 3D pass (S1 + S2) — rendering into the HDR target (S6.1) or, with HDR
		// off, the viewport FBO already bound + cleared by the engine's
		// WorkspaceLayer before client layers update.
		// =====================================================================
		Cosmic::Renderer3D::BeginScene(m_Orbit.GetCamera());

		// Procedural-sky background (S6.3) — fills the HDR target behind the scene.
		// Drawn right after BeginScene (which uploads the camera UBO the skybox reads)
		// and before opaque geometry, which then draws over it.
		if (m_ShowSkybox)
			m_Environment.DrawSkybox(m_Orbit.GetCamera().GetViewProjectionMatrix());

		if (m_ShowGrid)
		{
			Cosmic::Renderer3D::DrawGrid(24.0f, 1.0f,
				{ 0.18f, 0.19f, 0.22f, 1.0f },   // minor
				{ 0.32f, 0.34f, 0.38f, 1.0f },   // major
				5);
			Cosmic::Renderer3D::DrawAxes(glm::mat4(1.0f), 2.0f);   // world origin tripod
		}

		// Terrain (S8): quadtree LOD around the camera; receives sun shadows and
		// IBL ambient through the shared scene bindings.
		if (m_ShowTerrain && m_Terrain)
			m_Terrain->Render(m_Orbit.GetCamera().GetPosition());

		// Buoyant box (S9.2): rides the CPU Gerstner query — the acceptance that
		// SampleHeight and the rendered surface agree.
		if (m_ShowWater && m_Water && m_BuoyBox)
		{
			const float bx = 10.0f, bz = 6.0f;
			const float by = m_Water->SampleHeight(bx, bz, m_WorldTime);
			const glm::vec3 n = m_Water->SampleNormal(bx, bz, m_WorldTime);
			const glm::vec3 axis = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), n);
			const float s = glm::length(axis);
			glm::mat4 tilt(1.0f);
			if (s > 1e-5f)
				tilt = glm::rotate(glm::mat4(1.0f), std::atan2(s, n.y), axis / s);
			Cosmic::Renderer3D::DrawMesh(m_BuoyBox,
				glm::translate(glm::mat4(1.0f), { bx, by + 0.12f, bz }) * tilt,
				{ 0.85f, 0.30f, 0.12f, 1.0f });
		}

		// Landing pad mesh under the orbit center (proves CreatePlane + Lambert
		// on a big flat face). Toggle m_MaterialPad to draw it through the S4.2
		// custom-material path (DemoChecker3D) instead of the flat Lambert color.
		{
			// Enlarge the ground when shadows are on so the aircraft's ~18 m orbit
			// casts onto a receiver (the flat Lambert pad now receives shadows, S6.4).
			const float padScale = m_Shadows ? 10.0f : 1.0f;
			const glm::mat4 padXform = glm::translate(glm::mat4(1.0f), { 0.0f, 0.01f, 0.0f })
				* glm::scale(glm::mat4(1.0f), { padScale, 1.0f, padScale });
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

		// Lighting v1 (S4.5) / PBR (S6.2): push the sun + two point lights into the
		// binding-0 UBO before drawing anything lit (MeshLit aircraft OR PBR spheres).
		if ((m_LitAircraft && m_LitMaterial) || (m_PbrSpheres && m_PbrMaterial))
		{
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

		if (m_LitAircraft && m_LitMaterial)
		{
			m_LitMaterial->Set("u_Shininess", m_Shininess);
			for (auto& [c, m] : m_LitPartMaterials)   // keep the per-part clones live
				m->Set("u_Shininess", m_Shininess);
		}

		DrawAircraft();

		// PBR metallic-roughness grid (S6.2) — best seen with HDR on so specular
		// highlights roll off rather than clip.
		if (m_PbrSpheres && m_PbrMaterial)
			DrawPbrSpheres();

		// Emissive sphere for the bloom demo (S6.6): an HDR-bright emitter that
		// blooms through the post chain when bloom is enabled.
		if (m_Bloom && m_EmitterMat && m_PbrSphere)
			Cosmic::Renderer3D::DrawMesh(m_PbrSphere,
				glm::translate(glm::mat4(1.0f), { 3.5f, 1.3f, 3.5f }), m_EmitterMat);

		// Auto-instancing demo (S12.3): 48 identical spheres through ONE shared
		// material whose PBRInstanced twin is registered — the queue collapses
		// the run into a single instanced draw (entityID stays -1 by design;
		// per-instance IDs are not in the instance SSBO).
		if (m_AutoInstDemo && m_AutoInstMaterial && m_PbrSphere)
		{
			constexpr int   kCount  = 48;
			constexpr float kRadius = 9.0f;
			for (int i = 0; i < kCount; ++i)
			{
				const float a = 6.2831853f * static_cast<float>(i) / kCount;
				const float r = kRadius + 1.5f * std::sin(a * 3.0f);
				const glm::vec3 pos{ r * std::cos(a), 0.45f + 0.8f * std::sin(a * 5.0f), r * std::sin(a) };
				const glm::mat4 xform = glm::translate(glm::mat4(1.0f), pos)
				                      * glm::scale(glm::mat4(1.0f), glm::vec3(0.55f));
				Cosmic::Renderer3D::DrawMesh(m_PbrSphere, xform, m_AutoInstMaterial);
			}
		}

		Cosmic::Renderer3D::EndScene();

		// ECS scene (S4.3): its own 3D pass (OnRender3D owns BeginScene/EndScene).
		// Also rendered in editor mode (S5.4/S5.5) so the selectable entities show.
		if ((m_EcsScene || m_EditorMode) && m_Scene)
			m_Scene->OnRender3D(m_Orbit.GetCamera());

		// Selection outline (S5.4): oriented AABB of the selected entity, on top.
		if (m_EditorMode)
			DrawSelectionOutline();

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
			Cosmic::RenderCommand::GpuMemoryBarrier(
				Cosmic::RenderCommand::GpuBarrier::VertexAttribArray |
				Cosmic::RenderCommand::GpuBarrier::ShaderStorage);

			// 3) Draw the points (no vertex attributes; positions come from the SSBO).
			//    u_ViewProjection is read from the binding-1 camera UBO (S6.2), still
			//    holding this frame's main-pass camera — no per-draw setter needed.
			m_PointShader->Bind();
			m_PointShader->SetFloat4("u_Color", { 0.55f, 0.85f, 1.0f, 1.0f });
			Cosmic::RenderCommand::DrawArrays(
				Cosmic::RenderCommand::PrimitiveTopology::Points, 0, k_ParticleCount);
		}

		// =====================================================================
		// World-system surface + transparents (Phase 10) — after ALL opaque
		// geometry: the water grabs the scene color for refraction and reads the
		// scene depth for shorelines; particles/ribbons are the transparent tail.
		// Both need the HDR target's attachments, so they ride the HDR path.
		// =====================================================================
		if (useHdr)
		{
			const auto& cam        = m_Orbit.GetCamera();
			const auto& sceneFbo   = m_PostFx.GetSceneTarget();
			const uint32_t colorID = sceneFbo->GetColorAttachmentRendererID(0);
			const uint32_t depthID = sceneFbo->GetDepthAttachmentRendererID();
			const glm::mat4 invVP  = glm::inverse(cam.GetViewProjectionMatrix());

			if (m_ShowWater && m_Water)
				m_Water->Render(cam.GetPosition(), m_WorldTime, cam.GetViewProjectionMatrix(),
				                colorID, depthID,
				                static_cast<uint32_t>(w), static_cast<uint32_t>(h));

			if (m_ShowParticles)
			{
				if (m_Smoke)  m_Smoke->Render(cam.GetViewMatrix(), depthID, invVP);
				if (m_Embers) m_Embers->Render(cam.GetViewMatrix(), depthID, invVP);
			}
			if (m_ShowRibbon && m_Ribbon)
				m_Ribbon->Render(cam.GetViewMatrix(), m_WorldTime);
		}

		// =====================================================================
		// HDR resolve (S6.1) — tonemap the float scene into the viewport FBO so
		// the 2D overlay composites on top in LDR. Overbright (>1.0) values from
		// the lit path roll off on the ACES shoulder instead of clipping; the
		// exposure slider scales the whole scene before the curve.
		// =====================================================================
		if (useHdr)
		{
			// SSAO + bloom read the HDR scene target just rendered; run them before
			// re-binding the viewport FBO for the tonemap/FXAA resolve.
			m_PostFx.SetSSAOEnabled(m_Ssao);
			m_PostFx.SetSSAOParams(m_SsaoRadius, m_SsaoBias);
			m_PostFx.SetBloomEnabled(m_Bloom);
			m_PostFx.SetBloomParams(m_BloomThreshold, m_BloomKnee, m_BloomIntensity);
			m_PostFx.SetFXAAEnabled(m_Fxaa);

			// Vignette (Q5): folded into the tonemap; off = byte-identical.
			m_PostFx.SetVignetteEnabled(m_Vignette);
			m_PostFx.SetVignetteParams(m_VignetteAmount, 0.85f, 0.35f, glm::vec3(0.0f));

			// Height fog (S7.2): depth-based inscatter toward the sky-tinted color.
			m_PostFx.SetFogEnabled(m_Fog);
			m_PostFx.SetFogParams(m_SkyFogColor, m_FogDensity, 0.12f, 0.0f);
			m_PostFx.SetCamera(m_Orbit.GetCamera().GetViewProjectionMatrix(),
			                   m_Orbit.GetCamera().GetPosition());

			// Sun shafts (S10.3): raymarch the shadow map — needs shadows on.
			m_PostFx.SetGodRaysEnabled(m_GodRays && m_Shadows);
			if (m_GodRays && m_Shadows)
				m_PostFx.SetSunShaftInputs(m_Shadow.GetDepthID(), m_Shadow.GetLightViewProj(),
				                           m_LightDir, m_SunColor, m_SunIntensity);

			// Heat haze (S10.5): render the distortion emitter into the offset
			// field the tonemap displaces by. BeginDistortion binds its own target
			// and EndDistortion restores this one; RenderEffects re-asserts every
			// viewport it needs afterwards.
			m_PostFx.SetHeatHazeEnabled(m_HeatHaze);
			if (m_HeatHaze && m_Haze && m_PostFx.BeginDistortion())
			{
				m_Haze->RenderDistortion(m_Orbit.GetCamera().GetViewMatrix(),
				                         m_PostFx.GetSceneTarget()->GetDepthAttachmentRendererID(),
				                         glm::inverse(m_Orbit.GetCamera().GetViewProjectionMatrix()));
				m_PostFx.EndDistortion();
			}

			m_PostFx.RenderEffects(m_Orbit.GetCamera().GetProjectionMatrix());

			auto vfb = app.GetFrameBuffer();
			vfb->Bind();
			Cosmic::RenderCommand::SetViewport(0, 0, vfb->GetWidth(), vfb->GetHeight());
			m_PostFx.SetGamma(m_Gamma);          // X2 (default 2.2 = byte-identical)
			m_PostFx.Composite(m_Exposure);
		}

		// =====================================================================
		// 2D overlay pass — no 2D regressions (doc 05 contract rule 6).
		// =====================================================================
		if (m_Show2D)
			Draw2DOverlay();
	}

	// =========================================================================
	// S9.1 — the world subset re-rendered into the water's reflection target.
	// Called inside the mirrored Renderer3D scene (camera UBO = mirrored VP).
	// A subset by design: sky + terrain + the aircraft read clearly in the
	// reflection; the rest isn't worth a second full pass at this tier.
	// =========================================================================
	void Engine3DDemo::DrawWaterReflectionWorld(const glm::mat4& mirroredViewProj)
	{
		if (m_ShowSkybox)
			m_Environment.DrawSkybox(mirroredViewProj);

		if (m_ShowTerrain && m_Terrain)
		{
			// LOD selection uses the REAL camera position so the reflected
			// terrain tessellation matches the main view exactly (no LOD seam
			// between a surface and its reflection).
			m_Terrain->Render(m_Orbit.GetCamera().GetPosition());
		}

		DrawAircraft();
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
		// In-viewport tools first (gizmo + ViewCube), then the docked panels.
		// Each panel is its own window bound to an engine dock port in OnAttach —
		// see WorkspaceLayer::DockWindow.
		DrawViewportOverlay();

		DrawCameraPanel();       // "Camera & Views"        (left, top)
		DrawEditorPanel();       // "Editor Tools"          (left, middle)
		DrawRenderingPanel();    // "Rendering & Lighting"  (left, bottom)
		DrawSimulationPanel();   // "Simulation & Timing"   (right, top)
		DrawFeatureDemosPanel(); // "Feature Demos"         (right, bottom)
	}

	// =========================================================================
	// Viewport overlay — the transform gizmo (S5.5) and the ViewCube (S5.3)
	// live ON the 3D image, not in a side panel (the CAD convention). Drawn by
	// appending to the engine's Viewport window so ImGuizmo hover-tests against
	// the correct host window (see graphics/Gizmo.h FRAME PROTOCOL).
	// =========================================================================

	void Engine3DDemo::DrawViewportOverlay()
	{
		m_NavCubeHovered = false;

		auto& app = Cosmic::Application::Get();
		auto* ws  = app.GetWorkspaceLayer();
		if (!ws || (!m_EditorMode && !m_ShowNavCube))
		{
			m_GizmoActive = false;
			m_GizmoOver   = false;
			return;
		}

		const glm::vec2 vpPos  = app.GetViewportPos();
		const glm::vec2 vpSize = app.GetViewportSize();
		const bool      vpValid = vpSize.x > 0.0f && vpSize.y > 0.0f;

		bool gizmoActive = false;
		bool gizmoOver   = false;

		if (ws->BeginViewportOverlay() && vpValid)
		{
			// ---- Transform gizmo (S5.5) on the selected ECS entity ----
			if (m_EditorMode)
			{
				Cosmic::Gizmo::SetRect(vpPos.x, vpPos.y, vpSize.x, vpSize.y);

				if (Cosmic::Entity sel = SelectedEntity())
				{
					auto& t = sel.GetComponent<Cosmic::TransformComponent>();
					const float snap = m_GizmoSnap ? m_SnapValue : 0.0f;
					Cosmic::Gizmo::Manipulate(m_Orbit.GetCamera(), t, m_GizmoOp, m_GizmoSpace, snap);
				}

				gizmoActive = Cosmic::Gizmo::IsUsing();
				gizmoOver   = Cosmic::Gizmo::IsOver();
			}

			// ---- ViewCube (S5.3), top-right corner — click a face to snap ----
			if (m_ShowNavCube && m_NavCube)
			{
				const float sz     = static_cast<float>(m_NavCube->GetSize());
				const float margin = 12.0f;
				ImGui::SetCursorScreenPos(ImVec2(vpPos.x + vpSize.x - sz - margin,
				                                 vpPos.y + margin));
				ImGui::Image((ImTextureID)(intptr_t)m_NavCube->GetTextureID(),
					{ sz, sz }, ImVec2(0, 1), ImVec2(1, 0));   // flip V (GL bottom-left)

				if (ImGui::IsItemHovered())
				{
					// The cube owns this corner: click-to-select and camera drags
					// both yield (flag read next frame in OnUpdate — one-frame
					// hover lag is fine).
					m_NavCubeHovered = true;

					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						const ImVec2 imgMin = ImGui::GetItemRectMin();
						const ImVec2 m      = ImGui::GetMousePos();
						Cosmic::ViewPreset preset;
						if (m_NavCube->PickFace((m.x - imgMin.x) / sz, (m.y - imgMin.y) / sz, preset))
							m_Orbit.SnapView(preset);
					}
				}
			}
		}
		ws->EndViewportOverlay();

		m_GizmoActive = gizmoActive;
		m_GizmoOver   = gizmoOver;
	}

	// =========================================================================
	// "Camera & Views" — navigation style, standard views, framing, hotkeys.
	// =========================================================================

	void Engine3DDemo::DrawCameraPanel()
	{
		ImGui::Begin("Camera & Views");

		Cosmic::UI::Fonts::Push("Roboto-Bold", Cosmic::UI::Fonts::SizeHeading);
		ImGui::TextUnformatted("3D Engine Demo");
		Cosmic::UI::Fonts::Pop();
		ImGui::TextDisabled("Phases 4-8: 3D foundations + CAD navigation & editing");

		// ---- Navigation (S1 / S5.1) ----
		ImGui::SeparatorText("Navigation");

		ImGui::Checkbox("CAD style (S5.1)", &m_CadNav);
		ImGui::SameLine();
		ImGui::Checkbox("Inertia", &m_Inertia);

		if (m_CadNav)
			ImGui::TextWrapped("MMB drag orbits about the point under the cursor. "
			                   "Ctrl+MMB pans, Shift+MMB dollies, scroll zooms toward the cursor. "
			                   "LMB stays free for selection.");
		else
			ImGui::TextWrapped("LMB drag orbits, RMB drag pans, scroll zooms to the view center.");
		ImGui::TextDisabled("The cursor must be over the viewport for camera input.");

		ImGui::Spacing();
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

		// ---- Standard views (S5.2) + ViewCube toggle (S5.3) ----
		ImGui::SeparatorText("Standard views");

		auto snapBtn = [&](const char* label, Cosmic::ViewPreset p)
		{
			if (ImGui::Button(label)) m_Orbit.SnapView(p);
		};
		snapBtn("Front", Cosmic::ViewPreset::Front); ImGui::SameLine();
		snapBtn("Back",  Cosmic::ViewPreset::Back);  ImGui::SameLine();
		snapBtn("Left",  Cosmic::ViewPreset::Left);  ImGui::SameLine();
		snapBtn("Right", Cosmic::ViewPreset::Right);
		snapBtn("Top",   Cosmic::ViewPreset::Top);   ImGui::SameLine();
		snapBtn("Bottom",Cosmic::ViewPreset::Bottom);ImGui::SameLine();
		snapBtn("Iso",   Cosmic::ViewPreset::Iso);

		if (ImGui::Button("Frame selection (F)"))
		{
			glm::vec3 mn, mx;
			if (ComputeEntityWorldAABB(SelectedEntity(), mn, mx)) m_Orbit.FrameBounds(mn, mx);
		}
		ImGui::SameLine();
		if (ImGui::Button("Frame scene"))
		{
			glm::vec3 mn, mx;
			if (ComputeSceneWorldAABB(mn, mx)) m_Orbit.FrameBounds(mn, mx);
		}

		ImGui::Checkbox("Show ViewCube (S5.3)", &m_ShowNavCube);
		ImGui::TextDisabled("The cube sits in the viewport's top-right corner;\n"
		                    "click one of its faces to snap to that view.");

		// ---- Hotkeys (also see the Editor Tools panel) ----
		ImGui::SeparatorText("Hotkeys");
		ImGui::BulletText("F        frame selection (or the whole scene)");
		ImGui::BulletText("Home   snap to the isometric view");
		ImGui::BulletText("W/E/R  gizmo: move / rotate / scale (editor mode)");

		ImGui::End();
	}

	// =========================================================================
	// "Editor Tools" — selection + transform gizmo (S5.4 / S5.5).
	// =========================================================================

	void Engine3DDemo::DrawEditorPanel()
	{
		ImGui::Begin("Editor Tools");

		ImGui::Checkbox("Editor mode", &m_EditorMode);
		ImGui::SameLine(); ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Renders + picks the ECS scene (the S4.3 entities). Selection is the\n"
			                  "shared EntitySelection bus, so every panel sees the same entity.");

		ImGui::SeparatorText("How to use");
		ImGui::TextDisabled("1. Enable Editor mode above.");
		ImGui::TextDisabled("2. Left-click one of the colored meshes in the viewport\n"
		                    "   (the group left of center) - an orange outline appears.");
		ImGui::TextDisabled("3. Drag the gizmo's arrows / rings / boxes to transform it.");
		ImGui::TextDisabled("4. W / E / R (or the buttons below) switch the gizmo mode.");
		ImGui::TextDisabled("5. Click empty space to deselect.");

		ImGui::SeparatorText("Selection & gizmo");
		{
			Cosmic::Entity sel = SelectedEntity();
			if (sel && sel.HasComponent<Cosmic::TagComponent>())
				ImGui::Text("Selected: %s", sel.GetComponent<Cosmic::TagComponent>().Tag.c_str());
			else
				ImGui::TextDisabled("Selected: (none)");
		}

		ImGui::BeginDisabled(!m_EditorMode);
		int op = static_cast<int>(m_GizmoOp);
		ImGui::TextUnformatted("Gizmo"); ImGui::SameLine();
		ImGui::RadioButton("Move", &op, 0);   ImGui::SameLine();
		ImGui::RadioButton("Rotate", &op, 1); ImGui::SameLine();
		ImGui::RadioButton("Scale", &op, 2);
		m_GizmoOp = static_cast<Cosmic::Gizmo::Operation>(op);

		int space = static_cast<int>(m_GizmoSpace);
		ImGui::TextUnformatted("Space"); ImGui::SameLine();
		ImGui::RadioButton("World", &space, static_cast<int>(Cosmic::Gizmo::Space::World)); ImGui::SameLine();
		ImGui::RadioButton("Local", &space, static_cast<int>(Cosmic::Gizmo::Space::Local));
		m_GizmoSpace = static_cast<Cosmic::Gizmo::Space>(space);

		ImGui::Checkbox("Snap", &m_GizmoSnap); ImGui::SameLine();
		ImGui::SetNextItemWidth(90.0f);
		ImGui::InputFloat("##snapval", &m_SnapValue, 0.0f, 0.0f, "%.2f");
		ImGui::SameLine(); ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Snap increment: world units for move/scale, degrees for rotate.");
		ImGui::EndDisabled();

		ImGui::End();
	}

	// =========================================================================
	// "Rendering & Lighting" — draw toggles, scene content, lights.
	// =========================================================================

	void Engine3DDemo::DrawRenderingPanel()
	{
		ImGui::Begin("Rendering & Lighting");

		ImGui::SeparatorText("HDR pipeline (S6.1)");
		ImGui::Checkbox("HDR + tonemap", &m_Hdr);
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Renders the 3D scene into an RGBA16F float target, then a\n"
			                  "fullscreen ACES tonemap resolves it into the viewport (the 2D\n"
			                  "overlay composites after). Toggle off to render straight to LDR\n"
			                  "for an A/B comparison. HDR on looks brighter/filmic (final gamma).");
		ImGui::BeginDisabled(!m_Hdr);
		ImGui::SliderFloat("Exposure", &m_Exposure, 0.05f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
		ImGui::SliderFloat("Gamma", &m_Gamma, 1.0f, 3.0f, "%.2f");   // X2 (2.2 = shipped curve)
		ImGui::SameLine(); ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Tonemap output gamma (X2), previously hardcoded at 2.2. 2.2 is\n"
			                  "byte-identical to the shipped curve; lower brightens mid-tones.");
		ImGui::EndDisabled();
		ImGui::TextDisabled("Crank exposure past ~2x: highlights roll off, not clip to white.");

		ImGui::SeparatorText("Environment / IBL (S6.3)");
		ImGui::Checkbox("Skybox", &m_ShowSkybox);
		ImGui::SameLine();
		ImGui::Checkbox("IBL", &m_UseIBL);
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("A procedural analytic sky is baked into a cubemap and convolved into\n"
			                  "diffuse-irradiance + prefiltered-specular maps + a BRDF LUT. PBR\n"
			                  "materials sample them for image-based ambient; the same sky draws as\n"
			                  "the background. Drag 'Light dir' below to move the sun and rebake.");
		ImGui::SliderFloat("Ambient intensity", &m_AmbientIntensity, 0.0f, 4.0f, "%.2f");   // X2
		ImGui::SameLine(); ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("X2 — scales the PBR ambient/IBL term (1.0 = the shipped level,\n"
			                  "byte-identical). Drop toward 0 for a moodier, contact-lit look.");

		ImGui::SeparatorText("Sun shadows (S6.4)");
		ImGui::Checkbox("Shadows", &m_Shadows);
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("A 2k depth map is rendered from the sun each frame; the lit shaders\n"
			                  "3x3-PCF it. The ground enlarges so the aircraft's orbit casts onto it.\n"
			                  "Front-face culling in the depth pass + a slope bias fight acne.");
		ImGui::BeginDisabled(!m_Shadows);
		ImGui::SliderFloat("Shadow bias", &m_ShadowBias, 0.0002f, 0.01f, "%.4f", ImGuiSliderFlags_Logarithmic);
		ImGui::EndDisabled();

		ImGui::SeparatorText("Post effects (S6.5-6.7)");
		ImGui::BeginDisabled(!m_Hdr);
		ImGui::TextDisabled(m_Hdr ? "SSAO + bloom read the HDR target." : "Enable HDR to use post effects.");

		ImGui::Checkbox("SSAO", &m_Ssao);
		ImGui::SameLine(); ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Screen-space ambient occlusion, reconstructed from the scene depth\n"
			                  "(half-res, 32-sample hemisphere + 4x4 blur). Darkens contact crevices.\n"
			                  "Applied over the whole image in the tonemap (documented simplification).");
		if (m_Ssao)
		{
			ImGui::SliderFloat("AO radius", &m_SsaoRadius, 0.1f, 2.0f, "%.2f");
			ImGui::SliderFloat("AO bias",   &m_SsaoBias, 0.0f, 0.1f, "%.3f");
		}

		ImGui::Checkbox("Bloom", &m_Bloom);
		ImGui::SameLine(); ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Soft-knee threshold + separable Gaussian on the HDR buffer, added\n"
			                  "back before the ACES curve. Turn it on to see the emissive sphere glow.");
		if (m_Bloom)
		{
			ImGui::SliderFloat("Threshold", &m_BloomThreshold, 0.2f, 4.0f, "%.2f");
			ImGui::SliderFloat("Intensity", &m_BloomIntensity, 0.0f, 2.0f, "%.2f");
		}

		ImGui::Checkbox("FXAA", &m_Fxaa);
		ImGui::SameLine(); ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Fast approximate anti-aliasing — the final LDR edge blend. Toggle\n"
			                  "on/off and watch the grid / mesh edges for reduced crawl.");
		ImGui::EndDisabled();

		ImGui::Checkbox("Vignette", &m_Vignette);   // Q5 (post-tonemap; off = byte-identical)
		if (m_Vignette)
			ImGui::SliderFloat("Vignette amount", &m_VignetteAmount, 0.0f, 1.0f, "%.2f");

		ImGui::SeparatorText("Sky / fog / time-of-day (S7)");

		ImGui::Checkbox("Physical atmosphere (X1)", &m_PhysicalSky);
		ImGui::SameLine(); ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("SkyMode::Physical — an analytic Rayleigh+Mie single-scattering sky\n"
			                  "baked into the SAME cube the skybox and IBL read, so lighting always\n"
			                  "matches the visible sky. Raise Turbidity to haze the horizon; scrub\n"
			                  "Time of day to watch the dawn/noon/dusk color ramp. Off = the shipped\n"
			                  "procedural bake, byte-identical.");
		if (m_PhysicalSky)
		{
			ImGui::SliderFloat("Turbidity",  &m_Turbidity,      1.0f, 10.0f, "%.1f");
			ImGui::SliderFloat("Rayleigh",   &m_RayleighScale,  0.0f, 4.0f,  "%.2f");
			ImGui::SliderFloat("Mie",        &m_MieScale,       0.0f, 4.0f,  "%.2f");
			ImGui::SliderFloat("Mie G",      &m_MieG,           0.0f, 0.99f, "%.2f");
			ImGui::SliderFloat("Sun size",   &m_SunAngularSize, 0.1f, 10.0f, "%.2f deg");   // X2
		}

		ImGui::BeginDisabled(!m_Hdr);
		ImGui::Checkbox("Height fog", &m_Fog);
		ImGui::SameLine(); ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Depth-based exponential height fog + aerial perspective (S7.2). The\n"
			                  "fog color tracks the sun elevation. Distant geometry fades into the sky.");
		if (m_Fog)
			ImGui::SliderFloat("Fog density", &m_FogDensity, 0.0f, 0.08f, "%.3f");

		ImGui::Checkbox("Time of day", &m_TimeOfDay);
		ImGui::SameLine(); ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Drive the sun from a clock (S7.3): the analytic sky rebakes, and the\n"
			                  "directional light, IBL and shadows all follow. Scrub for morning ->\n"
			                  "noon -> sunset. (Overrides the manual Light dir below while enabled.)");
		if (m_TimeOfDay)
			ImGui::SliderFloat("Hour", &m_TimeHours, 4.0f, 20.0f, "%.1f h");
		ImGui::EndDisabled();

		ImGui::SeparatorText("World systems (Phase 10 / S8-S10)");

		ImGui::BeginDisabled(!m_Terrain);
		ImGui::Checkbox("Terrain (S8)", &m_ShowTerrain);
		ImGui::EndDisabled();
		ImGui::SameLine(); ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("256 m procedural island: chunked quadtree LOD with skirts, 4-layer\n"
			                  "auto-splat (grass/rock/snow/sand by height+slope), triplanar on steep\n"
			                  "slopes. Receives sun shadows + IBL ambient. Orbit far out and back in\n"
			                  "to watch patches split/merge.");
		if (m_ShowTerrain && m_Terrain)
			ImGui::TextDisabled("patches: %u   SampleHeight@aircraft: %.2f m",
			                    m_Terrain->GetLastDrawnNodeCount(), m_TerrainProbe);

		ImGui::BeginDisabled(!m_Water || !m_Hdr);
		ImGui::Checkbox("Water (S9)", &m_ShowWater);
		ImGui::EndDisabled();
		ImGui::SameLine(); ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Tier-1 lake at y = -5 (turn Terrain on for shorelines): Gerstner swell,\n"
			                  "planar reflections (mirrored re-render + oblique clip), refraction from\n"
			                  "a scene-color grab, depth-fade absorption, Fresnel, sun glint, shoreline\n"
			                  "foam. The orange box bobs via the CPU SampleHeight query (S9.2).\n"
			                  "Needs HDR on (reads the float scene target).");

		ImGui::BeginDisabled(!m_Smoke || !m_Hdr);
		ImGui::Checkbox("Particles (S10.1)", &m_ShowParticles);
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!m_Ribbon || !m_Hdr);
		ImGui::Checkbox("Ribbon (S10.2)", &m_ShowRibbon);
		ImGui::EndDisabled();
		ImGui::SameLine(); ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("GPU compute pool + attribute-less billboards: a soft-particle flipbook\n"
			                  "smoke plume (alpha) and HDR ember sparks (additive - enable Bloom to see\n"
			                  "them glow) at the fire pit (-5, 0, -5). Ribbon: additive camera-facing\n"
			                  "trail off the aircraft tail. Both need HDR (soft particles read scene depth).");

		ImGui::BeginDisabled(!m_Embers || !m_Hdr || !m_ShowParticles);
		ImGui::Checkbox("Ember curl noise (X3)", &m_EmberNoise);
		ImGui::SameLine(); ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("X3 - a divergence-free curl of a 3D value-noise field adds a swirling\n"
			                  "acceleration to the ember cone (same term on the GPU compute path and\n"
			                  "the unit-tested CPU mirror). Off = the shipped straight-rising sparks.");
		if (m_EmberNoise)
			ImGui::SliderFloat("Noise strength##ember", &m_EmberNoiseStrength, 0.0f, 20.0f, "%.1f");
		ImGui::EndDisabled();

		ImGui::BeginDisabled(!m_Shadows || !m_Hdr);
		ImGui::Checkbox("God rays (S10.3)", &m_GodRays);
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!m_Haze || !m_Hdr);
		ImGui::Checkbox("Heat haze (S10.5)", &m_HeatHaze);
		ImGui::EndDisabled();
		ImGui::SameLine(); ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("God rays: raymarched sun shafts against the shadow map (needs Shadows;\n"
			                  "best looking toward a low sun through the aircraft/terrain - try Time of\n"
			                  "day ~7h). Heat haze: distortion particles above the fire pit displace the\n"
			                  "tonemap's scene fetch - the air shimmers.");

		ImGui::SeparatorText("Overlays");
		ImGui::Checkbox("Grid", &m_ShowGrid);           ImGui::SameLine();
		ImGui::Checkbox("Body axes", &m_ShowAxes);      ImGui::SameLine();
		ImGui::Checkbox("Bounds", &m_ShowWireBox);
		ImGui::Checkbox("Trail", &m_ShowTrail);         ImGui::SameLine();
		ImGui::Checkbox("2D overlay", &m_Show2D);

		ImGui::SeparatorText("Scene content");
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
			                  "(off to the left, y=3). One cylinder uses the quaternion path.\n"
			                  "Editor mode always renders them - they are the pickable set.");

		ImGui::BeginDisabled(!m_DuckModel);
		ImGui::Checkbox("glTF Duck (S4.4b)", &m_ShowGltf);
		ImGui::EndDisabled();
		if (!m_GltfCacheResult.empty())
			ImGui::TextDisabled("%s", m_GltfCacheResult.c_str());

		ImGui::SeparatorText("Sun & ambient");
		if (ImGui::SliderFloat3("Light dir", &m_LightDir.x, -1.0f, 1.0f, "%.2f"))
			Cosmic::Renderer3D::SetLightDirection(m_LightDir);
		if (ImGui::SliderFloat("Ambient", &m_Ambient, 0.0f, 1.0f, "%.2f"))
			Cosmic::Renderer3D::SetAmbient(m_Ambient);

		ImGui::SeparatorText("PBR (S6.2)");
		ImGui::BeginDisabled(!m_PbrMaterial);
		ImGui::Checkbox("PBR sphere grid", &m_PbrSpheres);
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Cook-Torrance metallic-roughness grid: roughness 0->1 across,\n"
			                  "metallic 0->1 up. Lit by the sun + the two point lights below.\n"
			                  "Turn HDR on (above) so the metallic highlights roll off, not clip.");
		if (m_PbrSpheres)
			ImGui::ColorEdit3("PBR albedo", &m_PbrAlbedo.x);

		ImGui::SeparatorText("Lighting v1 (S4.5)");
		ImGui::BeginDisabled(!m_LitMaterial);
		ImGui::Checkbox("Lit aircraft", &m_LitAircraft);
		ImGui::EndDisabled();
		ImGui::TextDisabled("MeshLit material + lights UBO; 'Light dir' above is the sun.");

		if (m_LitAircraft || m_PbrSpheres)
		{
			ImGui::ColorEdit3("Sun color", &m_SunColor.x);
			ImGui::SliderFloat("Sun intensity", &m_SunIntensity, 0.0f, 4.0f, "%.2f");
			if (m_LitAircraft)
				ImGui::SliderFloat("Shininess", &m_Shininess, 1.0f, 128.0f, "%.0f");
			ImGui::SliderFloat3("Red pos",  &m_P0Pos.x, -10.0f, 10.0f, "%.1f");
			ImGui::SliderFloat3("Blue pos", &m_P1Pos.x, -10.0f, 10.0f, "%.1f");
			ImGui::SliderFloat("Point radius", &m_PointRadius, 1.0f, 30.0f, "%.0f");
		}

		// S12 acceptance readout: the cull rate (S12.1) and the queue's
		// auto-instancing wins (S12.3) across every pass this frame. Toggle
		// culling and orbit so part of the scene leaves the view — Submitted
		// stays put, Culled climbs, Draw calls drop.
		ImGui::SeparatorText("Performance (S12)");
		{
			bool culling = Cosmic::Renderer3D::IsFrustumCullingEnabled();
			if (ImGui::Checkbox("Frustum culling (S12.1)", &culling))
				Cosmic::Renderer3D::SetFrustumCullingEnabled(culling);

			ImGui::BeginDisabled(!m_AutoInstMaterial);
			ImGui::Checkbox("Auto-instance ring (S12.3)", &m_AutoInstDemo);
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("48 identical spheres through one shared material with a\n"
				                  "registered PBRInstanced twin - the queue collapses them\n"
				                  "into a single instanced draw (see Auto-instanced below).");

			const auto stats = Cosmic::Renderer3D::GetStats();
			ImGui::Text("Submitted %u   culled %u (%.0f%%)",
				stats.MeshesSubmitted, stats.MeshesCulled,
				stats.MeshesSubmitted > 0
					? 100.0f * static_cast<float>(stats.MeshesCulled) / static_cast<float>(stats.MeshesSubmitted)
					: 0.0f);
			ImGui::Text("Mesh draw calls %u (singles %u)", stats.DrawCalls, stats.MeshesDrawn);
			ImGui::Text("Auto-instanced %u meshes in %u draws",
				stats.AutoInstancedMeshes, stats.AutoInstanceBatches);
			if (stats.ExplicitInstanceDraws > 0)
				ImGui::Text("Explicit instancing %u draws / %u instances",
					stats.ExplicitInstanceDraws, stats.ExplicitInstances);
			ImGui::TextDisabled("LOD demo: 'ECS scene' on, zoom out past 15/35 m;\n"
			                    "the purple entity swaps sphere->cone->box, culls at 90 m.");
		}

		ImGui::End();
	}

	// =========================================================================
	// "Simulation & Timing" — the E3 kinematic sim + the E1 fixed-timestep rig.
	// =========================================================================

	void Engine3DDemo::DrawSimulationPanel()
	{
		ImGui::Begin("Simulation & Timing");

		ImGui::SeparatorText("Aircraft sim (E3 - NED + quaternions)");
		ImGui::Checkbox("Pause", &m_SimPaused);
		ImGui::SliderFloat("Speed (m/s)", &m_SpeedMs, 2.0f, 20.0f, "%.1f");
		ImGui::SliderFloat("Bank (deg)", &m_BankDeg, -45.0f, 45.0f, "%.0f");

		const glm::vec3 euler = Cosmic::Math::EulerZYXFromQuat(m_AttNed);
		ImGui::Text("NED pos  N %.1f  E %.1f  D %.1f", m_PosNed.x, m_PosNed.y, m_PosNed.z);
		ImGui::Text("Attitude R %.0f  P %.0f  Y %.0f", euler.x, euler.y, euler.z);

		if (ImGui::Button("Clear trail"))
			m_Trail.clear();

		ImGui::SeparatorText("Fixed timestep (E1)");
		auto& app = Cosmic::Application::Get();
		if (ImGui::SliderFloat("Engine rate (Hz)", &m_FixedHzUi, 30.0f, 480.0f, "%.0f"))
			app.SetFixedTimestepHz(m_FixedHzUi);

		ImGui::Text("Configured: %.0f Hz", app.GetFixedTimestepHz());
		ImGui::Text("Measured OnFixedUpdate: %.0f Hz", m_MeasuredFixedHz);
		ImGui::TextDisabled("240 Hz should read ~4x the 60 Hz default.");

		ImGui::End();
	}

	// =========================================================================
	// "Feature Demos" — asset cache, MRT picking, GPU compute acceptance rigs.
	// =========================================================================

	void Engine3DDemo::DrawFeatureDemosPanel()
	{
		ImGui::Begin("Feature Demos");

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
		// Mouse events (scroll zoom) are only meant for the 3D view — don't zoom
		// the camera while the cursor sits over a side panel. Non-mouse events
		// (window resize → aspect) always pass through.
		auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
		const bool vpHovered = ws && ws->IsViewportHovered();
		if (!e.IsInCategory(Cosmic::EventCategoryMouse) || vpHovered || m_Orbit.IsDragging())
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
