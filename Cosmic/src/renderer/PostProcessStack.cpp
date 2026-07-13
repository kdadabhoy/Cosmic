// PostProcessStack.cpp — S6 HDR + SSAO + bloom + FXAA. See PostProcessStack.h.

#include "renderer/PostProcessStack.h"
#include "renderer/RenderCommand.h"
#include "graphics/FrameBuffer.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "core/Log.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstdio>
#include <random>

namespace Cosmic
{
	namespace
	{
		Ref<FrameBuffer> MakeColorFbo(uint32_t w, uint32_t h, FramebufferTextureFormat fmt)
		{
			FramebufferSpecification spec;
			spec.Width  = std::max(1u, w);
			spec.Height = std::max(1u, h);
			spec.Attachments = { fmt };
			return FrameBuffer::Create(spec);
		}
	}

	PostProcessStack::~PostProcessStack() = default;

	void PostProcessStack::Init(uint32_t width, uint32_t height)
	{
		if (m_Initialized)
			return;

		m_Width  = width  > 0 ? width  : 1;
		m_Height = height > 0 ? height : 1;

		// HDR scene target: float color (overbright survives) + depth.
		FramebufferSpecification spec;
		spec.Width  = m_Width;
		spec.Height = m_Height;
		spec.Attachments = { FramebufferTextureFormat::RGBA16F,
		                     FramebufferTextureFormat::DEPTH24STENCIL8 };
		m_SceneHDR = FrameBuffer::Create(spec);

		m_TonemapShader = Shader::Create("assets/shaders/Tonemap.glsl");
		if (!m_TonemapShader)
			CS_CORE_ERROR("PostProcessStack: failed to load Tonemap shader — HDR resolve disabled.");

		InitEffects();

		m_Initialized = true;
	}

	void PostProcessStack::InitEffects()
	{
		// --- Shaders ---
		m_SsaoShader           = Shader::Create("assets/shaders/Ssao.glsl");
		m_SsaoBlurShader       = Shader::Create("assets/shaders/SsaoBlur.glsl");
		m_BloomPrefilterShader = Shader::Create("assets/shaders/BloomPrefilter.glsl");
		m_BloomBlurShader      = Shader::Create("assets/shaders/BloomBlur.glsl");
		m_FxaaShader           = Shader::Create("assets/shaders/Fxaa.glsl");
		m_GodRaysShader        = Shader::Create("assets/shaders/GodRays.glsl");
		m_LensFlareShader      = Shader::Create("assets/shaders/LensFlare.glsl");
		if (!m_SsaoShader || !m_SsaoBlurShader || !m_BloomPrefilterShader || !m_BloomBlurShader ||
		    !m_FxaaShader || !m_GodRaysShader || !m_LensFlareShader)
			CS_CORE_ERROR("PostProcessStack: one or more effect shaders failed to load.");

		// --- Targets ---
		ResizeEffects();

		// --- SSAO hemisphere kernel + 4x4 rotation noise ---
		std::mt19937 rng(1337u);
		std::uniform_real_distribution<float> u01(0.0f, 1.0f);
		std::uniform_real_distribution<float> u11(-1.0f, 1.0f);

		m_Kernel.clear();
		m_KernelNames.clear();
		for (int i = 0; i < 32; ++i)
		{
			glm::vec3 s(u11(rng), u11(rng), u01(rng));   // tangent-space hemisphere (+z)
			s = glm::normalize(s) * u01(rng);
			float t = static_cast<float>(i) / 32.0f;
			s *= glm::mix(0.1f, 1.0f, t * t);            // cluster samples near the origin
			m_Kernel.push_back(s);

			char name[32];
			std::snprintf(name, sizeof(name), "u_Kernel[%d]", i);
			m_KernelNames.emplace_back(name);
		}

		uint8_t noise[16 * 4];
		for (int i = 0; i < 16; ++i)
		{
			noise[i * 4 + 0] = static_cast<uint8_t>((u11(rng) * 0.5f + 0.5f) * 255.0f);
			noise[i * 4 + 1] = static_cast<uint8_t>((u11(rng) * 0.5f + 0.5f) * 255.0f);
			noise[i * 4 + 2] = 128;   // z ≈ 0 after decode
			noise[i * 4 + 3] = 255;
		}
		m_NoiseTex = Texture2D::Create(4, 4);
		if (m_NoiseTex)
		{
			m_NoiseTex->SetData(noise, sizeof(noise));
			m_NoiseTex->SetSampling(TextureFilter::Nearest, TextureWrap::Repeat);
		}
	}

	void PostProcessStack::ResizeEffects()
	{
		const uint32_t hw = std::max(1u, m_Width / 2);
		const uint32_t hh = std::max(1u, m_Height / 2);

		// Resize in place after the first allocation — SetViewportSize runs on
		// every viewport drag, and re-creating five FBOs per frame is exactly the
		// kind of GPU-object churn a resize storm turns into a hitch.
		auto ensure = [](Ref<FrameBuffer>& fbo, uint32_t w, uint32_t h, FramebufferTextureFormat fmt)
		{
			if (fbo)
				fbo->Resize(w, h);
			else
				fbo = MakeColorFbo(w, h, fmt);
		};
		ensure(m_SsaoTarget,     hw, hh,            FramebufferTextureFormat::RGBA16F);
		ensure(m_SsaoBlurTarget, hw, hh,            FramebufferTextureFormat::RGBA16F);
		ensure(m_BloomA,         hw, hh,            FramebufferTextureFormat::RGBA16F);
		ensure(m_BloomB,         hw, hh,            FramebufferTextureFormat::RGBA16F);
		ensure(m_ShaftTarget,    hw, hh,            FramebufferTextureFormat::RGBA16F);
		ensure(m_DistortTarget,  hw, hh,            FramebufferTextureFormat::RGBA16F);
		ensure(m_LdrTarget,      m_Width, m_Height, FramebufferTextureFormat::RGBA8);
	}

	void PostProcessStack::Shutdown()
	{
		m_SceneHDR.reset();
		m_TonemapShader.reset();
		m_SsaoShader.reset();
		m_SsaoBlurShader.reset();
		m_SsaoTarget.reset();
		m_SsaoBlurTarget.reset();
		m_NoiseTex.reset();
		m_Kernel.clear();
		m_KernelNames.clear();
		m_BloomPrefilterShader.reset();
		m_BloomBlurShader.reset();
		m_BloomA.reset();
		m_BloomB.reset();
		m_FxaaShader.reset();
		m_LdrTarget.reset();
		m_GodRaysShader.reset();
		m_ShaftTarget.reset();
		m_DistortTarget.reset();
		m_LensFlareShader.reset();
		m_Initialized = false;
		m_Width = m_Height = 0;
	}

	void PostProcessStack::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (!m_Initialized || width == 0 || height == 0)
			return;
		if (width == m_Width && height == m_Height)
			return;

		m_Width  = width;
		m_Height = height;
		if (m_SceneHDR)
			m_SceneHDR->Resize(width, height);
		ResizeEffects();
	}

	void PostProcessStack::BeginHDR(const glm::vec4& clearColor)
	{
		if (!m_SceneHDR)
			return;

		m_SceneHDR->Bind();
		RenderCommand::SetViewport(0, 0, m_Width, m_Height);
		RenderCommand::SetClearColor(clearColor);
		RenderCommand::Clear();
	}

	void PostProcessStack::RenderEffects(const glm::mat4& projection)
	{
		m_AoResultID    = 0;
		m_BloomResultID = 0;
		m_ShaftResultID = 0;
		if (!m_Initialized)
			return;

		if (m_SsaoEnabled)
			RenderSSAO(projection);
		if (m_BloomEnabled)
			RenderBloom();
		if (m_GodRaysEnabled && m_ShaftShadowMapID != 0)
			RenderGodRays();
	}

	void PostProcessStack::RenderSSAO(const glm::mat4& projection)
	{
		if (!m_SsaoShader || !m_SsaoBlurShader || !m_SsaoTarget || !m_SsaoBlurTarget ||
		    !m_SceneHDR || !m_NoiseTex)
			return;

		const uint32_t aw = m_SsaoTarget->GetWidth();
		const uint32_t ah = m_SsaoTarget->GetHeight();
		const glm::mat4 invProj = glm::inverse(projection);

		RenderCommand::SetDepthTest(false);
		RenderCommand::SetDepthWrite(false);

		// --- SSAO pass ---
		m_SsaoTarget->Bind();
		RenderCommand::SetViewport(0, 0, aw, ah);
		m_SsaoShader->Bind();
		RenderCommand::BindTextureSlot(0, m_SceneHDR->GetDepthAttachmentRendererID());
		m_SsaoShader->SetInt("u_Depth", 0);
		RenderCommand::BindTextureSlot(1, m_NoiseTex->GetRendererID());
		m_SsaoShader->SetInt("u_Noise", 1);
		m_SsaoShader->SetMat4("u_Projection", projection);
		m_SsaoShader->SetMat4("u_InvProjection", invProj);
		m_SsaoShader->SetFloat2("u_NoiseScale", { aw / 4.0f, ah / 4.0f });
		m_SsaoShader->SetFloat("u_Radius", m_SsaoRadius);
		m_SsaoShader->SetFloat("u_Bias", m_SsaoBias);
		m_SsaoShader->SetInt("u_KernelSize", static_cast<int>(m_Kernel.size()));
		for (size_t i = 0; i < m_Kernel.size(); ++i)
			m_SsaoShader->SetFloat3(m_KernelNames[i], m_Kernel[i]);
		DrawFullscreenTriangle();

		// --- Blur pass ---
		m_SsaoBlurTarget->Bind();
		RenderCommand::SetViewport(0, 0, aw, ah);
		m_SsaoBlurShader->Bind();
		RenderCommand::BindTextureSlot(0, m_SsaoTarget->GetColorAttachmentRendererID(0));
		m_SsaoBlurShader->SetInt("u_Ssao", 0);
		m_SsaoBlurShader->SetFloat2("u_TexelSize", { 1.0f / aw, 1.0f / ah });
		DrawFullscreenTriangle();

		RenderCommand::SetDepthTest(true);
		RenderCommand::SetDepthWrite(true);

		m_AoResultID = m_SsaoBlurTarget->GetColorAttachmentRendererID(0);
	}

	void PostProcessStack::RenderBloom()
	{
		if (!m_BloomPrefilterShader || !m_BloomBlurShader || !m_BloomA || !m_BloomB || !m_SceneHDR)
			return;

		const uint32_t bw = m_BloomA->GetWidth();
		const uint32_t bh = m_BloomA->GetHeight();

		RenderCommand::SetDepthTest(false);
		RenderCommand::SetDepthWrite(false);

		// --- Threshold the scene into ping (A) ---
		m_BloomA->Bind();
		RenderCommand::SetViewport(0, 0, bw, bh);
		m_BloomPrefilterShader->Bind();
		RenderCommand::BindTextureSlot(0, m_SceneHDR->GetColorAttachmentRendererID(0));
		m_BloomPrefilterShader->SetInt("u_Scene", 0);
		m_BloomPrefilterShader->SetFloat("u_Threshold", m_BloomThreshold);
		m_BloomPrefilterShader->SetFloat("u_Knee", m_BloomKnee);
		DrawFullscreenTriangle();

		// --- Separable Gaussian ping-pong ---
		m_BloomBlurShader->Bind();
		m_BloomBlurShader->SetFloat2("u_TexelSize", { 1.0f / bw, 1.0f / bh });
		Ref<FrameBuffer> src = m_BloomA;
		Ref<FrameBuffer> dst = m_BloomB;
		bool horizontal = true;
		for (int i = 0; i < 10; ++i)
		{
			dst->Bind();
			RenderCommand::SetViewport(0, 0, bw, bh);
			RenderCommand::BindTextureSlot(0, src->GetColorAttachmentRendererID(0));
			m_BloomBlurShader->SetInt("u_Image", 0);
			m_BloomBlurShader->SetFloat("u_Horizontal", horizontal ? 1.0f : 0.0f);
			DrawFullscreenTriangle();
			std::swap(src, dst);
			horizontal = !horizontal;
		}

		RenderCommand::SetDepthTest(true);
		RenderCommand::SetDepthWrite(true);

		m_BloomResultID = src->GetColorAttachmentRendererID(0);   // last written
	}

	void PostProcessStack::RenderGodRays()
	{
		if (!m_GodRaysShader || !m_ShaftTarget || !m_SceneHDR)
			return;

		const uint32_t sw = m_ShaftTarget->GetWidth();
		const uint32_t sh = m_ShaftTarget->GetHeight();

		RenderCommand::SetDepthTest(false);
		RenderCommand::SetDepthWrite(false);

		m_ShaftTarget->Bind();
		RenderCommand::SetViewport(0, 0, sw, sh);
		m_GodRaysShader->Bind();
		RenderCommand::BindTextureSlot(0, m_SceneHDR->GetDepthAttachmentRendererID());
		m_GodRaysShader->SetInt("u_Depth", 0);
		RenderCommand::BindTextureSlot(1, m_ShaftShadowMapID);
		m_GodRaysShader->SetInt("u_ShadowMap", 1);
		m_GodRaysShader->SetMat4("u_InvViewProj", glm::inverse(m_ViewProjection));
		m_GodRaysShader->SetFloat3("u_CameraPos", m_CameraPos);
		m_GodRaysShader->SetMat4("u_LightViewProj", m_ShaftLightViewProj);
		m_GodRaysShader->SetFloat3("u_SunDir", m_ShaftSunDir);
		m_GodRaysShader->SetFloat3("u_SunColor", m_ShaftSunColor * m_ShaftSunIntensity);
		m_GodRaysShader->SetFloat("u_Intensity", m_GodRaysIntensity);
		m_GodRaysShader->SetFloat("u_Density", m_GodRaysDensity);
		DrawFullscreenTriangle();

		RenderCommand::SetDepthTest(true);
		RenderCommand::SetDepthWrite(true);

		m_ShaftResultID = m_ShaftTarget->GetColorAttachmentRendererID(0);
	}

	bool PostProcessStack::BeginDistortion()
	{
		m_DistortionWritten = false;
		if (!m_Initialized || !m_HeatHazeEnabled || !m_DistortTarget || m_InDistortion)
			return false;

		m_DistortPrevFbo = RenderCommand::GetBoundFramebuffer();
		m_DistortTarget->Bind();
		RenderCommand::SetViewport(0, 0, m_DistortTarget->GetWidth(), m_DistortTarget->GetHeight());
		RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 0.0f });   // zero offset field
		RenderCommand::Clear();
		m_InDistortion = true;
		return true;
	}

	void PostProcessStack::EndDistortion()
	{
		if (!m_InDistortion)
			return;
		RenderCommand::BindFramebufferHandle(m_DistortPrevFbo);
		m_InDistortion      = false;
		m_DistortionWritten = true;
	}

	void PostProcessStack::Composite(float exposure)
	{
		if (!m_SceneHDR || !m_TonemapShader)
			return;

		const uint32_t finalTarget = RenderCommand::GetBoundFramebuffer();
		const bool fxaa = m_FxaaEnabled && m_FxaaShader && m_LdrTarget;

		RenderCommand::SetDepthTest(false);
		RenderCommand::SetDepthWrite(false);

		// Tonemap into the LDR intermediate (FXAA on) or straight to the final target.
		if (fxaa)
		{
			m_LdrTarget->Bind();
			RenderCommand::SetViewport(0, 0, m_Width, m_Height);
		}

		m_TonemapShader->Bind();
		RenderCommand::BindTextureSlot(0, m_SceneHDR->GetColorAttachmentRendererID(0));
		m_TonemapShader->SetInt("u_Scene", 0);
		m_TonemapShader->SetFloat("u_Exposure", exposure);

		// Vignette (Q5): amount 0 when disabled ⇒ the shader skips the block, so
		// the shipped output stays byte-identical.
		if (m_VignetteEnabled)
		{
			m_TonemapShader->SetFloat("u_VignetteAmount", m_VignetteAmount);
			m_TonemapShader->SetFloat("u_VignetteRadius", m_VignetteRadius);
			m_TonemapShader->SetFloat("u_VignetteFeather", m_VignetteFeather);
			m_TonemapShader->SetFloat3("u_VignetteColor", m_VignetteColor);
		}
		else
			m_TonemapShader->SetFloat("u_VignetteAmount", 0.0f);

		// Height fog (S7.2): reconstructs world position from the scene depth.
		if (m_FogEnabled)
		{
			RenderCommand::BindTextureSlot(3, m_SceneHDR->GetDepthAttachmentRendererID());
			m_TonemapShader->SetInt("u_Depth", 3);
			m_TonemapShader->SetFloat("u_UseFog", 1.0f);
			m_TonemapShader->SetFloat3("u_FogColor", m_FogColor);
			m_TonemapShader->SetFloat("u_FogDensity", m_FogDensity);
			m_TonemapShader->SetFloat("u_FogHeightFalloff", m_FogHeightFalloff);
			m_TonemapShader->SetFloat("u_FogBaseHeight", m_FogBaseHeight);
			m_TonemapShader->SetMat4("u_InvViewProj", glm::inverse(m_ViewProjection));
			m_TonemapShader->SetFloat3("u_CameraPos", m_CameraPos);
		}
		else
			m_TonemapShader->SetFloat("u_UseFog", 0.0f);

		// Underwater medium (F6): shares fog's depth reconstruction inputs (scene
		// depth on slot 3, inverse-VP, camera). Rebinding them is idempotent when
		// fog is also on; when fog is off this block supplies them.
		if (m_UnderwaterEnabled)
		{
			RenderCommand::BindTextureSlot(3, m_SceneHDR->GetDepthAttachmentRendererID());
			m_TonemapShader->SetInt("u_Depth", 3);
			m_TonemapShader->SetMat4("u_InvViewProj", glm::inverse(m_ViewProjection));
			m_TonemapShader->SetFloat3("u_CameraPos", m_CameraPos);
			m_TonemapShader->SetFloat("u_UseUnderwater", 1.0f);
			m_TonemapShader->SetFloat("u_WaterlineY", m_WaterlineY);
			m_TonemapShader->SetFloat3("u_UnderwaterColor", m_UnderwaterColor);
			m_TonemapShader->SetFloat("u_UnderwaterDensity", m_UnderwaterDensity);
			m_TonemapShader->SetFloat3("u_UnderwaterTint", m_UnderwaterTint);
			// Depth grading + seafloor caustics (Phase 11 Layer 2).
			m_TonemapShader->SetFloat3("u_DeepWaterColor", m_UnderwaterDeepColor);
			m_TonemapShader->SetFloat("u_UnderwaterDepthRef", m_UnderwaterDepthRef);
			m_TonemapShader->SetFloat("u_CausticStrength", m_UnderwaterCausticStrength);
			m_TonemapShader->SetFloat("u_CausticScale", m_UnderwaterCausticScale);
			m_TonemapShader->SetFloat("u_Time", m_Time);
		}
		else
			m_TonemapShader->SetFloat("u_UseUnderwater", 0.0f);

		if (m_SsaoEnabled && m_AoResultID)
		{
			RenderCommand::BindTextureSlot(1, m_AoResultID);
			m_TonemapShader->SetInt("u_AO", 1);
			m_TonemapShader->SetFloat("u_UseAO", 1.0f);
		}
		else
			m_TonemapShader->SetFloat("u_UseAO", 0.0f);

		if (m_BloomEnabled && m_BloomResultID)
		{
			RenderCommand::BindTextureSlot(2, m_BloomResultID);
			m_TonemapShader->SetInt("u_Bloom", 2);
			m_TonemapShader->SetFloat("u_UseBloom", 1.0f);
			m_TonemapShader->SetFloat("u_BloomIntensity", m_BloomIntensity);
		}
		else
			m_TonemapShader->SetFloat("u_UseBloom", 0.0f);

		// Sun shafts (S10.3): additive, like bloom.
		if (m_GodRaysEnabled && m_ShaftResultID)
		{
			RenderCommand::BindTextureSlot(4, m_ShaftResultID);
			m_TonemapShader->SetInt("u_Shafts", 4);
			m_TonemapShader->SetFloat("u_UseShafts", 1.0f);
		}
		else
			m_TonemapShader->SetFloat("u_UseShafts", 0.0f);

		// Heat-haze (S10.5): displace every scene-space fetch by the offset field.
		if (m_HeatHazeEnabled && m_DistortionWritten && m_DistortTarget)
		{
			RenderCommand::BindTextureSlot(5, m_DistortTarget->GetColorAttachmentRendererID(0));
			m_TonemapShader->SetInt("u_Distort", 5);
			m_TonemapShader->SetFloat("u_UseDistort", 1.0f);
			m_TonemapShader->SetFloat("u_DistortStrength", m_HeatHazeStrength);
		}
		else
			m_TonemapShader->SetFloat("u_UseDistort", 0.0f);

		DrawFullscreenTriangle();

		// Lens flare (F7): additive over the tonemapped image, AFTER tonemap and
		// BEFORE FXAA (so the flare is anti-aliased with the frame). The current
		// target is the tonemap output — the LDR intermediate when FXAA is on, else
		// the caller's final target — which is exactly where the flare belongs.
		if (m_LensFlareEnabled && m_LensFlareShader)
		{
			// The sun sits far away OPPOSITE its travel direction; project that world
			// point through the frame's view-projection (set via SetCamera) to a
			// screen-space UV. w > 0 == in front of the camera.
			const glm::vec3 sunWorld = m_CameraPos - m_LensFlareSunDir * 1.0e4f;
			const glm::vec4 clip     = m_ViewProjection * glm::vec4(sunWorld, 1.0f);
			const bool      inFront  = clip.w > 0.0f;
			glm::vec2 sunScreen(0.5f);
			if (inFront)
				sunScreen = glm::vec2(clip.x, clip.y) / clip.w * 0.5f + 0.5f;

			RenderCommand::SetBlendMode(RenderCommand::BlendMode::Additive);
			m_LensFlareShader->Bind();
			RenderCommand::BindTextureSlot(3, m_SceneHDR->GetDepthAttachmentRendererID());
			m_LensFlareShader->SetInt("u_Depth", 3);
			m_LensFlareShader->SetFloat2("u_SunScreenPos", sunScreen);
			m_LensFlareShader->SetFloat("u_SunInFront", inFront ? 1.0f : 0.0f);
			m_LensFlareShader->SetFloat("u_Intensity", m_LensFlareIntensity);
			m_LensFlareShader->SetFloat3("u_Tint", m_LensFlareTint);
			m_LensFlareShader->SetFloat("u_Aspect", m_Height > 0 ? (float)m_Width / (float)m_Height : 1.0f);
			DrawFullscreenTriangle();
			RenderCommand::SetBlendMode(RenderCommand::BlendMode::Alpha);   // restore engine default
		}

		// FXAA resolve: LDR intermediate → the caller's target.
		if (fxaa)
		{
			RenderCommand::BindFramebufferHandle(finalTarget);
			RenderCommand::SetViewport(0, 0, m_Width, m_Height);
			m_FxaaShader->Bind();
			RenderCommand::BindTextureSlot(0, m_LdrTarget->GetColorAttachmentRendererID(0));
			m_FxaaShader->SetInt("u_Image", 0);
			m_FxaaShader->SetFloat2("u_TexelSize", { 1.0f / m_Width, 1.0f / m_Height });
			DrawFullscreenTriangle();
		}

		RenderCommand::SetDepthTest(true);
		RenderCommand::SetDepthWrite(true);
	}

	void PostProcessStack::DrawFullscreenTriangle()
	{
		RenderCommand::DrawArrays(RenderCommand::PrimitiveTopology::Triangles, 0, 3);
	}
}
