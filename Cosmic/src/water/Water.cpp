// Water.cpp — S9.1 Tier 1 water surface + S9.2 buoyancy queries. See Water.h.

#include "water/Water.h"

#include "renderer/RenderCommand.h"
#include "renderer/Renderer3D.h"
#include "renderer/PostProcessStack.h"
#include "terrain/Terrain.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/FrameBuffer.h"
#include "math/Noise.h"
#include "core/Log.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Cosmic
{
	namespace
	{
		constexpr uint32_t kMaxShaderWaves  = 8;    // v2 (F6): mirrored by Water.glsl's arrays
		constexpr uint32_t kDetailTexSize   = 128;

		// Lengyel oblique near-plane: replace the projection's near plane with an
		// arbitrary VIEW-SPACE clip plane (used to clip the mirrored reflection
		// render at the water surface without any extra render state).
		glm::mat4 MakeObliqueProjection(glm::mat4 proj, const glm::vec4& clipPlaneVS)
		{
			const glm::vec4 q = glm::inverse(proj) * glm::vec4(
				clipPlaneVS.x >= 0.0f ? 1.0f : -1.0f,
				clipPlaneVS.y >= 0.0f ? 1.0f : -1.0f,
				1.0f, 1.0f);
			const glm::vec4 c = clipPlaneVS * (2.0f / glm::dot(clipPlaneVS, q));

			// Third row (z) = c - fourth row (w). glm is column-major: [col][row].
			proj[0][2] = c.x - proj[0][3];
			proj[1][2] = c.y - proj[1][3];
			proj[2][2] = c.z - proj[2][3];
			proj[3][2] = c.w - proj[3][3];
			return proj;
		}
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Factory + queries
	/////////////////////////////////////////////////////////////////////////////////

	Ref<Water> Water::Create(const WaterSpecification& spec)
	{
		if (spec.Extent.x <= 0.0f || spec.Extent.y <= 0.0f || spec.GridResolution < 2)
		{
			CS_CORE_ERROR("Water: bad spec — Extent must be positive and GridResolution >= 2.");
			return nullptr;
		}

		auto water = std::make_shared<Water>();
		water->m_Spec  = spec;
		water->m_Waves = spec.Waves;

		if (water->m_Waves.empty())
		{
			// A plausible default swell: one primary + two crossing detail waves.
			water->m_Waves = {
				{ {  1.00f,  0.25f }, 14.0f, 0.18f, 0.55f, 0.0f, 0.0f },
				{ {  0.65f, -0.75f },  7.0f, 0.09f, 0.45f, 0.0f, 1.3f },
				{ { -0.30f,  0.95f },  3.5f, 0.04f, 0.35f, 0.0f, 2.6f },
			};
		}
		if (water->m_Waves.size() > kMaxShaderWaves)
		{
			CS_CORE_WARN("Water: {} waves supplied; the shader evaluates the first {}.",
			             water->m_Waves.size(), kMaxShaderWaves);
			water->m_Waves.resize(kMaxShaderWaves);
		}
		return water;
	}

	Water::~Water() = default;

	float Water::SampleHeight(float x, float z, float timeSeconds) const
	{
		return m_Spec.SurfaceHeight + SampleGerstnerHeight(m_Waves, x, z, timeSeconds);
	}

	glm::vec3 Water::SampleNormal(float x, float z, float timeSeconds) const
	{
		return SampleGerstnerNormal(m_Waves, x, z, timeSeconds);
	}

	void Water::SetShoreTerrain(const Ref<Terrain>& terrain)
	{
		m_ShoreTerrain = terrain;   // null clears the shore gate (open water)
	}

	/////////////////////////////////////////////////////////////////////////////////
	// GPU resources (lazy)
	/////////////////////////////////////////////////////////////////////////////////

	Ref<Texture2D> Water::MakeDetailNormalMap(uint32_t seed) const
	{
		// Tangent-space normals from an fBm heightfield gradient. Value-noise on
		// an 8-period lattice is near-tileable at detail scale; residual seams
		// vanish under the dual-scroll blend.
		const Noise noise(seed);
		const float strength = 2.2f;

		std::vector<uint8_t> texels(static_cast<size_t>(kDetailTexSize) * kDetailTexSize * 4);
		auto heightAt = [&](int i, int j)
		{
			const float u = static_cast<float>(i) / kDetailTexSize;
			const float v = static_cast<float>(j) / kDetailTexSize;
			return noise.Fbm2D(u * 8.0f, v * 8.0f, 4, 2.0f, 0.5f);
		};

		for (int j = 0; j < static_cast<int>(kDetailTexSize); ++j)
			for (int i = 0; i < static_cast<int>(kDetailTexSize); ++i)
			{
				const float dx = (heightAt(i + 1, j) - heightAt(i - 1, j)) * strength;
				const float dy = (heightAt(i, j + 1) - heightAt(i, j - 1)) * strength;
				const glm::vec3 n = glm::normalize(glm::vec3(-dx, -dy, 1.0f));

				const size_t s = (static_cast<size_t>(j) * kDetailTexSize + i) * 4;
				texels[s + 0] = static_cast<uint8_t>((n.x * 0.5f + 0.5f) * 255.0f);
				texels[s + 1] = static_cast<uint8_t>((n.y * 0.5f + 0.5f) * 255.0f);
				texels[s + 2] = static_cast<uint8_t>((n.z * 0.5f + 0.5f) * 255.0f);
				texels[s + 3] = 255;
			}

		// Mipmapped: the detail normals tile ~0.12 repeats/m and are sampled out to
		// the horizon, so without a mip chain they alias into salt-and-pepper past a
		// few hundred meters. The mipmapped ctor sets trilinear + repeat and SetData
		// regenerates the chain, so no SetSampling override is needed here.
		Ref<Texture2D> tex = Texture2D::Create(kDetailTexSize, kDetailTexSize, /*mipmapped*/ true);
		if (tex)
			tex->SetData(texels.data(), static_cast<uint32_t>(texels.size()));
		return tex;
	}

	bool Water::EnsureGpuResources()
	{
		if (m_GpuReady)
			return true;

		m_Shader     = Shader::Create("assets/shaders/Water.glsl");
		m_CopyShader = Shader::Create("assets/shaders/BlitCopy.glsl");
		if (!m_Shader || !m_CopyShader)
		{
			CS_CORE_ERROR("Water: shader load failed — water rendering disabled.");
			return false;
		}

		// Flat grid; the vertex stage displaces it by the Gerstner sum.
		const int res = static_cast<int>(m_Spec.GridResolution);
		std::vector<MeshVertex> verts;
		std::vector<uint32_t>   idx;
		verts.reserve(static_cast<size_t>(res) * res);
		for (int j = 0; j < res; ++j)
			for (int i = 0; i < res; ++i)
			{
				MeshVertex v{};
				const float u = static_cast<float>(i) / (res - 1);
				const float w = static_cast<float>(j) / (res - 1);
				v.Position = { u - 0.5f, 0.0f, w - 0.5f };
				v.Normal   = { 0.0f, 1.0f, 0.0f };
				v.TexCoord = { u, w };
				verts.push_back(v);
			}
		for (int j = 0; j < res - 1; ++j)
			for (int i = 0; i < res - 1; ++i)
			{
				const uint32_t v00 = j * res + i,       v10 = j * res + i + 1;
				const uint32_t v01 = (j + 1) * res + i, v11 = (j + 1) * res + i + 1;
				idx.insert(idx.end(), { v00, v11, v10 });
				idx.insert(idx.end(), { v00, v01, v11 });
			}
		m_Grid = Mesh::Create(verts, idx);

		m_DetailA = MakeDetailNormalMap(0x11223344u);
		m_DetailB = MakeDetailNormalMap(0x55667788u);

		{
			FramebufferSpecification spec;
			spec.Width  = m_Spec.ReflectionResolution;
			spec.Height = m_Spec.ReflectionResolution;
			spec.Attachments = { FramebufferTextureFormat::RGBA16F,
			                     FramebufferTextureFormat::DEPTH24STENCIL8 };
			m_ReflectionFbo = FrameBuffer::Create(spec);
		}
		{
			FramebufferSpecification spec;
			spec.Width  = 1;   // resized to the viewport on first Render
			spec.Height = 1;
			spec.Attachments = { FramebufferTextureFormat::RGBA16F };
			m_RefractionFbo = FrameBuffer::Create(spec);
		}

		m_GpuReady = m_Grid && m_DetailA && m_DetailB && m_ReflectionFbo && m_RefractionFbo;
		return m_GpuReady;
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Reflection pass
	/////////////////////////////////////////////////////////////////////////////////

	bool Water::BeginReflection(const glm::mat4& view, const glm::mat4& projection,
	                            const glm::vec3& cameraPos,
	                            glm::mat4& outMirroredViewProj, glm::vec3& outMirroredCamPos)
	{
		if (!EnsureGpuResources() || m_InReflection)
			return false;

		const float h = m_Spec.SurfaceHeight;

		// Mirror the camera about the plane y = h.
		const glm::mat4 mirror =
			glm::translate(glm::mat4(1.0f), { 0.0f, h, 0.0f }) *
			glm::scale(glm::mat4(1.0f), { 1.0f, -1.0f, 1.0f }) *
			glm::translate(glm::mat4(1.0f), { 0.0f, -h, 0.0f });
		const glm::mat4 mirroredView = view * mirror;

		// Clip at the surface with an oblique near plane: keep y >= h. Transform
		// the world plane (0,1,0,-h) into the mirrored camera's view space
		// (planes transform by the inverse-transpose).
		glm::vec4 planeVS = glm::transpose(glm::inverse(mirroredView)) * glm::vec4(0.0f, 1.0f, 0.0f, -h);
		if (planeVS.w > 0.0f)
			planeVS = -planeVS;   // camera must sit on the negative side
		const glm::mat4 obliqueProj = MakeObliqueProjection(projection, planeVS);

		outMirroredViewProj = obliqueProj * mirroredView;
		outMirroredCamPos   = { cameraPos.x, 2.0f * h - cameraPos.y, cameraPos.z };
		m_ReflectionViewProj = outMirroredViewProj;

		m_PrevFramebuffer = RenderCommand::GetBoundFramebuffer();
		m_ReflectionFbo->Bind();
		RenderCommand::SetViewport(0, 0, m_Spec.ReflectionResolution, m_Spec.ReflectionResolution);
		RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
		RenderCommand::Clear();

		m_InReflection = true;
		return true;
	}

	void Water::EndReflection()
	{
		if (!m_InReflection)
			return;
		RenderCommand::BindFramebufferHandle(m_PrevFramebuffer);
		m_InReflection  = false;
		m_HasReflection = true;
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Surface draw
	/////////////////////////////////////////////////////////////////////////////////

	void Water::Render(const glm::vec3& cameraPos, float timeSeconds,
	                   const glm::mat4& viewProjection,
	                   uint32_t sceneColorID, uint32_t sceneDepthID,
	                   uint32_t viewportWidth, uint32_t viewportHeight,
	                   int entityID)
	{
		if (!EnsureGpuResources() || viewportWidth == 0 || viewportHeight == 0)
			return;

		// --- 1) Refraction grab: copy the scene color rendered so far. The water
		// shader cannot sample the attachment of the target it renders into
		// (feedback loop), so this copy is mandatory, not an optimization. ---
		const uint32_t callerFbo = RenderCommand::GetBoundFramebuffer();
		if (m_RefractionFbo->GetWidth() != viewportWidth || m_RefractionFbo->GetHeight() != viewportHeight)
			m_RefractionFbo->Resize(viewportWidth, viewportHeight);

		m_RefractionFbo->Bind();
		RenderCommand::SetViewport(0, 0, viewportWidth, viewportHeight);
		RenderCommand::SetDepthTest(false);
		RenderCommand::SetDepthWrite(false);
		m_CopyShader->Bind();
		RenderCommand::BindTextureSlot(0, sceneColorID);
		m_CopyShader->SetInt("u_Source", 0);
		PostProcessStack::DrawFullscreenTriangle();
		RenderCommand::SetDepthTest(true);
		RenderCommand::SetDepthWrite(true);

		RenderCommand::BindFramebufferHandle(callerFbo);
		RenderCommand::SetViewport(0, 0, viewportWidth, viewportHeight);

		// --- 2) Draw the displaced grid into the caller's target. ---
		m_Shader->Bind();
		Renderer3D::ApplySceneBindings(m_Shader);   // sun shadow + IBL fallback reflections

		m_Shader->SetFloat2("u_Center", m_Spec.Center);
		m_Shader->SetFloat2("u_Extent", m_Spec.Extent);
		m_Shader->SetFloat("u_SurfaceHeight", m_Spec.SurfaceHeight);
		m_Shader->SetFloat("u_Time", timeSeconds);
		m_Shader->SetInt("u_EntityID", entityID);

		// Wave set — precomputed k / omega / effective steepness per wave so the
		// shader stays a pure sum (mirrors water/GerstnerWave.h EXACTLY).
		const int waveCount = static_cast<int>(std::min<size_t>(m_Waves.size(), kMaxShaderWaves));
		m_Shader->SetInt("u_WaveCount", waveCount);
		const float invCount = waveCount > 0 ? 1.0f / static_cast<float>(waveCount) : 0.0f;
		for (int i = 0; i < waveCount; ++i)
		{
			const GerstnerWave& w = m_Waves[i];
			const glm::vec2 d = glm::dot(w.Direction, w.Direction) > 1e-8f
				? glm::normalize(w.Direction) : glm::vec2(1.0f, 0.0f);
			const float k     = 2.0f * 3.14159265358979f / std::max(w.Wavelength, 1e-4f);
			const float omega = w.Speed > 0.0f ? k * w.Speed : std::sqrt(9.81f * k);
			const float q     = w.Steepness <= 0.0f || w.Amplitude <= 0.0f ? 0.0f
			                  : w.Steepness / (k * w.Amplitude) * invCount;

			char name[32];
			std::snprintf(name, sizeof(name), "u_WaveDirKA[%d]", i);
			m_Shader->SetFloat4(name, { d.x, d.y, k, w.Amplitude });
			std::snprintf(name, sizeof(name), "u_WaveQOP[%d]", i);
			m_Shader->SetFloat4(name, { q, omega, w.Phase, 0.0f });
		}

		// Optics.
		m_Shader->SetFloat3("u_ShallowColor", m_Spec.ShallowColor);
		m_Shader->SetFloat3("u_DeepColor", m_Spec.DeepColor);
		m_Shader->SetFloat("u_DepthFadeDistance", m_Spec.DepthFadeDistance);
		m_Shader->SetFloat("u_FoamDepth", m_Spec.FoamDepth);
		m_Shader->SetFloat("u_RefractionStrength", m_Spec.RefractionStrength);
		m_Shader->SetFloat("u_ReflectionStrength", m_Spec.ReflectionStrength);
		m_Shader->SetFloat("u_DetailTiling", m_Spec.DetailTiling);
		m_Shader->SetFloat("u_DetailSpeed", m_Spec.DetailSpeed);
		m_Shader->SetFloat("u_DetailStrength", m_Spec.DetailStrength);
		m_Shader->SetFloat("u_SpecularPower", m_Spec.SpecularPower);

		// v2 optics (F6). All default 0 -> off (the shipped S9.1 look is byte-identical).
		m_Shader->SetFloat("u_CausticStrength", m_Spec.CausticStrength);
		m_Shader->SetFloat("u_CausticScale", m_Spec.CausticScale);
		m_Shader->SetFloat("u_SparkleStrength", m_Spec.SparkleStrength);
		m_Shader->SetFloat("u_WhitecapStrength", m_Spec.WhitecapStrength);

		// Screen-space resources (low units; reserved 8+ stay for the scene set).
		RenderCommand::BindTextureSlot(0, m_RefractionFbo->GetColorAttachmentRendererID(0));
		m_Shader->SetInt("u_Refraction", 0);
		RenderCommand::BindTextureSlot(1, sceneDepthID);
		m_Shader->SetInt("u_SceneDepth", 1);
		m_DetailA->Bind(2);
		m_Shader->SetInt("u_DetailA", 2);
		m_DetailB->Bind(3);
		m_Shader->SetInt("u_DetailB", 3);
		RenderCommand::BindTextureSlot(4, m_HasReflection ? m_ReflectionFbo->GetColorAttachmentRendererID(0) : 0);
		m_Shader->SetInt("u_Reflection", 4);
		m_Shader->SetFloat("u_HasReflection", m_HasReflection ? 1.0f : 0.0f);
		m_Shader->SetMat4("u_ReflectionViewProj", m_ReflectionViewProj);
		m_Shader->SetMat4("u_InvViewProj", glm::inverse(viewProjection));

		// v2 shore awareness (F6): bind the terrain's packed height texture (unit 6;
		// water owns 0..5, engine reserves 8+) so waves flatten + break in the
		// shallows. The sampler unit is assigned unconditionally (same rule as the
		// scene set); the gate uniform u_HasShoreTex enables the sampling. Requires
		// the terrain's GPU resources (built when it renders earlier this frame).
		m_Shader->SetInt("u_ShoreHeightTex", 6);
		const uint32_t shoreTexID = m_ShoreTerrain ? m_ShoreTerrain->GetHeightTextureID() : 0;
		if (shoreTexID != 0)
		{
			RenderCommand::BindTextureSlot(6, shoreTexID);
			const glm::vec2 minCorner = m_ShoreTerrain->GetWorldMinCorner();
			const float     worldSize = m_ShoreTerrain->GetWorldSize();
			const float     invSize   = worldSize > 0.0f ? 1.0f / worldSize : 0.0f;
			m_Shader->SetFloat4("u_ShoreRect", { minCorner.x, minCorner.y, invSize, invSize });
			m_Shader->SetFloat2("u_ShoreHeight", { m_ShoreTerrain->GetHeightScale(),
			                                       m_ShoreTerrain->GetBaseHeight() });
			m_Shader->SetFloat("u_ShoreDepthRange", m_Spec.ShoreDepthRange);
			m_Shader->SetFloat("u_HasShoreTex", 1.0f);
		}
		else
		{
			m_Shader->SetFloat("u_HasShoreTex", 0.0f);
		}

		m_Grid->GetVertexArray()->Bind();

		// Render BOTH faces: the surface must be visible from BELOW (the dive look) as
		// well as above — the fragment shader branches on the view side (Snell's window
		// + TIR from below). None is the engine default, so nothing to restore.
		RenderCommand::SetCullMode(RendererAPI::CullMode::None);

		// Depth WRITES stay off: the shader samples the bound target's depth
		// attachment (depth-fade/foam), and writing depth while sampling it is a
		// rendering-feedback loop per the GL spec. Depth TEST stays on so terrain
		// above the waterline still occludes the surface. Restored after.
		RenderCommand::SetDepthWrite(false);
		RenderCommand::DrawIndexed(m_Grid->GetVertexArray(), m_Grid->GetIndexCount());
		RenderCommand::SetDepthWrite(true);

		// The reflection capture is consumed; a new one is expected next frame
		// (stale captures would smear when the camera moves).
		m_HasReflection = false;
	}
}
