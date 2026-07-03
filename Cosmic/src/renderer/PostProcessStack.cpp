// PostProcessStack.cpp
// Last Modified: 7/2/2026

#include "renderer/PostProcessStack.h"
#include "renderer/RenderCommand.h"
#include "graphics/FrameBuffer.h"
#include "graphics/Shader.h"
#include "core/Log.h"

namespace Cosmic
{
	// Out-of-line so the Ref<FrameBuffer>/Ref<Shader> members are destroyed where
	// those types are complete (they are only forward-declared in the header).
	PostProcessStack::~PostProcessStack() = default;

	void PostProcessStack::Init(uint32_t width, uint32_t height)
	{
		if (m_Initialized)
			return;

		m_Width  = width  > 0 ? width  : 1;
		m_Height = height > 0 ? height : 1;

		// HDR scene target: float color so overbright survives to the tonemap;
		// depth so the 3D pass depth-tests normally. (The empty-attachment default
		// would give RGBA8 — we want RGBA16F here, contract rule 7.)
		FramebufferSpecification spec;
		spec.Width  = m_Width;
		spec.Height = m_Height;
		spec.Attachments = { FramebufferTextureFormat::RGBA16F,
		                     FramebufferTextureFormat::DEPTH24STENCIL8 };
		m_SceneHDR = FrameBuffer::Create(spec);

		m_TonemapShader = Shader::Create("assets/shaders/Tonemap.glsl");
		if (!m_TonemapShader)
			CS_CORE_ERROR("PostProcessStack: failed to load Tonemap shader — HDR resolve disabled.");

		m_Initialized = true;
	}

	void PostProcessStack::Shutdown()
	{
		// Release GPU handles while the GL context is still current (same teardown
		// contract as Renderer3D::Shutdown).
		m_SceneHDR.reset();
		m_TonemapShader.reset();
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

	void PostProcessStack::Composite(float exposure)
	{
		if (!m_SceneHDR || !m_TonemapShader)
			return;

		// Fullscreen resolve — no depth interaction. Disable depth test + write so
		// the triangle always writes every texel of the (already-bound) LDR target,
		// then restore ON/ON (the Init() default Renderer2D relies on).
		RenderCommand::SetDepthTest(false);
		RenderCommand::SetDepthWrite(false);

		m_TonemapShader->Bind();
		RenderCommand::BindTextureSlot(0, m_SceneHDR->GetColorAttachmentRendererID(0));
		m_TonemapShader->SetInt("u_Scene", 0);
		m_TonemapShader->SetFloat("u_Exposure", exposure);

		DrawFullscreenTriangle();

		RenderCommand::SetDepthTest(true);
		RenderCommand::SetDepthWrite(true);
	}

	void PostProcessStack::DrawFullscreenTriangle()
	{
		// 3 attribute-less verts; Tonemap.glsl (and any post shader) positions them
		// from gl_VertexID. RenderCommand binds its private empty VAO for the draw.
		RenderCommand::DrawArrays(RenderCommand::PrimitiveTopology::Triangles, 0, 3);
	}
}
