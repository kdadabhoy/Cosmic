// CoverageCapture.cpp — S11.1 top-down accumulation mask. See CoverageCapture.h.

#include "renderer/CoverageCapture.h"
#include "renderer/RenderCommand.h"
#include "renderer/InstanceSet.h"
#include "graphics/FrameBuffer.h"
#include "graphics/Shader.h"
#include "graphics/Mesh.h"
#include "core/Log.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <utility>

namespace Cosmic
{
	CoverageCapture::~CoverageCapture() = default;

	namespace
	{
		Ref<FrameBuffer> MakeMaskFbo(uint32_t size)
		{
			FramebufferSpecification spec;
			spec.Width  = size;
			spec.Height = size;
			spec.Attachments = { FramebufferTextureFormat::RGBA16F };   // only RG used
			return FrameBuffer::Create(spec);
		}
	}

	void CoverageCapture::Init(uint32_t resolution, const glm::vec2& worldMin, float worldSize,
	                           float worldYMin, float worldYMax)
	{
		if (m_Initialized)
			return;

		m_Resolution = resolution > 0 ? resolution : 512;
		m_WorldMin   = worldMin;
		m_WorldSize  = worldSize > 1e-3f ? worldSize : 1.0f;
		m_WorldYMin  = worldYMin;
		m_WorldYMax  = worldYMax > worldYMin + 1e-3f ? worldYMax : worldYMin + 1.0f;

		// Depth-only top-down capture (a lone depth attachment ⇒ 0 color attachments,
		// same shape as ShadowMap's depth FBO — sampled as a plain sampler2D).
		{
			FramebufferSpecification spec;
			spec.Width  = m_Resolution;
			spec.Height = m_Resolution;
			spec.Attachments = { FramebufferTextureFormat::DEPTH24STENCIL8 };
			m_DepthFbo = FrameBuffer::Create(spec);
		}
		m_MaskA = MakeMaskFbo(m_Resolution);
		m_MaskB = MakeMaskFbo(m_Resolution);

		m_DepthShader = Shader::Create("assets/shaders/ShadowDepth.glsl");
		m_AccumShader = Shader::Create("assets/shaders/SnowAccum.glsl");
		if (!m_DepthShader || !m_AccumShader)
			CS_CORE_ERROR("CoverageCapture: a required shader failed to load — coverage disabled.");

		RebuildCaptureMatrix();

		// Clear both ping-pong targets to zero coverage so the first UpdateCoverage
		// (u_FirstFrame = 1) starts from a known state.
		const uint32_t prevFbo = RenderCommand::GetBoundFramebuffer();
		RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 0.0f });
		if (m_MaskA) { m_MaskA->Bind(); RenderCommand::SetViewport(0, 0, m_Resolution, m_Resolution); RenderCommand::Clear(); }
		if (m_MaskB) { m_MaskB->Bind(); RenderCommand::SetViewport(0, 0, m_Resolution, m_Resolution); RenderCommand::Clear(); }
		RenderCommand::BindFramebufferHandle(prevFbo);

		m_FirstFrame  = true;
		m_Initialized = true;
	}

	void CoverageCapture::Shutdown()
	{
		m_DepthFbo.reset();
		m_MaskA.reset();
		m_MaskB.reset();
		m_DepthShader.reset();
		m_DepthInstancedShader.reset();
		m_AccumShader.reset();
		m_Initialized = false;
		m_FirstFrame  = true;
	}

	void CoverageCapture::RebuildCaptureMatrix()
	{
		// Top-down orthographic camera looking straight DOWN (-Y) over the world
		// rect. The ortho's Y clip axis is FLIPPED (bottom > top) so the depth
		// capture's texel (u,v) maps to world (minX + u*size, minZ + v*size) — the
		// SAME mapping the material shaders use for u_SnowMaskRect (a plain down-look
		// would mirror Z). up = -Z keeps a right-handed basis with right = +X.
		const float     half   = m_WorldSize * 0.5f;
		const glm::vec3 center(m_WorldMin.x + half, 0.0f, m_WorldMin.y + half);
		const float     eyeY   = m_WorldYMax + 1.0f;   // small margin above the volume top
		const glm::vec3 eye(center.x, eyeY, center.z);
		const float     nearP  = 1.0f;                 // eyeY - worldYMax
		const float     farP   = eyeY - m_WorldYMin;   // reaches the volume floor

		const glm::mat4 view = glm::lookAt(eye, glm::vec3(center.x, m_WorldYMin, center.z),
		                                   glm::vec3(0.0f, 0.0f, -1.0f));
		const glm::mat4 proj = glm::ortho(-half, half, half, -half, nearP, farP);   // Y flipped
		m_CaptureVP = proj * view;
	}

	void CoverageCapture::BeginDepthCapture()
	{
		if (!m_Initialized || !m_DepthFbo || !m_DepthShader || m_InCapture)
			return;

		m_PrevFbo = RenderCommand::GetBoundFramebuffer();
		m_DepthFbo->Bind();
		RenderCommand::SetViewport(0, 0, m_Resolution, m_Resolution);
		// Capture the TOP surface: depth-LESS keeps the nearest (highest) fragment.
		RenderCommand::SetDepthTest(true);
		RenderCommand::SetDepthWrite(true);
		RenderCommand::Clear();   // depth clears to 1.0

		m_DepthShader->Bind();
		m_DepthShader->SetMat4("u_LightViewProj", m_CaptureVP);
		m_InCapture = true;
	}

	void CoverageCapture::DrawCaster(const Ref<Mesh>& mesh, const glm::mat4& transform)
	{
		if (!m_InCapture || !mesh || !m_DepthShader)
			return;

		// Re-bind the plain depth program (an interleaved instanced draw may have
		// left the instanced program bound; uniform setters target the bound program).
		m_DepthShader->Bind();
		m_DepthShader->SetMat4("u_Model", transform);
		mesh->GetVertexArray()->Bind();
		RenderCommand::DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());
	}

	void CoverageCapture::DrawCasterInstanced(const Ref<Mesh>& mesh, const Ref<InstanceSet>& instances, uint32_t count)
	{
		if (!m_InCapture || !mesh || !instances || count == 0)
			return;

		const uint32_t drawCount = std::min(count, instances->GetCount());
		if (drawCount == 0)
			return;

		if (!m_DepthInstancedShader)
		{
			m_DepthInstancedShader = Shader::Create("assets/shaders/ShadowDepthInstanced.glsl");
			if (!m_DepthInstancedShader)
			{
				CS_CORE_ERROR("CoverageCapture: ShadowDepthInstanced shader failed to load — instanced casters disabled.");
				return;
			}
		}

		m_DepthInstancedShader->Bind();
		m_DepthInstancedShader->SetMat4("u_LightViewProj", m_CaptureVP);
		instances->Bind();
		mesh->GetVertexArray()->Bind();
		RenderCommand::DrawIndexedInstanced(mesh->GetVertexArray(), mesh->GetIndexCount(), drawCount);
	}

	void CoverageCapture::EndDepthCapture()
	{
		if (!m_InCapture)
			return;
		RenderCommand::BindFramebufferHandle(m_PrevFbo);   // caller re-asserts its viewport
		m_InCapture = false;
	}

	void CoverageCapture::UpdateCoverage(float dt, float accumPerSec, float meltPerSec)
	{
		if (!m_Initialized || !m_AccumShader || !m_MaskA || !m_MaskB || !m_DepthFbo)
			return;

		const float height = m_WorldYMax - m_WorldYMin;
		const uint32_t prevFbo = RenderCommand::GetBoundFramebuffer();

		RenderCommand::SetDepthTest(false);
		RenderCommand::SetDepthWrite(false);

		// SnowAccum.glsl: write m_MaskB from m_MaskA (ping-pong read) + the fresh
		// top-down depth capture.
		m_MaskB->Bind();
		RenderCommand::SetViewport(0, 0, m_Resolution, m_Resolution);
		m_AccumShader->Bind();
		RenderCommand::BindTextureSlot(0, m_MaskA->GetColorAttachmentRendererID(0));
		m_AccumShader->SetInt("u_PrevMask", 0);
		RenderCommand::BindTextureSlot(1, m_DepthFbo->GetDepthAttachmentRendererID());
		m_AccumShader->SetInt("u_TopDepth", 1);
		m_AccumShader->SetFloat("u_AccumRate", accumPerSec);
		m_AccumShader->SetFloat("u_MeltRate", meltPerSec);
		m_AccumShader->SetFloat("u_DeltaTime", dt);
		m_AccumShader->SetFloat2("u_DepthToWorldY", { -height, m_WorldYMax });
		m_AccumShader->SetFloat2("u_WorldYEncode", { height > 1e-5f ? 1.0f / height : 0.0f, m_WorldYMin });
		m_AccumShader->SetFloat("u_FirstFrame", m_FirstFrame ? 1.0f : 0.0f);
		RenderCommand::DrawArrays(RenderCommand::PrimitiveTopology::Triangles, 0, 3);

		RenderCommand::SetDepthTest(true);
		RenderCommand::SetDepthWrite(true);
		RenderCommand::BindFramebufferHandle(prevFbo);

		// m_MaskA is now the LATEST mask (material samples it); m_MaskB is next
		// update's read/scratch.
		std::swap(m_MaskA, m_MaskB);
		m_FirstFrame = false;
	}

	uint32_t CoverageCapture::GetMaskTextureID() const
	{
		return m_MaskA ? m_MaskA->GetColorAttachmentRendererID(0) : 0;
	}

	void CoverageCapture::FillSnowDesc(Renderer3D::SnowDesc& snow) const
	{
		snow.MaskTextureID   = GetMaskTextureID();
		snow.MaskWorldMin    = m_WorldMin;
		snow.MaskWorldInvSize = { 1.0f / m_WorldSize, 1.0f / m_WorldSize };
		snow.MaskYDecode     = { m_WorldYMax - m_WorldYMin, m_WorldYMin };   // topY = g*height + min
	}
}
