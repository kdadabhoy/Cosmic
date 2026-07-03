// SceneRenderer.cpp — F2 engine-owned frame orchestration. See SceneRenderer.h.

#include "renderer/SceneRenderer.h"

#include "renderer/RenderCommand.h"
#include "renderer/InstanceSet.h"
#include "renderer/CoverageCapture.h"
#include "graphics/FrameBuffer.h"
#include "graphics/Mesh.h"
#include "graphics/Model.h"
#include "graphics/Material.h"
#include "camera/Camera.h"
#include "terrain/Terrain.h"
#include "water/Water.h"
#include "particles/ParticleSystem.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "core/Log.h"

#include <glm/glm.hpp>

namespace Cosmic
{
	namespace
	{
		// Minimal Camera adapter so Scene::OnRender3D (which takes const Camera&)
		// can be driven from the frame's loose matrices. Camera getters return
		// const&, so the four values are cached as members (Camera.h contract).
		class MatrixCamera : public Camera
		{
		public:
			MatrixCamera(const glm::mat4& view, const glm::mat4& proj,
			             const glm::mat4& viewProj, const glm::vec3& pos)
				: m_View(view), m_Proj(proj), m_ViewProj(viewProj), m_Pos(pos) {}

			const glm::mat4& GetViewMatrix()           const override { return m_View; }
			const glm::mat4& GetProjectionMatrix()     const override { return m_Proj; }
			const glm::mat4& GetViewProjectionMatrix() const override { return m_ViewProj; }
			const glm::vec3& GetPosition()             const override { return m_Pos; }

		private:
			glm::mat4 m_View, m_Proj, m_ViewProj;
			glm::vec3 m_Pos;
		};
	}

	// =========================================================================
	// SceneDrawContext — routed submits
	// =========================================================================

	void SceneDrawContext::DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform,
	                                const glm::vec4& color, int entityID) const
	{
		if (Pass == ScenePass::ShadowDepth)
		{
			if (m_Shadow && mesh)
				m_Shadow->DrawCaster(mesh, transform);
			return;
		}
		if (Pass == ScenePass::TopDownDepth)
		{
			if (m_Coverage && mesh)
				m_Coverage->DrawCaster(mesh, transform);
			return;
		}
		Renderer3D::DrawMesh(mesh, transform, color, entityID);
	}

	void SceneDrawContext::DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform,
	                                const Ref<Material>& material, int entityID) const
	{
		if (Pass == ScenePass::ShadowDepth)
		{
			if (m_Shadow && mesh)
				m_Shadow->DrawCaster(mesh, transform);      // material ignored for depth
			return;
		}
		if (Pass == ScenePass::TopDownDepth)
		{
			if (m_Coverage && mesh)
				m_Coverage->DrawCaster(mesh, transform);
			return;
		}
		Renderer3D::DrawMesh(mesh, transform, material, entityID);
	}

	void SceneDrawContext::DrawModel(const Ref<Model>& model, const glm::mat4& transform, int entityID) const
	{
		if (Pass == ScenePass::ShadowDepth || Pass == ScenePass::TopDownDepth)
		{
			if (model)
				for (const auto& part : model->GetParts())
					if (part.Geometry)
					{
						if (Pass == ScenePass::ShadowDepth && m_Shadow)
							m_Shadow->DrawCaster(part.Geometry, transform);
						else if (Pass == ScenePass::TopDownDepth && m_Coverage)
							m_Coverage->DrawCaster(part.Geometry, transform);
					}
			return;
		}
		Renderer3D::DrawModel(model, transform, entityID);
	}

	void SceneDrawContext::DrawMeshInstanced(const Ref<Mesh>& mesh, const Ref<Material>& material,
	                                         const Ref<InstanceSet>& instances, uint32_t count, int entityID) const
	{
		if (Pass == ScenePass::ShadowDepth)
		{
			if (m_Shadow && mesh)
				m_Shadow->DrawCasterInstanced(mesh, instances, count);   // material/entityID ignored
			return;
		}
		if (Pass == ScenePass::TopDownDepth)
		{
			if (m_Coverage && mesh)
				m_Coverage->DrawCasterInstanced(mesh, instances, count);
			return;
		}
		Renderer3D::DrawMeshInstanced(mesh, material, instances, count, entityID);
	}

	// =========================================================================
	// SceneRenderDesc
	// =========================================================================

	void SceneRenderDesc::SetCamera(const Camera& camera)
	{
		View           = camera.GetViewMatrix();
		Projection     = camera.GetProjectionMatrix();
		CameraPosition = camera.GetPosition();
	}

	// =========================================================================
	// SceneRenderer — lifecycle
	// =========================================================================

	SceneRenderer::~SceneRenderer()
	{
		// Members' own dtors release their GPU resources; Shutdown() is idempotent
		// and also clears the Renderer3D registration, so call it if still live.
		if (m_Initialized)
			Shutdown();
	}

	void SceneRenderer::Init(uint32_t width, uint32_t height, uint32_t shadowMapSize)
	{
		if (m_Initialized)
			return;

		m_Width  = width  > 0 ? width  : 1;
		m_Height = height > 0 ? height : 1;

		m_Post.Init(m_Width, m_Height);
		m_Environment.Init();
		m_Shadow.Init(shadowMapSize);

		m_Initialized = true;
	}

	void SceneRenderer::Shutdown()
	{
		if (!m_Initialized)
			return;

		m_Post.Shutdown();
		m_Environment.Shutdown();
		Renderer3D::ClearIBL();
		m_Shadow.Shutdown();
		Renderer3D::ClearShadow();

		m_Initialized = false;
	}

	void SceneRenderer::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0)
			return;
		m_Width  = width;
		m_Height = height;
		if (m_Initialized)
			m_Post.SetViewportSize(width, height);
	}

	// =========================================================================
	// SceneRenderer — the frame
	// =========================================================================

	void SceneRenderer::Render(const SceneRenderDesc& desc)
	{
		if (!m_Initialized)
			return;
		if (m_InRender)
		{
			CS_CORE_ERROR("SceneRenderer::Render re-entered (called inside a draw callback) — ignored.");
			return;
		}
		m_InRender = true;

		// GPU profiler (F3): mark the frame boundary — this closes last frame's
		// zones and resolves the oldest ready frame. Each pass below is wrapped in
		// a named GPU zone (why Render() is decomposed into one method per pass).
		RenderCommand::GpuFrameMark();

		// 1) Capture the final target FIRST, before any pass rebinds a framebuffer,
		//    and precompute the main camera matrices reused across passes.
		m_FinalFbo    = RenderCommand::GetBoundFramebuffer();
		m_ViewProj    = desc.Projection * desc.View;
		m_InvViewProj = glm::inverse(m_ViewProj);

		// 2) Lights: upload once up front so every pass (reflection terrain, opaque,
		//    the transparent tail) reads the same lights UBO.
		Renderer3D::SetLightDirection(desc.Lights.SunDirection);
		Renderer3D::SetAmbient(desc.Lights.Ambient);
		Renderer3D::SetLights(desc.Lights);

		// 3) Environment: bake when the skybox OR IBL needs the cube (dirty-flag
		//    no-op; leaves the default FBO bound — safe HERE, never mid-pass). The
		//    IBL set is pushed to Renderer3D only when IBL is on.
		if ((desc.Settings.IBL || desc.Settings.Skybox) && m_Environment.IsInitialized())
			m_Environment.Bake();
		if (desc.Settings.IBL)
			m_Environment.PushToRenderer();
		else
			Renderer3D::ClearIBL();

		// Each pass runs inside a GPU timer zone (F3). Zone names are the profiler
		// HUD's rows; they respond live to the Settings toggles (a disabled feature
		// shrinks or zeroes its zone).
		RenderCommand::BeginGpuZone("Shadow");         PassShadow(desc);           RenderCommand::EndGpuZone();  // 4
		RenderCommand::BeginGpuZone("Coverage");       PassCoverage(desc);         RenderCommand::EndGpuZone();  // 4b (F8)
		RenderCommand::BeginGpuZone("Reflection");     PassReflection(desc);       RenderCommand::EndGpuZone();  // 5
		RenderCommand::BeginGpuZone("Opaque");         PassOpaqueHDR(desc);        RenderCommand::EndGpuZone();  // 6
		RenderCommand::BeginGpuZone("Transparents");   PassTransparents(desc);     RenderCommand::EndGpuZone();  // 7
		RenderCommand::BeginGpuZone("Post+Composite"); PassPostAndComposite(desc); RenderCommand::EndGpuZone();  // 8

		m_InRender = false;          // 9
	}

	// 4) Shadow depth pass -----------------------------------------------------
	void SceneRenderer::PassShadow(const SceneRenderDesc& desc)
	{
		if (!desc.Settings.Shadows)
		{
			Renderer3D::ClearShadow();
			return;
		}

		m_Shadow.SetLight(desc.Lights.SunDirection, desc.Settings.ShadowCenter, desc.Settings.ShadowRadius);
		m_Shadow.BeginDepthPass();

		if (desc.DrawOpaque)
		{
			SceneDrawContext ctx;
			ctx.Pass           = ScenePass::ShadowDepth;
			ctx.ViewProjection = m_Shadow.GetLightViewProj();
			ctx.EyePosition    = desc.CameraPosition;   // no eye for a directional depth pass
			ctx.CameraPosition = desc.CameraPosition;
			ctx.m_Shadow       = &m_Shadow;
			desc.DrawOpaque(ctx);
		}

		// ECS meshes cast too (respect CastShadows; skip null mesh).
		if (desc.EcsScene)
		{
			auto view = desc.EcsScene->View<TransformComponent, MeshRendererComponent>();
			for (auto entity : view)
			{
				Entity e{ entity, desc.EcsScene };
				const auto& mr = e.GetComponent<MeshRendererComponent>();
				if (mr.MeshAsset && mr.CastShadows)
					m_Shadow.DrawCaster(mr.MeshAsset, e.GetComponent<TransformComponent>().GetTransform());
			}
		}

		// Terrain casts too (F4): walks the same LOD cut as the lit pass using the
		// REAL camera position so caster and receiver tessellation agree. Uses the
		// shadow pass's render state (front-cull + viewport already set).
		if (desc.TerrainSystem && desc.Settings.TerrainCastsShadows)
			desc.TerrainSystem->RenderDepth(m_Shadow.GetLightViewProj(), desc.CameraPosition);

		m_Shadow.EndDepthPass();
		m_Shadow.PushToRenderer(desc.Settings.ShadowBias);
	}

	// 4b) Top-down snow coverage capture (F8) ----------------------------------
	void SceneRenderer::PassCoverage(const SceneRenderDesc& desc)
	{
		if (!desc.Coverage || !desc.Coverage->IsInitialized())
			return;

		CoverageCapture& cov = *desc.Coverage;
		cov.BeginDepthCapture();   // binds the depth FBO + ortho viewport; restores at End

		// App occluders — routed to the coverage's depth draw via TopDownDepth.
		if (desc.DrawOpaque)
		{
			SceneDrawContext ctx;
			ctx.Pass           = ScenePass::TopDownDepth;
			ctx.ViewProjection = cov.GetCaptureViewProj();
			ctx.EyePosition    = desc.CameraPosition;
			ctx.CameraPosition = desc.CameraPosition;
			ctx.m_Coverage     = &cov;
			desc.DrawOpaque(ctx);
		}

		// ECS meshes occlude too (CastShadows doubles as the "casts coverage" gate).
		if (desc.EcsScene)
		{
			auto view = desc.EcsScene->View<TransformComponent, MeshRendererComponent>();
			for (auto entity : view)
			{
				Entity e{ entity, desc.EcsScene };
				const auto& mr = e.GetComponent<MeshRendererComponent>();
				if (mr.MeshAsset && mr.CastShadows)
					cov.DrawCaster(mr.MeshAsset, e.GetComponent<TransformComponent>().GetTransform());
			}
		}

		// Terrain top surface (same depth path, the coverage's top-down ortho matrix).
		if (desc.TerrainSystem)
			desc.TerrainSystem->RenderDepth(cov.GetCaptureViewProj(), desc.CameraPosition);

		cov.EndDepthCapture();
		cov.UpdateCoverage(desc.DeltaTime, desc.CoverageAccumPerSec, desc.CoverageMeltPerSec);
	}

	// 5) Planar reflection pass (primary water only) ---------------------------
	void SceneRenderer::PassReflection(const SceneRenderDesc& desc)
	{
		if (!desc.Settings.WaterReflections || desc.WaterBodies.empty())
			return;

		const int idx = desc.PrimaryReflectionWater;
		if (idx < 0 || idx >= static_cast<int>(desc.WaterBodies.size()))
			return;   // -1 (or out of range) = IBL-only for all waters

		Water* water = desc.WaterBodies[idx];
		if (!water)
			return;

		glm::mat4 reflVP{ 1.0f };
		glm::vec3 reflCam{ 0.0f };
		if (!water->BeginReflection(desc.View, desc.Projection, desc.CameraPosition, reflVP, reflCam))
			return;

		Renderer3D::BeginScene(reflVP, reflCam);

		if (desc.Settings.Skybox)
		{
			if (desc.DetailedSky)
				m_Environment.DrawSkyboxDetailed(reflVP, *desc.DetailedSky);
			else
				m_Environment.DrawSkybox(reflVP);
		}

		// LOD selection uses the REAL camera position so the reflected terrain
		// tessellation matches the main view exactly (no seam under the surface).
		if (desc.TerrainSystem)
			desc.TerrainSystem->Render(desc.CameraPosition);

		if (desc.DrawOpaque)
		{
			SceneDrawContext ctx;
			ctx.Pass           = ScenePass::Reflection;
			ctx.ViewProjection = reflVP;
			ctx.EyePosition    = reflCam;
			ctx.CameraPosition = desc.CameraPosition;
			desc.DrawOpaque(ctx);
		}

		Renderer3D::EndScene();
		water->EndReflection();
		// The following opaque pass (BeginHDR) re-asserts the viewport.
	}

	// 6) Opaque HDR pass -------------------------------------------------------
	void SceneRenderer::PassOpaqueHDR(const SceneRenderDesc& desc)
	{
		m_Post.SetViewportSize(m_Width, m_Height);
		m_Post.BeginHDR(desc.Settings.ClearColor);

		Renderer3D::BeginScene(m_ViewProj, desc.CameraPosition);

		if (desc.Settings.Skybox)
		{
			if (desc.DetailedSky)
				m_Environment.DrawSkyboxDetailed(m_ViewProj, *desc.DetailedSky);
			else
				m_Environment.DrawSkybox(m_ViewProj);
		}

		if (desc.TerrainSystem)
			desc.TerrainSystem->Render(desc.CameraPosition);

		if (desc.DrawOpaque)
		{
			SceneDrawContext ctx;
			ctx.Pass           = ScenePass::Main;
			ctx.ViewProjection = m_ViewProj;
			ctx.EyePosition    = desc.CameraPosition;
			ctx.CameraPosition = desc.CameraPosition;
			desc.DrawOpaque(ctx);
		}

		Renderer3D::EndScene();

		// ECS scene (Main only). OnRender3D owns its own BeginScene/EndScene and
		// re-uploads the lights UBO from ECS light components, so re-assert the
		// app's lights afterwards for the transparent tail (fixed here by design).
		if (desc.EcsScene)
		{
			MatrixCamera cam(desc.View, desc.Projection, m_ViewProj, desc.CameraPosition);
			desc.EcsScene->OnRender3D(cam);
			Renderer3D::SetLights(desc.Lights);
		}
	}

	// 7) Transparents (HDR still bound) ---------------------------------------
	void SceneRenderer::PassTransparents(const SceneRenderDesc& desc)
	{
		const Ref<FrameBuffer>& sceneFbo = m_Post.GetSceneTarget();
		if (!sceneFbo)
			return;

		const uint32_t colorID = sceneFbo->GetColorAttachmentRendererID(0);
		const uint32_t depthID = sceneFbo->GetDepthAttachmentRendererID();

		// Water far -> near: each does its own refraction grab + FBO/viewport re-assert.
		for (Water* w : desc.WaterBodies)
			if (w)
				w->Render(desc.CameraPosition, desc.TimeSeconds, m_ViewProj,
				          colorID, depthID, m_Width, m_Height);

		for (ParticleEmitter* e : desc.Emitters)
			if (e)
				e->Render(desc.View, depthID, m_InvViewProj);

		for (RibbonEmitter* r : desc.Ribbons)
			if (r)
				r->Render(desc.View, desc.TimeSeconds);

		// App transparent geometry — wrapped in a scene so ctx.DrawMesh routes to
		// a live Renderer3D (the camera UBO is restored to the main camera here).
		if (desc.DrawTransparent)
		{
			Renderer3D::BeginScene(m_ViewProj, desc.CameraPosition);
			SceneDrawContext ctx;
			ctx.Pass           = ScenePass::Main;
			ctx.ViewProjection = m_ViewProj;
			ctx.EyePosition    = desc.CameraPosition;
			ctx.CameraPosition = desc.CameraPosition;
			desc.DrawTransparent(ctx);
			Renderer3D::EndScene();
		}
	}

	// 8) Post + composite ------------------------------------------------------
	void SceneRenderer::PassPostAndComposite(const SceneRenderDesc& desc)
	{
		const SceneRendererSettings& s = desc.Settings;

		m_Post.SetSSAOEnabled(s.SSAO);
		m_Post.SetSSAOParams(s.SsaoRadius, s.SsaoBias);
		m_Post.SetBloomEnabled(s.Bloom);
		m_Post.SetBloomParams(s.BloomThreshold, s.BloomKnee, s.BloomIntensity);
		m_Post.SetFXAAEnabled(s.FXAA);
		m_Post.SetFogEnabled(s.Fog);
		m_Post.SetFogParams(s.FogColor, s.FogDensity, s.FogHeightFalloff, s.FogBaseHeight);
		// Underwater medium (F6 + Layer 2): the tonemap fogs + tints when the camera is
		// below the waterline (checked shader-side against UnderwaterY), depth-graded
		// toward the deep color + denser with descent, with animated seafloor caustics.
		m_Post.SetUnderwater(s.Underwater, s.UnderwaterY, s.UnderwaterColor,
		                     s.UnderwaterDensity, s.UnderwaterTint);
		m_Post.SetUnderwaterGrading(s.UnderwaterDeepColor, s.UnderwaterDepthReference);
		m_Post.SetUnderwaterCaustics(s.UnderwaterCausticStrength, s.UnderwaterCausticScale);
		m_Post.SetTime(desc.TimeSeconds);
		// Camera for depth reconstruction (fog + god rays + lens flare) — needed by
		// both RenderEffects and Composite; set once, it persists across both.
		m_Post.SetCamera(m_ViewProj, desc.CameraPosition);

		// Lens flare (F7): additive screen-space flare in Composite's LDR stage. The
		// tint is the sun color; the sun screen position comes from the camera above
		// and the sun travel direction (the sun sits opposite it).
		m_Post.SetLensFlare(s.LensFlare, s.LensFlareIntensity, desc.Lights.SunColor);
		m_Post.SetLensFlareSun(desc.Lights.SunDirection);

		const bool godRays = s.GodRays && s.Shadows;   // shafts raymarch the shadow map
		m_Post.SetGodRaysEnabled(godRays);
		m_Post.SetGodRaysParams(s.GodRaysIntensity, s.GodRaysDensity);
		if (godRays)
			m_Post.SetSunShaftInputs(m_Shadow.GetDepthID(), m_Shadow.GetLightViewProj(),
			                         desc.Lights.SunDirection, desc.Lights.SunColor, desc.Lights.SunIntensity);

		// Heat-haze distortion field (S10.5): distortion emitters write it; the
		// tonemap displaces the scene fetch by it.
		m_Post.SetHeatHazeEnabled(s.HeatHaze);
		m_Post.SetHeatHazeStrength(s.HeatHazeStrength);
		if (s.HeatHaze && !desc.DistortionEmitters.empty() && m_Post.BeginDistortion())
		{
			const uint32_t depthID = m_Post.GetSceneTarget()->GetDepthAttachmentRendererID();
			for (ParticleEmitter* e : desc.DistortionEmitters)
				if (e)
					e->RenderDistortion(desc.View, depthID, m_InvViewProj);
			m_Post.EndDistortion();
		}

		m_Post.RenderEffects(desc.Projection);

		// Resolve into the final LDR target the caller had bound.
		RenderCommand::BindFramebufferHandle(m_FinalFbo);
		RenderCommand::SetViewport(0, 0, m_Width, m_Height);
		m_Post.Composite(desc.Exposure);

		if (desc.DrawOverlay2D)
			desc.DrawOverlay2D();
	}
}
