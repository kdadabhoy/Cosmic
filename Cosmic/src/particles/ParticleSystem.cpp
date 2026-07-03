// ParticleSystem.cpp — S10.1 GPU particles + S10.2 ribbons. See ParticleSystem.h.

#include "particles/ParticleSystem.h"

#include "renderer/RenderCommand.h"
#include "renderer/BindingPoints.h"
#include "graphics/Shader.h"
#include "graphics/StorageBuffer.h"
#include "graphics/Texture.h"
#include "graphics/Buffer.h"
#include "graphics/VertexArray.h"
#include "math/Noise.h"
#include "core/Log.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Cosmic
{
	namespace
	{
		constexpr uint32_t kComputeGroupSize = 256;   // mirrors ParticleUpdate.glsl local_size_x
		constexpr float    kPi = 3.14159265358979f;

		// PCG hash — the SAME generator ParticleUpdate.glsl uses, so the CPU
		// fallback spawns statistically identical particles to the GPU path.
		uint32_t PcgHash(uint32_t v)
		{
			v = v * 747796405u + 2891336453u;
			v = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
			return (v >> 22u) ^ v;
		}

		struct Rng
		{
			uint32_t State;
			float Next01()   // [0, 1)
			{
				State = PcgHash(State);
				return static_cast<float>(State) * (1.0f / 4294967296.0f);
			}
		};

		glm::vec3 RandomUnitSphere(Rng& rng)
		{
			// Marsaglia-free spherical draw: z uniform, azimuth uniform.
			const float z   = rng.Next01() * 2.0f - 1.0f;
			const float phi = rng.Next01() * 2.0f * kPi;
			const float r   = std::sqrt(std::max(1.0f - z * z, 0.0f));
			return { r * std::cos(phi), z, r * std::sin(phi) };
		}
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Pure CPU step (unit-testable; the GPU compute shader mirrors this exactly)
	/////////////////////////////////////////////////////////////////////////////////

	void ParticleEmitter::StepCpu(std::vector<GpuParticle>& particles, const ParticleEmitterSpec& spec,
	                              const glm::mat4& transform, float dt,
	                              uint32_t spawnStart, uint32_t spawnCount, uint32_t frameSeed)
	{
		const uint32_t n = static_cast<uint32_t>(particles.size());
		if (n == 0)
			return;

		const glm::mat3 rot(transform);
		const glm::vec3 origin(transform[3]);

		for (uint32_t slot = 0; slot < n; ++slot)
		{
			const uint32_t rel = (slot + n - (spawnStart % n)) % n;
			GpuParticle& p = particles[slot];

			if (rel < spawnCount)
			{
				// --- Respawn this slot ---
				Rng rng{ PcgHash(frameSeed * 9781u + slot * 6271u + 1u) };

				glm::vec3 localPos{ 0.0f };
				glm::vec3 localDir{ 0.0f, 1.0f, 0.0f };
				switch (spec.Shape)
				{
				case EmitterShape::Point:
					localDir = RandomUnitSphere(rng);
					break;
				case EmitterShape::Sphere:
				{
					localDir = RandomUnitSphere(rng);
					const float r = spec.ShapeRadius * std::cbrt(rng.Next01());
					localPos = localDir * r;
					break;
				}
				case EmitterShape::Cone:
				{
					const float cosMax = std::cos(glm::radians(spec.ConeAngleDeg));
					const float cosT   = 1.0f + (cosMax - 1.0f) * rng.Next01();
					const float sinT   = std::sqrt(std::max(1.0f - cosT * cosT, 0.0f));
					const float phi    = rng.Next01() * 2.0f * kPi;
					localDir = { sinT * std::cos(phi), cosT, sinT * std::sin(phi) };
					const float discR   = spec.ShapeRadius * std::sqrt(rng.Next01());
					const float discPhi = rng.Next01() * 2.0f * kPi;
					localPos = { discR * std::cos(discPhi), 0.0f, discR * std::sin(discPhi) };
					break;
				}
				case EmitterShape::Box:
					localPos = { (rng.Next01() - 0.5f) * spec.BoxExtents.x,
					             (rng.Next01() - 0.5f) * spec.BoxExtents.y,
					             (rng.Next01() - 0.5f) * spec.BoxExtents.z };
					localDir = { 0.0f, 1.0f, 0.0f };
					break;
				}

				glm::vec3 pos = localPos;
				glm::vec3 dir = localDir;
				if (spec.Space == ParticleSpace::World)
				{
					pos = origin + rot * localPos;
					dir = glm::normalize(rot * localDir);
				}

				const float speed = spec.SpeedMin + (spec.SpeedMax - spec.SpeedMin) * rng.Next01();
				const float life  = std::max(spec.LifeMin + (spec.LifeMax - spec.LifeMin) * rng.Next01(), 1e-3f);

				p.PosAge   = glm::vec4(pos, 0.0f);
				p.VelLife  = glm::vec4(dir * speed, life);
				p.SeedSize = glm::vec4(rng.Next01(), 0.75f + 0.5f * rng.Next01(), 0.0f, 0.0f);
			}
			else if (p.PosAge.w < p.VelLife.w)
			{
				// --- Integrate a live particle ---
				glm::vec3 vel(p.VelLife);
				vel += (spec.Gravity + spec.Wind) * dt;
				vel *= std::max(1.0f - spec.Drag * dt, 0.0f);

				p.PosAge   = glm::vec4(glm::vec3(p.PosAge) + vel * dt, p.PosAge.w + dt);
				p.VelLife  = glm::vec4(vel, p.VelLife.w);
			}
		}
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Emitter lifecycle
	/////////////////////////////////////////////////////////////////////////////////

	Ref<ParticleEmitter> ParticleEmitter::Create(const ParticleEmitterSpec& spec)
	{
		if (spec.MaxParticles == 0)
		{
			CS_CORE_ERROR("ParticleEmitter: MaxParticles must be > 0.");
			return nullptr;
		}

		auto emitter = std::make_shared<ParticleEmitter>();
		emitter->m_Spec = spec;
		if (!spec.GpuSimulation)
			emitter->m_CpuPool.assign(spec.MaxParticles, GpuParticle{});
		return emitter;
	}

	ParticleEmitter::~ParticleEmitter() = default;

	bool ParticleEmitter::EnsureGpuResources()
	{
		if (m_GpuReady)
			return true;

		m_DrawShader = Shader::Create("assets/shaders/ParticleBillboards.glsl");
		if (m_Spec.GpuSimulation)
			m_UpdateShader = Shader::Create("assets/shaders/ParticleUpdate.glsl");
		if (!m_DrawShader || (m_Spec.GpuSimulation && !m_UpdateShader))
		{
			CS_CORE_ERROR("ParticleEmitter: shader load failed — emitter disabled.");
			return false;
		}

		// The pool SSBO. Seeded fully dead (age >= life) so nothing flashes on
		// the first frame before the spawn window walks the ring.
		std::vector<GpuParticle> dead(m_Spec.MaxParticles, GpuParticle{});
		m_Pool = StorageBuffer::Create(m_Spec.MaxParticles * sizeof(GpuParticle), Bindings::ParticlesSsbo);
		if (!m_Pool)
			return false;
		m_Pool->SetData(dead.data(), static_cast<uint32_t>(dead.size() * sizeof(GpuParticle)));

		// Texture: caller-supplied, or a procedural soft-puff flipbook sheet
		// (radial falloff x fBm per tile — S10.4's "flipbook-billboard first" tier).
		if (m_Spec.Texture)
		{
			m_Texture = m_Spec.Texture;
		}
		else
		{
			const uint32_t tilesX = std::max(m_Spec.FlipbookTilesX, 1u);
			const uint32_t tilesY = std::max(m_Spec.FlipbookTilesY, 1u);
			const uint32_t texSize = 256;
			const Noise noise(0xC0FFEEu);

			std::vector<uint8_t> px(static_cast<size_t>(texSize) * texSize * 4, 255);
			for (uint32_t y = 0; y < texSize; ++y)
				for (uint32_t x = 0; x < texSize; ++x)
				{
					const uint32_t tx = x * tilesX / texSize, ty = y * tilesY / texSize;
					const float tileW = static_cast<float>(texSize) / tilesX;
					const float tileH = static_cast<float>(texSize) / tilesY;
					const float u = (x - tx * tileW) / tileW * 2.0f - 1.0f;
					const float v = (y - ty * tileH) / tileH * 2.0f - 1.0f;

					const float r      = std::sqrt(u * u + v * v);
					const float radial = std::clamp(1.0f - r, 0.0f, 1.0f);
					const float n      = 0.5f + 0.5f * noise.Fbm2D(
						u * 2.5f + static_cast<float>(tx) * 17.0f,
						v * 2.5f + static_cast<float>(ty) * 29.0f, 4, 2.0f, 0.5f);
					const float a = radial * radial * (0.55f + 0.45f * n);

					px[(static_cast<size_t>(y) * texSize + x) * 4 + 3] =
						static_cast<uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f);
				}

			m_Texture = Texture2D::Create(texSize, texSize);
			if (m_Texture)
			{
				m_Texture->SetData(px.data(), static_cast<uint32_t>(px.size()));
				m_Texture->SetSampling(TextureFilter::Linear, TextureWrap::ClampToEdge);
			}
		}

		m_GpuReady = m_Pool && m_Texture != nullptr;
		return m_GpuReady;
	}

	void ParticleEmitter::Update(float dt, float timeSeconds)
	{
		(void)timeSeconds;
		if (!EnsureGpuResources() || dt <= 0.0f)
			return;

		// Ring-buffer spawn budget for this frame.
		m_SpawnAccum += m_Spec.SpawnRate * dt;
		uint32_t spawnCount = static_cast<uint32_t>(m_SpawnAccum) + m_BurstBudget;
		m_SpawnAccum -= static_cast<float>(static_cast<uint32_t>(m_SpawnAccum));
		m_BurstBudget = 0;
		spawnCount = std::min(spawnCount, m_Spec.MaxParticles);

		const uint32_t spawnStart = m_Head;
		m_Head = (m_Head + spawnCount) % m_Spec.MaxParticles;
		m_FrameSeed = PcgHash(m_FrameSeed + 0x9E3779B9u);

		if (m_Spec.GpuSimulation)
		{
			m_UpdateShader->Bind();
			m_UpdateShader->SetFloat("u_Dt", dt);
			m_UpdateShader->SetInt("u_MaxParticles", static_cast<int>(m_Spec.MaxParticles));
			m_UpdateShader->SetInt("u_SpawnStart", static_cast<int>(spawnStart));
			m_UpdateShader->SetInt("u_SpawnCount", static_cast<int>(spawnCount));
			m_UpdateShader->SetInt("u_FrameSeed", static_cast<int>(m_FrameSeed));
			m_UpdateShader->SetInt("u_Shape", static_cast<int>(m_Spec.Shape));
			m_UpdateShader->SetFloat4("u_ShapeParams",
				{ m_Spec.ShapeRadius, glm::radians(m_Spec.ConeAngleDeg), 0.0f, 0.0f });
			m_UpdateShader->SetFloat3("u_BoxExtents", m_Spec.BoxExtents);
			m_UpdateShader->SetFloat2("u_SpeedRange", { m_Spec.SpeedMin, m_Spec.SpeedMax });
			m_UpdateShader->SetFloat2("u_LifeRange", { m_Spec.LifeMin, m_Spec.LifeMax });
			m_UpdateShader->SetFloat3("u_Gravity", m_Spec.Gravity);
			m_UpdateShader->SetFloat3("u_Wind", m_Spec.Wind);
			m_UpdateShader->SetFloat("u_Drag", m_Spec.Drag);
			m_UpdateShader->SetInt("u_WorldSpace", m_Spec.Space == ParticleSpace::World ? 1 : 0);
			m_UpdateShader->SetMat4("u_EmitterTransform", m_Transform);

			m_Pool->Bind();
			RenderCommand::DispatchCompute((m_Spec.MaxParticles + kComputeGroupSize - 1) / kComputeGroupSize, 1, 1);
			// The draw reads the pool from the vertex stage — make writes visible.
			RenderCommand::GpuMemoryBarrier(RenderCommand::GpuBarrier::ShaderStorage |
			                                RenderCommand::GpuBarrier::VertexAttribArray);
		}
		else
		{
			StepCpu(m_CpuPool, m_Spec, m_Transform, dt, spawnStart, spawnCount, m_FrameSeed);
			m_Pool->SetData(m_CpuPool.data(),
			                static_cast<uint32_t>(m_CpuPool.size() * sizeof(GpuParticle)));
		}
	}

	void ParticleEmitter::Render(const glm::mat4& view, uint32_t sceneDepthID, const glm::mat4& invViewProj)
	{
		RenderInternal(view, sceneDepthID, invViewProj, false);
	}

	void ParticleEmitter::RenderDistortion(const glm::mat4& view, uint32_t sceneDepthID, const glm::mat4& invViewProj)
	{
		RenderInternal(view, sceneDepthID, invViewProj, true);
	}

	void ParticleEmitter::RenderInternal(const glm::mat4& view, uint32_t sceneDepthID,
	                                     const glm::mat4& invViewProj, bool distortionMode)
	{
		if (!EnsureGpuResources())
			return;

		m_DrawShader->Bind();

		// Billboard axes = the camera view rows (world-space right/up).
		const glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
		const glm::vec3 camUp   (view[0][1], view[1][1], view[2][1]);
		m_DrawShader->SetFloat3("u_CamRight", camRight);
		m_DrawShader->SetFloat3("u_CamUp", camUp);

		m_DrawShader->SetFloat2("u_SizeRange", { m_Spec.SizeStart, m_Spec.SizeEnd });
		m_DrawShader->SetFloat4("u_ColorStart", m_Spec.ColorStart);
		m_DrawShader->SetFloat4("u_ColorEnd", m_Spec.ColorEnd);
		m_DrawShader->SetInt("u_WorldSpace", m_Spec.Space == ParticleSpace::World ? 1 : 0);
		m_DrawShader->SetMat4("u_EmitterTransform", m_Transform);

		const int tilesX = static_cast<int>(std::max(m_Spec.FlipbookTilesX, 1u));
		const int tilesY = static_cast<int>(std::max(m_Spec.FlipbookTilesY, 1u));
		m_DrawShader->SetFloat2("u_FlipbookTiles", { static_cast<float>(tilesX), static_cast<float>(tilesY) });
		m_DrawShader->SetFloat("u_FlipbookFps", m_Spec.FlipbookFps);
		m_DrawShader->SetFloat("u_FlipbookBlend", m_Spec.FlipbookBlend ? 1.0f : 0.0f);

		m_Texture->Bind(0);
		m_DrawShader->SetInt("u_Texture", 0);

		const bool soft = sceneDepthID != 0 && m_Spec.SoftFadeDistance > 0.0f;
		m_DrawShader->SetFloat("u_SoftFade", soft ? m_Spec.SoftFadeDistance : 0.0f);
		m_DrawShader->SetInt("u_SceneDepth", 1);
		if (soft)
		{
			RenderCommand::BindTextureSlot(1, sceneDepthID);
			m_DrawShader->SetMat4("u_InvViewProj", invViewProj);
		}

		m_DrawShader->SetFloat("u_DistortionMode", distortionMode ? 1.0f : 0.0f);

		m_Pool->Bind();

		// Transparent pass state: depth-tested but not depth-written, per-emitter
		// blend (distortion fields always accumulate additively). BOTH restored
		// before returning (engine restore contract). The distortion target has
		// no depth attachment, so occlusion there rides the soft-depth fade.
		const bool additive = distortionMode || m_Spec.Blend == ParticleBlend::Additive;
		RenderCommand::SetDepthWrite(false);
		RenderCommand::SetBlendMode(additive ? RenderCommand::BlendMode::Additive
		                                     : RenderCommand::BlendMode::Alpha);

		RenderCommand::DrawArrays(RenderCommand::PrimitiveTopology::Triangles, 0, m_Spec.MaxParticles * 6);

		RenderCommand::SetBlendMode(RenderCommand::BlendMode::Alpha);
		RenderCommand::SetDepthWrite(true);
	}

	/////////////////////////////////////////////////////////////////////////////////
	// RibbonEmitter (S10.2)
	/////////////////////////////////////////////////////////////////////////////////

	namespace
	{
		struct RibbonVertex
		{
			glm::vec3 Position;
			glm::vec4 Color;
		};
	}

	Ref<RibbonEmitter> RibbonEmitter::Create(const RibbonSpec& spec)
	{
		if (spec.MaxPoints < 2)
		{
			CS_CORE_ERROR("RibbonEmitter: MaxPoints must be >= 2.");
			return nullptr;
		}
		auto ribbon = std::make_shared<RibbonEmitter>();
		ribbon->m_Spec = spec;
		ribbon->m_Points.reserve(spec.MaxPoints);
		return ribbon;
	}

	RibbonEmitter::~RibbonEmitter() = default;

	bool RibbonEmitter::EnsureGpuResources()
	{
		if (m_GpuReady)
			return true;

		m_Shader = Shader::Create("assets/shaders/Ribbon.glsl");
		if (!m_Shader)
		{
			CS_CORE_ERROR("RibbonEmitter: Ribbon.glsl failed to load — ribbons disabled.");
			return false;
		}

		m_VertexArray  = VertexArray::Create();
		m_VertexBuffer = VertexBuffer::Create(m_Spec.MaxPoints * 2 * sizeof(RibbonVertex));
		m_VertexBuffer->SetLayout({
			{ ShaderDataType::Float3, "a_Position" },
			{ ShaderDataType::Float4, "a_Color"    }
			});
		m_VertexArray->AddVertexBuffer(m_VertexBuffer);

		// Static max-size index list: two triangles per segment over the 2-wide strip.
		std::vector<uint32_t> indices;
		indices.reserve((m_Spec.MaxPoints - 1) * 6);
		for (uint32_t s = 0; s + 1 < m_Spec.MaxPoints; ++s)
		{
			const uint32_t a = s * 2;
			indices.insert(indices.end(), { a, a + 2, a + 1,  a + 1, a + 2, a + 3 });
		}
		m_VertexArray->SetIndexBuffer(IndexBuffer::Create(indices.data(),
		                              static_cast<uint32_t>(indices.size())));

		m_GpuReady = true;
		return true;
	}

	void RibbonEmitter::AddPoint(const glm::vec3& position, float timeSeconds)
	{
		if (!m_Points.empty() &&
		    glm::distance(m_Points.back().Position, position) < m_Spec.MinDistance)
			return;

		if (m_Points.size() >= m_Spec.MaxPoints)
			m_Points.erase(m_Points.begin());
		m_Points.push_back({ position, timeSeconds });
	}

	void RibbonEmitter::Update(float timeSeconds)
	{
		const float cutoff = timeSeconds - m_Spec.PointLifetime;
		while (!m_Points.empty() && m_Points.front().BornTime < cutoff)
			m_Points.erase(m_Points.begin());
	}

	void RibbonEmitter::Render(const glm::mat4& view, float timeSeconds)
	{
		if (m_Points.size() < 2 || !EnsureGpuResources())
			return;

		// Camera-facing strip: each point extrudes +/- half-width along
		// normalize(cross(trailDir, viewDir)), colored head->tail with age fade.
		const glm::vec3 viewDir(-view[0][2], -view[1][2], -view[2][2]);

		std::vector<RibbonVertex> verts;
		verts.reserve(m_Points.size() * 2);
		const size_t count = m_Points.size();
		for (size_t i = 0; i < count; ++i)
		{
			const glm::vec3& p = m_Points[i].Position;
			const glm::vec3 dir = (i + 1 < count) ? m_Points[i + 1].Position - p
			                                      : p - m_Points[i - 1].Position;
			glm::vec3 side = glm::cross(dir, viewDir);
			const float len = glm::length(side);
			side = len > 1e-6f ? side / len : glm::vec3(0.0f, 1.0f, 0.0f);

			const float along = static_cast<float>(i) / static_cast<float>(count - 1); // 0 tail, 1 head
			const float age   = std::clamp((timeSeconds - m_Points[i].BornTime) / m_Spec.PointLifetime,
			                               0.0f, 1.0f);
			glm::vec4 color = glm::mix(m_Spec.ColorTail, m_Spec.ColorHead, along);
			color.a *= 1.0f - age;

			const glm::vec3 offset = side * (m_Spec.Width * 0.5f);
			verts.push_back({ p - offset, color });
			verts.push_back({ p + offset, color });
		}

		m_VertexBuffer->SetData(verts.data(),
		                        static_cast<uint32_t>(verts.size() * sizeof(RibbonVertex)));

		m_Shader->Bind();
		m_VertexArray->Bind();

		RenderCommand::SetDepthWrite(false);
		if (m_Spec.Additive)
			RenderCommand::SetBlendMode(RenderCommand::BlendMode::Additive);

		const uint32_t indexCount = static_cast<uint32_t>((count - 1) * 6);
		RenderCommand::DrawIndexed(m_VertexArray, indexCount);

		if (m_Spec.Additive)
			RenderCommand::SetBlendMode(RenderCommand::BlendMode::Alpha);
		RenderCommand::SetDepthWrite(true);
	}
}
