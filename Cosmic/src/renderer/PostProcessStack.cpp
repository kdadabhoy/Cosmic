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
		if (!m_SsaoShader || !m_SsaoBlurShader || !m_BloomPrefilterShader || !m_BloomBlurShader || !m_FxaaShader)
			CS_CORE_ERROR("PostProcessStack: one or more effect shaders failed to load.");

		// --- Targets ---
		ResizeEffects();

		// --- SSAO hemisphere kernel + 4x4 rotation noise ---
		std::mt19937 rng(1337u);
		std::uniform_real_distribution<float> u01(0.0f, 1.0f);
		std::uniform_real_distribution<float> u11(-1.0f, 1.0f);

		m_Kernel.clear();
		for (int i = 0; i < 32; ++i)
		{
			glm::vec3 s(u11(rng), u11(rng), u01(rng));   // tangent-space hemisphere (+z)
			s = glm::normalize(s) * u01(rng);
			float t = static_cast<float>(i) / 32.0f;
			s *= glm::mix(0.1f, 1.0f, t * t);            // cluster samples near the origin
			m_Kernel.push_back(s);
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

		m_SsaoTarget     = MakeColorFbo(hw, hh, FramebufferTextureFormat::RGBA16F);
		m_SsaoBlurTarget = MakeColorFbo(hw, hh, FramebufferTextureFormat::RGBA16F);
		m_BloomA         = MakeColorFbo(hw, hh, FramebufferTextureFormat::RGBA16F);
		m_BloomB         = MakeColorFbo(hw, hh, FramebufferTextureFormat::RGBA16F);
		m_LdrTarget      = MakeColorFbo(m_Width, m_Height, FramebufferTextureFormat::RGBA8);
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
		m_BloomPrefilterShader.reset();
		m_BloomBlurShader.reset();
		m_BloomA.reset();
		m_BloomB.reset();
		m_FxaaShader.reset();
		m_LdrTarget.reset();
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
		if (!m_Initialized)
			return;

		if (m_SsaoEnabled)
			RenderSSAO(projection);
		if (m_BloomEnabled)
			RenderBloom();
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
		{
			char name[32];
			std::snprintf(name, sizeof(name), "u_Kernel[%zu]", i);
			m_SsaoShader->SetFloat3(name, m_Kernel[i]);
		}
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

		DrawFullscreenTriangle();

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
