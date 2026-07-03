#pragma once

// ParticleSystem.h
// Last Modified: 7/3/2026

/**
 * ============================================================================
 * COSMIC ENGINE — ParticleEmitter + RibbonEmitter (S10.1 / S10.2)
 * ============================================================================
 *
 * GPU particle system on the S4.7 compute path: a std430 SSBO pool
 * (binding: Bindings::ParticlesSsbo) updated by ParticleUpdate.glsl and drawn
 * attribute-less as camera-facing billboards (6 vertices per particle from
 * gl_VertexID — no vertex buffer). Emission is a RING BUFFER: the CPU
 * accumulates a fractional spawn budget, hands the compute pass a
 * [start, count) spawn window, and slots respawn in place — no free lists, no
 * readbacks, dead slots render as zero-area triangles.
 *
 *   - Spawn shapes: Point / Sphere / Cone / Box  (cone axis = emitter +Y)
 *   - Over-lifetime: size + color start->end (linear tier; curves ride S12)
 *   - Forces: gravity, drag, wind
 *   - World or Local simulation space
 *   - Soft particles (depth fade vs the scene depth) + texture flipbooks
 *   - Alpha or Additive blending per emitter (SetBlendMode verb; restored)
 *   - CPU fallback (GpuSimulation = false): identical ring-buffer semantics
 *     stepped on the CPU and uploaded — for tiny emitters or GL-less tests
 *     (the pure step lives in ParticleSystem.cpp and is unit-testable).
 *
 * DOCUMENTED TIER DEVIATIONS (doc 05 S10.1): draw is a fixed MaxParticles
 * quads (an indirect-draw verb + GPU compaction is the follow-up); no
 * intra-emitter depth sort (rides the S12.2 transparent queue).
 *
 * WIRING (app-side, per frame):
 *   emitter->SetTransform(placement);
 *   emitter->Update(dt, timeSeconds);                 // compute or CPU step
 *   ... opaque scene renders ...
 *   emitter->Render(view, sceneDepthID, invViewProj); // inside the 3D pass
 *
 * RibbonEmitter (S10.2) is the CPU trail sibling: feed it points, it builds a
 * camera-facing, age-faded triangle strip each frame (rocket exhaust, tip
 * vortices, tire tracks).
 * ============================================================================
 */

#include "core/Core.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace Cosmic
{
	class Shader;
	class StorageBuffer;
	class Texture2D;
	class VertexArray;
	class VertexBuffer;

	enum class EmitterShape    { Point = 0, Sphere, Cone, Box };
	enum class ParticleBlend   { Alpha = 0, Additive };
	enum class ParticleSpace   { World = 0, Local };

	struct ParticleEmitterSpec
	{
		uint32_t MaxParticles = 4096;
		float    SpawnRate    = 200.0f;     // particles / second (Burst() adds on top)

		// --- Spawn shape (cone axis / box frame = the emitter transform's +Y) ---
		EmitterShape Shape        = EmitterShape::Point;
		float        ShapeRadius  = 0.25f;  // Sphere / Cone base
		float        ConeAngleDeg = 22.0f;
		glm::vec3    BoxExtents{ 1.0f };    // full extents

		float SpeedMin = 1.0f, SpeedMax = 2.0f;
		float LifeMin  = 1.0f, LifeMax  = 2.0f;

		// --- Forces ---
		glm::vec3 Gravity{ 0.0f, -9.81f, 0.0f };
		float     Drag = 0.1f;              // 1/s velocity damping
		glm::vec3 Wind{ 0.0f };             // constant acceleration

		// --- Over-lifetime (linear start -> end) ---
		float     SizeStart = 0.15f, SizeEnd = 0.05f;
		glm::vec4 ColorStart{ 1.0f }, ColorEnd{ 1.0f, 1.0f, 1.0f, 0.0f };

		ParticleBlend Blend = ParticleBlend::Alpha;
		ParticleSpace Space = ParticleSpace::World;

		// --- Texture / flipbook. Null texture -> a procedural soft puff sheet. ---
		Ref<Texture2D> Texture;
		uint32_t FlipbookTilesX = 1, FlipbookTilesY = 1;
		float    FlipbookFps    = 0.0f;     // 0 = static random tile per particle
		bool     FlipbookBlend  = false;    // crossfade consecutive frames

		float SoftFadeDistance = 0.4f;      // meters; 0 disables soft particles

		bool GpuSimulation = true;          // false = CPU fallback path
	};

	/** CPU mirror of the std430 particle (3 vec4 = 48 bytes; see ParticleUpdate.glsl). */
	struct GpuParticle
	{
		glm::vec4 PosAge{ 0.0f, 0.0f, 0.0f, 1e9f };   // xyz pos, w age (>= life = dead)
		glm::vec4 VelLife{ 0.0f, 0.0f, 0.0f, 0.0f };  // xyz velocity, w lifetime
		glm::vec4 SeedSize{ 0.0f };                   // x rand01, y size jitter, zw reserved
	};
	static_assert(sizeof(GpuParticle) == 48, "GpuParticle must match the std430 layout (48 bytes).");

	class COSMIC_API ParticleEmitter
	{
	public:
		static Ref<ParticleEmitter> Create(const ParticleEmitterSpec& spec);

		~ParticleEmitter();

		// GPU resource owner — copying would alias the pool/ring state.
		ParticleEmitter(const ParticleEmitter&)            = delete;
		ParticleEmitter& operator=(const ParticleEmitter&) = delete;

		/** @brief Emitter placement: spawn frame (World space) or render/model
		 *  transform (Local space). Cone/Box orient to its +Y. */
		void SetTransform(const glm::mat4& transform) { m_Transform = transform; }

		/** @brief Queue `count` extra spawns for the next Update. */
		void Burst(uint32_t count) { m_BurstBudget += count; }

		/** @brief Advance the pool one frame (compute dispatch, or the CPU step +
		 *  upload on the fallback path). Needs a live GL context. */
		void Update(float dt, float timeSeconds);

		/**
		 * @brief Draw the pool as billboards into the bound target, inside a
		 * Renderer3D scene. Depth WRITES are disabled during the draw and the
		 * blend mode is switched per the spec — both restored before returning.
		 * @param view         the camera view matrix (billboard right/up axes).
		 * @param sceneDepthID depth attachment for soft particles (0 = off).
		 * @param invViewProj  inverse view-projection (soft-particle reconstruct).
		 */
		void Render(const glm::mat4& view, uint32_t sceneDepthID, const glm::mat4& invViewProj);

		/**
		 * @brief Draw the pool as a heat-haze DISTORTION field (S10.5) — call
		 * between PostProcessStack::BeginDistortion/EndDistortion. Each billboard
		 * writes a radial screen-space offset scaled by its coverage instead of
		 * color; accumulation is forced additive (restored). Same soft-depth
		 * fade inputs as Render.
		 */
		void RenderDistortion(const glm::mat4& view, uint32_t sceneDepthID, const glm::mat4& invViewProj);

		const ParticleEmitterSpec& GetSpecification() const { return m_Spec; }
		bool     IsGpuPath() const      { return m_Spec.GpuSimulation; }
		uint32_t GetMaxParticles() const { return m_Spec.MaxParticles; }

		/**
		 * @brief The pure CPU simulation step (the fallback path's core, exposed
		 * for headless unit tests). Applies the ring-buffer spawn window
		 * [spawnStart, spawnStart + spawnCount) then integrates every live slot.
		 * Deterministic for a given (particles, spec, seed) input.
		 */
		static void StepCpu(std::vector<GpuParticle>& particles, const ParticleEmitterSpec& spec,
		                    const glm::mat4& transform, float dt,
		                    uint32_t spawnStart, uint32_t spawnCount, uint32_t frameSeed);

	public:
		// Public only so Ref construction works inside Create().
		ParticleEmitter() = default;

	private:
		bool EnsureGpuResources();
		void RenderInternal(const glm::mat4& view, uint32_t sceneDepthID,
		                    const glm::mat4& invViewProj, bool distortionMode);

		ParticleEmitterSpec m_Spec;
		glm::mat4 m_Transform{ 1.0f };

		// Ring-buffer emission state.
		float    m_SpawnAccum  = 0.0f;
		uint32_t m_BurstBudget = 0;
		uint32_t m_Head        = 0;
		uint32_t m_FrameSeed   = 1;

		// GPU path.
		bool               m_GpuReady = false;
		Ref<Shader>        m_UpdateShader;    // ParticleUpdate.glsl (compute)
		Ref<Shader>        m_DrawShader;      // ParticleBillboards.glsl
		Ref<StorageBuffer> m_Pool;            // Bindings::ParticlesSsbo
		Ref<Texture2D>     m_Texture;         // spec texture or procedural puff

		// CPU fallback path.
		std::vector<GpuParticle> m_CpuPool;
	};

	/////////////////////////////////////////////////////////////////////////////////

	struct RibbonSpec
	{
		uint32_t  MaxPoints     = 96;
		float     Width         = 0.12f;     // full width, meters
		float     PointLifetime = 1.2f;      // seconds until a point expires
		float     MinDistance   = 0.05f;     // meters between recorded points
		glm::vec4 ColorHead{ 1.0f };         // newest end
		glm::vec4 ColorTail{ 1.0f, 1.0f, 1.0f, 0.0f };
		bool      Additive = false;
	};

	class COSMIC_API RibbonEmitter
	{
	public:
		static Ref<RibbonEmitter> Create(const RibbonSpec& spec);

		~RibbonEmitter();

		RibbonEmitter(const RibbonEmitter&)            = delete;
		RibbonEmitter& operator=(const RibbonEmitter&) = delete;

		/** @brief Record the trailed object's position (skipped under MinDistance). */
		void AddPoint(const glm::vec3& position, float timeSeconds);

		/** @brief Expire points older than PointLifetime. */
		void Update(float timeSeconds);

		/** @brief Build + draw the camera-facing strip (inside a Renderer3D scene).
		 *  Depth writes off during the draw; blend restored (S10.2). */
		void Render(const glm::mat4& view, float timeSeconds);

		void   Clear() { m_Points.clear(); }
		size_t GetPointCount() const { return m_Points.size(); }

	public:
		RibbonEmitter() = default;

	private:
		bool EnsureGpuResources();

		struct TrailPoint { glm::vec3 Position; float BornTime; };

		RibbonSpec              m_Spec;
		std::vector<TrailPoint> m_Points;    // oldest first

		bool              m_GpuReady = false;
		Ref<Shader>       m_Shader;          // Ribbon.glsl
		Ref<VertexArray>  m_VertexArray;
		Ref<VertexBuffer> m_VertexBuffer;    // dynamic, rebuilt per frame
	};
}
