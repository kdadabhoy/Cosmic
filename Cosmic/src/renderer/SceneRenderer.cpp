// SceneRenderer.cpp — F2 engine-owned frame orchestration. See SceneRenderer.h.

#include "renderer/SceneRenderer.h"

#include "renderer/RenderCommand.h"
#include "renderer/BindingPoints.h"
#include "graphics/FrameBuffer.h"
#include "graphics/Mesh.h"
#include "graphics/Material.h"
#include "graphics/Shader.h"
#include "camera/Camera.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "utils/FileSystem.h"   // resolve project:// HdriPath (H4)
#include "core/Log.h"

#include <glm/glm.hpp>

namespace Cosmic
{

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
		// Members' own dtors release their GPU resources; Shutdown() is idempotent,
		// so call it if still live.
		if (m_Initialized)
			Shutdown();
	}

	void SceneRenderer::Init(uint32_t width, uint32_t height)
	{
		if (m_Initialized)
			return;

		m_Width  = width  > 0 ? width  : 1;
		m_Height = height > 0 ? height : 1;

		m_Post.Init(m_Width, m_Height);

		m_Initialized = true;
	}

	void SceneRenderer::Shutdown()
	{
		if (!m_Initialized)
			return;

		m_Post.Shutdown();

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

	// E4 — map a scene EnvironmentComponent into a per-frame desc. Every field
	// mirrors a SceneRendererSettings/SceneRenderDesc default, so applying a
	// default-constructed component is a no-op relative to the engine defaults.
	void SceneRenderer::ApplyEnvironment(const EnvironmentComponent& env, SceneRenderDesc& desc)
	{
		desc.Exposure = env.Exposure;

		auto& s = desc.Settings;
		s.Skybox           = env.Skybox;
		s.IBL              = env.IBL;
		s.Fog              = env.Fog;
		s.FogColor         = env.FogColor;
		s.FogDensity       = env.FogDensity;
		s.FogHeightFalloff = env.FogHeightFalloff;
		s.FogBaseHeight    = env.FogBaseHeight;
		s.Bloom            = env.Bloom;
		s.BloomThreshold   = env.BloomThreshold;
		s.BloomIntensity   = env.BloomIntensity;
		s.SSAO             = env.SSAO;
		s.SsaoRadius       = env.SsaoRadius;
		s.FXAA             = env.FXAA;
		s.LensFlare        = env.LensFlare;
		s.LensFlareIntensity = env.LensFlareIntensity;
		s.Vignette         = env.Vignette;             // Q5
		s.VignetteAmount   = env.VignetteAmount;
		s.VignetteRadius   = env.VignetteRadius;
		s.VignetteFeather  = env.VignetteFeather;
		s.VignetteColor    = env.VignetteColor;
		s.AmbientIntensity = env.AmbientIntensity;     // X2 (default 1.0 = identical)
		s.Gamma            = env.Gamma;                // X2 (default 2.2 = identical)

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

		// Each pass runs inside a GPU timer zone (F3). Zone names are the profiler
		// HUD's rows; they respond live to the Settings toggles (a disabled feature
		// shrinks or zeroes its zone). Steps 4–5 have no 2D counterpart — 2D content
		// casts no shadows, accumulates no coverage and reflects nothing — so the 2D
		// frame is Opaque (an HDR clear) -> Transparents (sprites) -> Post.
		RenderCommand::BeginGpuZone("Opaque");         PassOpaqueHDR(desc);        RenderCommand::EndGpuZone();  // 6
		RenderCommand::BeginGpuZone("Transparents");   PassTransparents(desc);     RenderCommand::EndGpuZone();  // 7
		RenderCommand::BeginGpuZone("Post+Composite"); PassPostAndComposite(desc); RenderCommand::EndGpuZone();  // 8

		m_InRender = false;          // 9
	}

	// X7 — offscreen render-to-texture verb ------------------------------------
	void SceneRenderer::RenderToTexture(const SceneRenderDesc& desc, const Ref<FrameBuffer>& target)
	{
		if (!m_Initialized || !target)
			return;                                  // headless / null ⇒ safe no-op
		const uint32_t w = target->GetWidth();
		const uint32_t h = target->GetHeight();
		if (w == 0 || h == 0)
			return;

		// Remember the caller's target so the main viewport is restored on exit
		// (the A4 contract). Render() itself captures whatever is bound as its
		// final FBO, so we bind `target` first.
		const uint32_t prevFbo = RenderCommand::GetBoundFramebuffer();

		if (m_Width != w || m_Height != h)
			SetViewportSize(w, h);                   // size this renderer's post stack to the target

		target->Bind();
		RenderCommand::SetViewport(0, 0, w, h);
		Render(desc);                                // env/sky/shadows/post all apply, composites into target

		RenderCommand::BindFramebufferHandle(prevFbo);
	}

	// 6) Opaque HDR pass -------------------------------------------------------
	void SceneRenderer::PassOpaqueHDR(const SceneRenderDesc& desc)
	{
		m_Post.SetViewportSize(m_Width, m_Height);
		m_Post.BeginHDR(desc.Settings.ClearColor);

		// Wireframe view (R8): geometry rasterizes as lines; the skybox is skipped
		// (see SceneRendererSettings). Fill is restored before this pass returns —
		// the post/composite fullscreen triangles must never rasterize as lines.
		if (desc.Settings.Wireframe)
			RenderCommand::SetPolygonMode(RendererAPI::PolygonMode::Line);

		// W6 — everything from here to the wireframe restore is 3D. A 2D frame's
		// opaque pass is exactly the HDR bind + clear above: sprites are transparent
		// geometry and draw in the next pass.

		if (desc.Settings.Wireframe)
			RenderCommand::SetPolygonMode(RendererAPI::PolygonMode::Fill);
	}

	// 7) Transparents (HDR still bound) ---------------------------------------
	void SceneRenderer::PassTransparents(const SceneRenderDesc& desc)
	{
		const Ref<FrameBuffer>& sceneFbo = m_Post.GetSceneTarget();
		if (!sceneFbo)
			return;

		// Wireframe view (R8): water/particle/app-transparent triangles rasterize
		// as lines too; Fill is restored before the pass returns (refraction grabs
		// are blits — polygon mode does not affect them).
		if (desc.Settings.Wireframe)
			RenderCommand::SetPolygonMode(RendererAPI::PolygonMode::Line);

		// THIS is the 2D sprite path: PlayerLayer, Starforge and the scene2d golden
		// all draw OnRenderSprites + OnRender2DLights from this hook with the HDR
		// target still bound (Renderer2D opens its own scene inside the callback).
		if (desc.DrawTransparent)
		{
			SceneDrawContext ctx;
			ctx.Pass           = ScenePass::Main;
			ctx.ViewProjection = m_ViewProj;
			ctx.EyePosition    = desc.CameraPosition;
			ctx.CameraPosition = desc.CameraPosition;
			desc.DrawTransparent(ctx);
		}

		if (desc.Settings.Wireframe)
			RenderCommand::SetPolygonMode(RendererAPI::PolygonMode::Fill);
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
		m_Post.SetVignetteEnabled(s.Vignette);   // Q5
		m_Post.SetVignetteParams(s.VignetteAmount, s.VignetteRadius, s.VignetteFeather, s.VignetteColor);
		m_Post.SetGamma(s.Gamma);                // X2 (default 2.2 = byte-identical)
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
		// Camera for depth reconstruction (fog + lens flare) — needed by both
		// RenderEffects and Composite; set once, it persists across both.
		m_Post.SetCamera(m_ViewProj, desc.CameraPosition);

		// Lens flare (F7): additive screen-space flare in Composite's LDR stage. A
		// SUN effect with no 2D meaning, so its setters are never called here —
		// byte-identical to calling them disabled (PostProcessStack's
		// m_LensFlareEnabled defaults to off). The sun-shaft pass that used to sit
		// beside it went with the 3D renderer (AP-05).

		// Heat-haze distortion field (S10.5): distortion emitters write it; the
		// tonemap displaces the scene fetch by it. The toggles stay (the tonemap
		// reads them); with no emitters the field is never written.
		m_Post.SetHeatHazeEnabled(s.HeatHaze);
		m_Post.SetHeatHazeStrength(s.HeatHazeStrength);

		m_Post.RenderEffects(desc.Projection);

		// Resolve into the final LDR target the caller had bound.
		RenderCommand::BindFramebufferHandle(m_FinalFbo);
		RenderCommand::SetViewport(0, 0, m_Width, m_Height);
		m_Post.Composite(desc.Exposure);

		if (desc.DrawOverlay2D)
			desc.DrawOverlay2D();
	}

}
