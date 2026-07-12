// ShadowMap.cpp — S6.4 directional sun shadows. See ShadowMap.h.

#include "renderer/ShadowMap.h"
#include "renderer/RenderCommand.h"
#include "renderer/Renderer3D.h"
#include "renderer/InstanceSet.h"
#include "renderer/BindingPoints.h"   // A2 — SkinningSsbo
#include "graphics/FrameBuffer.h"
#include "graphics/Shader.h"
#include "graphics/Mesh.h"
#include "graphics/StorageBuffer.h"   // A2 — skinned-caster palette
#include "core/Log.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Cosmic
{
	ShadowMap::~ShadowMap() = default;

	void ShadowMap::Init(uint32_t size)
	{
		if (m_Initialized)
			return;

		m_Size = size > 0 ? size : 2048;

		// Depth-only FBO: a lone DEPTH24STENCIL8 attachment ⇒ 0 color attachments
		// (the OpenGL impl calls glDrawBuffer(GL_NONE)); the depth texture is the
		// shadow map, sampled as a plain sampler2D (NEAREST/clamp params already set).
		FramebufferSpecification spec;
		spec.Width  = m_Size;
		spec.Height = m_Size;
		spec.Attachments = { FramebufferTextureFormat::DEPTH24STENCIL8 };
		m_Fbo = FrameBuffer::Create(spec);

		m_DepthShader = Shader::Create("assets/shaders/ShadowDepth.glsl");
		if (!m_DepthShader)
			CS_CORE_ERROR("ShadowMap: ShadowDepth shader failed to load — shadows disabled.");

		m_Initialized = true;
	}

	void ShadowMap::Shutdown()
	{
		m_Fbo.reset();
		m_DepthShader.reset();
		m_DepthInstancedShader.reset();
		m_DepthSkinnedShader.reset();
		m_SkinSsbo.reset();
		m_SkinSsboCapacity = 0;
		m_Initialized = false;
	}

	void ShadowMap::SetLight(const glm::vec3& sunTravelDir, const glm::vec3& center, float radius)
	{
		radius = radius > 1e-3f ? radius : 1.0f;

		glm::vec3 dir = glm::length(sunTravelDir) > 1e-6f
			? glm::normalize(sunTravelDir)
			: glm::vec3(0.0f, -1.0f, 0.0f);

		// Position the light back along -dir so the whole sphere is in front of it.
		const glm::vec3 lightPos = center - dir * (radius * 2.0f);
		const glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);

		const glm::mat4 view = glm::lookAt(lightPos, center, up);
		const glm::mat4 proj = glm::ortho(-radius, radius, -radius, radius, 0.05f, radius * 4.0f);
		m_LightViewProj = proj * view;
	}

	void ShadowMap::BeginDepthPass()
	{
		if (!m_Initialized || !m_Fbo || !m_DepthShader)
			return;

		m_Fbo->Bind();
		RenderCommand::SetViewport(0, 0, m_Size, m_Size);
		RenderCommand::Clear();   // depth clears to 1.0 (no color attachment to clear)

		// Front-face culling during the depth pass pushes self-shadow acne to the
		// back faces (peter-panning trade). Restored to None in EndDepthPass.
		RenderCommand::SetCullMode(RenderCommand::CullMode::Front);

		m_DepthShader->Bind();
		m_DepthShader->SetMat4("u_LightViewProj", m_LightViewProj);
	}

	void ShadowMap::DrawCaster(const Ref<Mesh>& mesh, const glm::mat4& transform)
	{
		if (!m_Initialized || !mesh || !m_DepthShader)
			return;

		// Re-bind the plain depth program: an interleaved DrawCasterInstanced may
		// have left the instanced program bound, and the uniform setters below
		// target the CURRENTLY bound program. Program uniforms persist, so
		// u_LightViewProj (set in BeginDepthPass) is restored by the rebind.
		m_DepthShader->Bind();
		m_DepthShader->SetMat4("u_Model", transform);
		mesh->GetVertexArray()->Bind();
		RenderCommand::DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());
	}

	void ShadowMap::DrawCasterSkinned(const Ref<Mesh>& mesh, const glm::mat4& transform,
	                                  const glm::mat4* palette, uint32_t jointCount)
	{
		if (!m_Initialized || !mesh)
			return;
		if (!palette || jointCount == 0)
		{
			DrawCaster(mesh, transform);   // bind-pose fallback
			return;
		}

		if (!m_DepthSkinnedShader)
		{
			m_DepthSkinnedShader = Shader::Create("assets/shaders/ShadowDepthSkinned.glsl");
			if (!m_DepthSkinnedShader)
			{
				CS_CORE_ERROR("ShadowMap: ShadowDepthSkinned shader failed to load — skinned casters draw bind pose.");
				DrawCaster(mesh, transform);
				return;
			}
		}

		// Per-caster palette upload at base 0. This map owns its own binding-10
		// buffer; the lit pass's Renderer3D upload happens later in the frame
		// and re-binds the slot, so the two never fight.
		if (!m_SkinSsbo || m_SkinSsboCapacity < jointCount)
		{
			uint32_t cap = m_SkinSsboCapacity ? m_SkinSsboCapacity : 128;
			while (cap < jointCount)
				cap *= 2;
			m_SkinSsbo = StorageBuffer::Create(cap * (uint32_t)sizeof(glm::mat4), Bindings::SkinningSsbo);
			m_SkinSsboCapacity = m_SkinSsbo ? cap : 0;
		}
		if (!m_SkinSsbo)
		{
			DrawCaster(mesh, transform);
			return;
		}
		m_SkinSsbo->SetData(palette, jointCount * (uint32_t)sizeof(glm::mat4));
		m_SkinSsbo->Bind();

		m_DepthSkinnedShader->Bind();
		m_DepthSkinnedShader->SetMat4("u_LightViewProj", m_LightViewProj);
		m_DepthSkinnedShader->SetMat4("u_Model", transform);
		m_DepthSkinnedShader->SetInt("u_SkinBase", 0);

		mesh->GetVertexArray()->Bind();
		RenderCommand::DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());
	}

	void ShadowMap::DrawCasterInstanced(const Ref<Mesh>& mesh, const Ref<InstanceSet>& instances, uint32_t count)
	{
		if (!m_Initialized || !mesh || !instances || count == 0)
			return;

		const uint32_t drawCount = std::min(count, instances->GetCount());
		if (drawCount == 0)
			return;

		if (!m_DepthInstancedShader)
		{
			m_DepthInstancedShader = Shader::Create("assets/shaders/ShadowDepthInstanced.glsl");
			if (!m_DepthInstancedShader)
			{
				CS_CORE_ERROR("ShadowMap: ShadowDepthInstanced shader failed to load — instanced casters disabled.");
				return;
			}
		}

		// Bind the instanced program and (re-)assert its light matrix — it has its
		// own uniform state separate from the plain depth program.
		m_DepthInstancedShader->Bind();
		m_DepthInstancedShader->SetMat4("u_LightViewProj", m_LightViewProj);

		instances->Bind();
		mesh->GetVertexArray()->Bind();
		RenderCommand::DrawIndexedInstanced(mesh->GetVertexArray(), mesh->GetIndexCount(), drawCount);
	}

	void ShadowMap::EndDepthPass()
	{
		if (!m_Initialized || !m_Fbo)
			return;

		RenderCommand::SetCullMode(RenderCommand::CullMode::None);   // restore engine default
		m_Fbo->Unbind();
	}

	void ShadowMap::PushToRenderer(float bias) const
	{
		if (m_Initialized && m_Fbo)
			Renderer3D::SetShadow(m_Fbo->GetDepthAttachmentRendererID(), m_LightViewProj, bias);
	}

	uint32_t ShadowMap::GetDepthID() const
	{
		return m_Fbo ? m_Fbo->GetDepthAttachmentRendererID() : 0;
	}
}
