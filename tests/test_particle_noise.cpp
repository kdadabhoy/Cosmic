// test_particle_noise.cpp — curl-noise turbulence (Phase 27 X3).
//
// Headless (no GL): exercises the CPU mirror ParticleEmitter::StepCpu and the
// exposed field ParticleEmitter::CurlNoise. The GPU compute shader
// (ParticleUpdate.glsl) is a line-by-line mirror built on the SAME shared
// PcgHash + magic constants, so these determinism/compat properties carry over
// to the GPU path (the on-GPU swirl is the recorded user acceptance).

#include "doctest.h"
#include "particles/ParticleSystem.h"

#include <glm/glm.hpp>
#include <cmath>
#include <vector>

using namespace Cosmic;

TEST_SUITE("Particle curl-noise turbulence (X3)")
{
	// A tiny CPU-path emitter spec; forces off so the only motion is the initial
	// velocity plus (optionally) the curl field — isolates the noise term.
	static ParticleEmitterSpec MakeSpec(bool noise, float strength)
	{
		ParticleEmitterSpec s;
		s.MaxParticles   = 64;
		s.Shape          = EmitterShape::Point;
		s.SpeedMin = s.SpeedMax = 1.0f;
		s.LifeMin  = s.LifeMax  = 100.0f;   // never die during the test
		s.Gravity  = { 0.0f, 0.0f, 0.0f };
		s.Wind     = { 0.0f, 0.0f, 0.0f };
		s.Drag     = 0.0f;
		s.NoiseEnabled   = noise;
		s.NoiseStrength  = strength;
		s.NoiseFrequency = 0.4f;
		s.NoiseOctaves   = 2;
		s.GpuSimulation  = false;
		return s;
	}

	// Spawn every slot on frame 0, then integrate `frames-1` more steps.
	static std::vector<GpuParticle> Run(const ParticleEmitterSpec& spec, int frames, float dt)
	{
		std::vector<GpuParticle> pool(spec.MaxParticles);
		const glm::mat4 xform(1.0f);
		ParticleEmitter::StepCpu(pool, spec, xform, dt, 0, spec.MaxParticles, 12345u);
		for (int f = 1; f < frames; ++f)
			ParticleEmitter::StepCpu(pool, spec, xform, dt, 0, 0, 12345u);
		return pool;
	}

	TEST_CASE("StepCpu with noise is bit-deterministic across runs")
	{
		const auto a = Run(MakeSpec(true, 5.0f), 30, 1.0f / 60.0f);
		const auto b = Run(MakeSpec(true, 5.0f), 30, 1.0f / 60.0f);
		REQUIRE(a.size() == b.size());
		for (size_t i = 0; i < a.size(); ++i)
		{
			CHECK(a[i].PosAge.x  == b[i].PosAge.x);
			CHECK(a[i].PosAge.y  == b[i].PosAge.y);
			CHECK(a[i].PosAge.z  == b[i].PosAge.z);
			CHECK(a[i].VelLife.x == b[i].VelLife.x);
			CHECK(a[i].VelLife.z == b[i].VelLife.z);
		}
	}

	TEST_CASE("Noise disabled ignores strength (compat: shipped path byte-identical)")
	{
		// With NoiseEnabled=false the integration must not read NoiseStrength — so
		// wildly different strengths produce bit-identical pools (the `if` is gated).
		const auto off0  = Run(MakeSpec(false, 0.0f),  30, 1.0f / 60.0f);
		const auto off99 = Run(MakeSpec(false, 99.0f), 30, 1.0f / 60.0f);
		for (size_t i = 0; i < off0.size(); ++i)
		{
			CHECK(off0[i].PosAge.x == off99[i].PosAge.x);
			CHECK(off0[i].PosAge.y == off99[i].PosAge.y);
			CHECK(off0[i].PosAge.z == off99[i].PosAge.z);
		}
	}

	TEST_CASE("Noise enabled perturbs trajectories vs disabled")
	{
		const auto off = Run(MakeSpec(false, 5.0f), 30, 1.0f / 60.0f);
		const auto on  = Run(MakeSpec(true,  5.0f), 30, 1.0f / 60.0f);
		bool differs = false;
		for (size_t i = 0; i < on.size() && !differs; ++i)
			if (std::fabs(on[i].PosAge.x - off[i].PosAge.x) > 1e-4f ||
			    std::fabs(on[i].PosAge.y - off[i].PosAge.y) > 1e-4f ||
			    std::fabs(on[i].PosAge.z - off[i].PosAge.z) > 1e-4f)
				differs = true;
		CHECK(differs);
	}

	TEST_CASE("CurlNoise is deterministic, finite and spatially varying")
	{
		const glm::vec3 a  = ParticleEmitter::CurlNoise({ 1.2f, 3.4f, -0.7f }, 0.4f, 2);
		const glm::vec3 a2 = ParticleEmitter::CurlNoise({ 1.2f, 3.4f, -0.7f }, 0.4f, 2);
		CHECK(a.x == a2.x);
		CHECK(a.y == a2.y);
		CHECK(a.z == a2.z);
		CHECK(std::isfinite(a.x));
		CHECK(std::isfinite(a.y));
		CHECK(std::isfinite(a.z));

		const glm::vec3 c = ParticleEmitter::CurlNoise({ 5.9f, -2.1f, 4.3f }, 0.4f, 2);
		CHECK((a.x != c.x || a.y != c.y || a.z != c.z));
	}

	TEST_CASE("CurlNoise octave count is clamped to 1..4")
	{
		// Out-of-range octaves clamp to the same field as the boundary values, so
		// the CPU preview and the GPU sim (which uploads the same clamp) agree.
		const glm::vec3 p{ 2.0f, 1.0f, 0.5f };
		CHECK(ParticleEmitter::CurlNoise(p, 0.4f, 0).x == ParticleEmitter::CurlNoise(p, 0.4f, 1).x);
		CHECK(ParticleEmitter::CurlNoise(p, 0.4f, 9).y == ParticleEmitter::CurlNoise(p, 0.4f, 4).y);
	}
}

TEST_SUITE("Particle local-space bounds (X4)")
{
	// One live particle at the origin moving +X at 10 m/s, forces off, huge life —
	// so only the X4 bounds clamp can change its fate.
	static ParticleEmitterSpec BoundsSpec(const glm::vec3& ext, bool wrap)
	{
		ParticleEmitterSpec s;
		s.MaxParticles  = 1;
		s.Gravity  = { 0.0f, 0.0f, 0.0f };
		s.Wind     = { 0.0f, 0.0f, 0.0f };
		s.Drag     = 0.0f;
		s.BoundsExtents = ext;
		s.BoundsWrap    = wrap;
		s.GpuSimulation = false;
		return s;
	}

	static std::vector<GpuParticle> OneLive(int steps, const ParticleEmitterSpec& spec)
	{
		GpuParticle p;
		p.PosAge  = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);        // pos 0, age 0
		p.VelLife = glm::vec4(10.0f, 0.0f, 0.0f, 100.0f);     // +X, life 100
		std::vector<GpuParticle> pool{ p };
		for (int i = 0; i < steps; ++i)
			ParticleEmitter::StepCpu(pool, spec, glm::mat4(1.0f), 0.05f, 0, 0, 1u);
		return pool;
	}

	TEST_CASE("Bounds off (zero extents) is inert regardless of wrap flag")
	{
		const auto a = OneLive(6, BoundsSpec({ 0.0f, 0.0f, 0.0f }, false));
		const auto b = OneLive(6, BoundsSpec({ 0.0f, 0.0f, 0.0f }, true));
		CHECK(a[0].PosAge.x == b[0].PosAge.x);               // wrap flag ignored when off
		CHECK(a[0].PosAge.x == doctest::Approx(3.0f));       // 6 * 0.05 * 10 — free flight
		CHECK(a[0].PosAge.w < a[0].VelLife.w);               // still alive
	}

	TEST_CASE("Kill: a particle past the box dies in place")
	{
		// Bound X to +/-1. Steps of 0.5 m: reaches 1.5 on step 3 -> killed.
		const auto pool = OneLive(4, BoundsSpec({ 1.0f, 0.0f, 0.0f }, false));
		CHECK(pool[0].PosAge.w >= pool[0].VelLife.w);        // age >= life -> dead
	}

	TEST_CASE("Wrap: a particle stays inside the box and alive")
	{
		const auto pool = OneLive(8, BoundsSpec({ 1.0f, 0.0f, 0.0f }, true));
		CHECK(std::fabs(pool[0].PosAge.x) <= 1.0f + 1e-4f);  // wrapped back inside
		CHECK(pool[0].PosAge.w < pool[0].VelLife.w);         // never killed
	}
}
