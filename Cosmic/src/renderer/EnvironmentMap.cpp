// EnvironmentMap.cpp — S6.3 image-based lighting. See EnvironmentMap.h.

#include "renderer/EnvironmentMap.h"
#include "renderer/RenderCommand.h"
#include "renderer/Renderer3D.h"
#include "graphics/TextureCube.h"
#include "graphics/FrameBuffer.h"
#include "graphics/Shader.h"
#include "graphics/Mesh.h"
#include "core/Log.h"

#include <glm/gtc/matrix_transform.hpp>
#include <array>

namespace Cosmic
{
	namespace
	{
		constexpr uint32_t kEnvSize       = 256;
		constexpr uint32_t kIrradianceSz  = 32;
		constexpr uint32_t kPrefilterSize = 128;
		constexpr uint32_t kPrefilterMips = 5;   // roughness levels baked
		constexpr uint32_t kBrdfLutSize   = 512;

		// Texture units reserved for IBL (mirrors Renderer3D's IBL binding).
		// Chosen high so they never collide with a material's own textures (0..N).

		const glm::mat4 kCaptureProj =
			glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

		std::array<glm::mat4, 6> CaptureViews()
		{
			const glm::vec3 O(0.0f);
			return {
				glm::lookAt(O, glm::vec3( 1,  0,  0), glm::vec3(0, -1,  0)),
				glm::lookAt(O, glm::vec3(-1,  0,  0), glm::vec3(0, -1,  0)),
				glm::lookAt(O, glm::vec3( 0,  1,  0), glm::vec3(0,  0,  1)),
				glm::lookAt(O, glm::vec3( 0, -1,  0), glm::vec3(0,  0, -1)),
				glm::lookAt(O, glm::vec3( 0,  0,  1), glm::vec3(0, -1,  0)),
				glm::lookAt(O, glm::vec3( 0,  0, -1), glm::vec3(0, -1,  0)),
			};
		}
	}

	EnvironmentMap::~EnvironmentMap() = default;

	void EnvironmentMap::Init()
	{
		if (m_Initialized)
			return;

		m_EnvSkyShader     = Shader::Create("assets/shaders/EnvSky.glsl");
		m_IrradianceShader = Shader::Create("assets/shaders/IrradianceConvolve.glsl");
		m_PrefilterShader  = Shader::Create("assets/shaders/PrefilterEnv.glsl");
		m_BrdfShader       = Shader::Create("assets/shaders/BrdfLut.glsl");
		m_SkyboxShader     = Shader::Create("assets/shaders/Skybox.glsl");
		if (!m_EnvSkyShader || !m_IrradianceShader || !m_PrefilterShader || !m_BrdfShader || !m_SkyboxShader)
			CS_CORE_ERROR("EnvironmentMap: one or more IBL shaders failed to load — IBL disabled.");

		m_Cube = Mesh::CreateBox({ 1.0f, 1.0f, 1.0f });

		{
			TextureCubeSpecification env; env.Size = kEnvSize; env.Mipmapped = true;
			m_EnvCube = TextureCube::Create(env);
		}
		{
			TextureCubeSpecification irr; irr.Size = kIrradianceSz; irr.Mipmapped = false;
			m_Irradiance = TextureCube::Create(irr);
		}
		{
			TextureCubeSpecification pre; pre.Size = kPrefilterSize; pre.Mipmapped = true;
			m_Prefilter = TextureCube::Create(pre);
		}
		m_PrefilterMaxLod = static_cast<float>(kPrefilterMips - 1);

		// BRDF LUT: a single RGBA16F 2D target (we only use .rg).
		{
			FramebufferSpecification spec;
			spec.Width  = kBrdfLutSize;
			spec.Height = kBrdfLutSize;
			spec.Attachments = { FramebufferTextureFormat::RGBA16F };
			m_BrdfLut = FrameBuffer::Create(spec);
		}

		// Bake the (view-independent) BRDF LUT once.
		if (m_BrdfShader && m_BrdfLut)
		{
			m_BrdfLut->Bind();
			RenderCommand::SetViewport(0, 0, kBrdfLutSize, kBrdfLutSize);
			RenderCommand::SetDepthTest(false);
			RenderCommand::SetDepthWrite(false);
			m_BrdfShader->Bind();
			RenderCommand::DrawArrays(RenderCommand::PrimitiveTopology::Triangles, 0, 3);
			RenderCommand::SetDepthTest(true);
			RenderCommand::SetDepthWrite(true);
			m_BrdfLut->Unbind();
		}

		m_Initialized = true;
		m_Dirty       = true;
	}

	void EnvironmentMap::Shutdown()
	{
		m_EnvCube.reset();
		m_Irradiance.reset();
		m_Prefilter.reset();
		m_BrdfLut.reset();
		m_EnvSkyShader.reset();
		m_IrradianceShader.reset();
		m_PrefilterShader.reset();
		m_BrdfShader.reset();
		m_SkyboxShader.reset();
		m_Cube.reset();
		m_Initialized = false;
	}

	void EnvironmentMap::SetSunDirection(const glm::vec3& toSun)
	{
		const float len = glm::length(toSun);
		const glm::vec3 n = len > 1e-6f ? toSun / len : glm::vec3(0.0f, 1.0f, 0.0f);
		if (n != m_SunDir)
		{
			m_SunDir = n;
			m_Dirty  = true;
		}
	}

	void EnvironmentMap::SetSkyIntensity(float intensity)
	{
		if (intensity != m_SkyIntensity)
		{
			m_SkyIntensity = intensity;
			m_Dirty        = true;
		}
	}

	void EnvironmentMap::RenderCubeFaces(const Ref<Shader>& shader, const Ref<TextureCube>& target, uint32_t mip)
	{
		const auto views = CaptureViews();
		m_Cube->GetVertexArray()->Bind();
		for (uint32_t face = 0; face < 6; ++face)
		{
			target->BeginRenderToFace(face, mip);
			shader->SetMat4("u_ViewProjection", kCaptureProj * views[face]);
			RenderCommand::DrawIndexed(m_Cube->GetVertexArray(), m_Cube->GetIndexCount());
		}
	}

	void EnvironmentMap::Bake()
	{
		if (!m_Initialized || !m_Dirty)
			return;
		if (!m_EnvSkyShader || !m_IrradianceShader || !m_PrefilterShader ||
		    !m_EnvCube || !m_Irradiance || !m_Prefilter || !m_Cube)
			return;

		// Bake state: no depth interaction, render both cube faces (we sit inside it).
		RenderCommand::SetDepthTest(false);
		RenderCommand::SetDepthWrite(false);
		RenderCommand::SetCullMode(RenderCommand::CullMode::None);

		// 1) Procedural sky → environment cube.
		m_EnvSkyShader->Bind();
		m_EnvSkyShader->SetFloat3("u_SunDirection", m_SunDir);
		m_EnvSkyShader->SetFloat("u_SkyIntensity", m_SkyIntensity);
		RenderCubeFaces(m_EnvSkyShader, m_EnvCube, 0);
		m_EnvCube->FinishRender();
		m_EnvCube->GenerateMips();

		// 2) Diffuse irradiance convolution.
		m_IrradianceShader->Bind();
		m_EnvCube->Bind(0);
		m_IrradianceShader->SetInt("u_EnvironmentMap", 0);
		RenderCubeFaces(m_IrradianceShader, m_Irradiance, 0);
		m_Irradiance->FinishRender();

		// 3) Prefiltered specular (one roughness per mip).
		m_PrefilterShader->Bind();
		m_EnvCube->Bind(0);
		m_PrefilterShader->SetInt("u_EnvironmentMap", 0);
		m_PrefilterShader->SetFloat("u_Resolution", static_cast<float>(m_EnvCube->GetSize()));
		for (uint32_t mip = 0; mip < kPrefilterMips; ++mip)
		{
			const float roughness = kPrefilterMips > 1
				? static_cast<float>(mip) / static_cast<float>(kPrefilterMips - 1)
				: 0.0f;
			m_PrefilterShader->SetFloat("u_Roughness", roughness);
			RenderCubeFaces(m_PrefilterShader, m_Prefilter, mip);
		}
		m_Prefilter->FinishRender();

		// Restore engine defaults (contract: bakers restore what they change).
		RenderCommand::SetDepthTest(true);
		RenderCommand::SetDepthWrite(true);
		RenderCommand::SetCullMode(RenderCommand::CullMode::None);

		m_Dirty = false;
	}

	void EnvironmentMap::PushToRenderer() const
	{
		if (m_Initialized && m_Irradiance && m_Prefilter && m_BrdfLut)
			Renderer3D::SetIBL(m_Irradiance->GetRendererID(), m_Prefilter->GetRendererID(),
			                   m_BrdfLut->GetColorAttachmentRendererID(0), m_PrefilterMaxLod);
	}

	void EnvironmentMap::DrawSkybox(const glm::mat4& viewProjection)
	{
		if (!m_Initialized || !m_SkyboxShader || !m_EnvCube)
			return;

		// Background fill: depth test/write off, so it colors every pixel and the
		// opaque scene (drawn afterward) overwrites it. (LEQUAL-after-opaque would
		// be more efficient but needs a depth-func verb we don't have yet.)
		RenderCommand::SetDepthTest(false);
		RenderCommand::SetDepthWrite(false);

		m_SkyboxShader->Bind();
		RenderCommand::BindTextureCubeSlot(0, m_EnvCube->GetRendererID());
		m_SkyboxShader->SetInt("u_EnvironmentMap", 0);
		m_SkyboxShader->SetMat4("u_InvViewProj", glm::inverse(viewProjection));
		RenderCommand::DrawArrays(RenderCommand::PrimitiveTopology::Triangles, 0, 3);

		RenderCommand::SetDepthTest(true);
		RenderCommand::SetDepthWrite(true);
	}

	uint32_t EnvironmentMap::GetIrradianceID() const { return m_Irradiance ? m_Irradiance->GetRendererID() : 0; }
	uint32_t EnvironmentMap::GetPrefilterID()  const { return m_Prefilter  ? m_Prefilter->GetRendererID()  : 0; }
	uint32_t EnvironmentMap::GetBrdfLutID()     const { return m_BrdfLut     ? m_BrdfLut->GetColorAttachmentRendererID(0) : 0; }
}
