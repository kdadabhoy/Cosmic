// test_phase10_world.cpp — Phase 10 world systems, CPU contracts (no GL):
//   S8.3  Terrain::SampleHeight / SampleNormal (renderer-matching triangle
//         interpolation, the "within 1 cm" acceptance line)
//   S9.2  Gerstner wave math + Water::SampleHeight (buoyancy queries)
//   S10.1 ParticleEmitter::StepCpu (ring-buffer spawn + integration —
//         the CPU-fallback core the GPU compute shader mirrors)

#include <cmath>
#include <vector>

#include "doctest.h"
#include "terrain/Terrain.h"
#include "water/Water.h"
#include "water/GerstnerWave.h"
#include "particles/ParticleSystem.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

TEST_SUITE("Terrain (S8)")
{
	static Cosmic::TerrainSpecification SmallSpec()
	{
		Cosmic::TerrainSpecification spec;
		spec.Resolution  = 65;         // 64 intervals = 32 * 2^1 — valid
		spec.WorldSize   = 64.0f;      // cell = 1 m
		spec.HeightScale = 10.0f;
		spec.BaseHeight  = 2.0f;
		spec.Seed        = 99;
		spec.Octaves     = 4;
		return spec;
	}

	TEST_CASE("factory validates the spec")
	{
		auto bad = SmallSpec();
		bad.Resolution = 100;                              // not 32*2^k + 1
		CHECK(Cosmic::Terrain::Create(bad) == nullptr);

		bad = SmallSpec();
		bad.WorldSize = 0.0f;
		CHECK(Cosmic::Terrain::Create(bad) == nullptr);

		CHECK(Cosmic::Terrain::Create(SmallSpec()) != nullptr);
	}

	TEST_CASE("SampleHeight is exact at grid vertices")
	{
		auto terrain = Cosmic::Terrain::Create(SmallSpec());
		REQUIRE(terrain);
		const auto& spec = terrain->GetSpecification();
		const float half = spec.WorldSize * 0.5f;

		for (uint32_t j = 0; j < spec.Resolution; j += 7)
			for (uint32_t i = 0; i < spec.Resolution; i += 5)
			{
				const float x = -half + static_cast<float>(i);   // cell = 1 m
				const float z = -half + static_cast<float>(j);
				const float expected = spec.BaseHeight + terrain->GetSample(i, j) * spec.HeightScale;
				CHECK(terrain->SampleHeight(x, z) == doctest::Approx(expected).epsilon(1e-5));
			}
	}

	TEST_CASE("SampleHeight matches the renderer's triangle split inside cells (<= 1 cm)")
	{
		auto terrain = Cosmic::Terrain::Create(SmallSpec());
		REQUIRE(terrain);
		const auto& spec = terrain->GetSpecification();
		const float half = spec.WorldSize * 0.5f;

		auto gridHeight = [&](uint32_t i, uint32_t j)
		{ return spec.BaseHeight + terrain->GetSample(i, j) * spec.HeightScale; };

		// Probe many interior points of many cells; recompute the diagonal-(+x+z)
		// triangle interpolation independently — the formula the patch mesh draws.
		int checked = 0;
		for (uint32_t j = 1; j < spec.Resolution - 2; j += 9)
			for (uint32_t i = 1; i < spec.Resolution - 2; i += 6)
				for (int s = 0; s < 5; ++s)
				{
					const float fx = 0.15f + 0.17f * s;
					const float fz = 0.83f - 0.15f * s;
					const float x = -half + i + fx;
					const float z = -half + j + fz;

					const float h00 = gridHeight(i, j),     h10 = gridHeight(i + 1, j);
					const float h01 = gridHeight(i, j + 1), h11 = gridHeight(i + 1, j + 1);
					const float expected = (fx >= fz)
						? h00 + (h10 - h00) * fx + (h11 - h10) * fz
						: h00 + (h11 - h01) * fx + (h01 - h00) * fz;

					CHECK(std::abs(terrain->SampleHeight(x, z) - expected) < 0.01f);   // 1 cm
					++checked;
				}
		CHECK(checked > 100);
	}

	TEST_CASE("outside the extent: BaseHeight, +Y normal, Contains false")
	{
		auto terrain = Cosmic::Terrain::Create(SmallSpec());
		REQUIRE(terrain);

		CHECK_FALSE(terrain->Contains(1000.0f, 0.0f));
		CHECK(terrain->SampleHeight(1000.0f, 0.0f) == doctest::Approx(2.0f));
		const glm::vec3 n = terrain->SampleNormal(1000.0f, 0.0f);
		CHECK(n.y == doctest::Approx(1.0f));
	}

	TEST_CASE("normals: unit length everywhere; +Y on flat terrain")
	{
		auto spec = SmallSpec();
		auto terrain = Cosmic::Terrain::Create(spec);
		REQUIRE(terrain);
		for (int k = 0; k < 50; ++k)
		{
			const float x = -30.0f + k * 1.17f;
			const float z =  28.0f - k * 1.03f;
			CHECK(glm::length(terrain->SampleNormal(x, z)) == doctest::Approx(1.0f).epsilon(1e-4));
		}

		spec.HeightScale = 0.0f;                         // flat: every normal is +Y
		auto flat = Cosmic::Terrain::Create(spec);
		REQUIRE(flat);
		CHECK(flat->SampleNormal(3.0f, -4.0f).y == doctest::Approx(1.0f));
		CHECK(flat->SampleHeight(3.0f, -4.0f) == doctest::Approx(spec.BaseHeight));
	}

	TEST_CASE("min/max heights bound every sample")
	{
		auto terrain = Cosmic::Terrain::Create(SmallSpec());
		REQUIRE(terrain);
		for (int k = 0; k < 200; ++k)
		{
			const float x = -31.5f + k * 0.31f;
			const float h = terrain->SampleHeight(x, x * 0.5f);
			CHECK(h >= terrain->GetMinHeight() - 1e-4f);
			CHECK(h <= terrain->GetMaxHeight() + 1e-4f);
		}
	}
}

TEST_SUITE("Gerstner water (S9)")
{
	TEST_CASE("empty wave set: flat surface, +Y normal")
	{
		std::vector<Cosmic::GerstnerWave> none;
		CHECK(Cosmic::SampleGerstnerHeight(none, 3.0f, -2.0f, 1.5f) == doctest::Approx(0.0f));
		CHECK(Cosmic::SampleGerstnerNormal(none, 3.0f, -2.0f, 1.5f).y == doctest::Approx(1.0f));
	}

	TEST_CASE("height bounded by total amplitude; normals unit length")
	{
		std::vector<Cosmic::GerstnerWave> waves = {
			{ {  1.0f, 0.3f }, 12.0f, 0.20f, 0.7f, 0.0f, 0.0f },
			{ { -0.4f, 1.0f },  5.0f, 0.10f, 0.5f, 0.0f, 1.0f },
			{ {  0.2f,-0.9f },  2.5f, 0.05f, 0.4f, 0.0f, 2.0f },
		};
		const float ampSum = 0.20f + 0.10f + 0.05f;

		for (int k = 0; k < 200; ++k)
		{
			const float x = k * 0.37f - 30.0f;
			const float z = k * 0.23f - 20.0f;
			const float t = k * 0.05f;

			const float h = Cosmic::SampleGerstnerHeight(waves, x, z, t);
			CHECK(std::abs(h) <= ampSum + 1e-3f);

			const glm::vec3 n = Cosmic::SampleGerstnerNormal(waves, x, z, t);
			CHECK(glm::length(n) == doctest::Approx(1.0f).epsilon(1e-4));
			CHECK(n.y > 0.0f);                        // never folds past vertical
		}
	}

	TEST_CASE("horizontal-displacement inversion converges (query lands on itself)")
	{
		std::vector<Cosmic::GerstnerWave> waves = {
			{ { 1.0f, 0.0f }, 10.0f, 0.25f, 0.8f, 0.0f, 0.0f },
			{ { 0.0f, 1.0f },  6.0f, 0.12f, 0.6f, 0.0f, 0.7f },
		};

		for (int k = 0; k < 60; ++k)
		{
			const float x = k * 0.61f - 18.0f;
			const float z = k * 0.43f - 12.0f;
			const float t = 2.0f + k * 0.11f;

			// Recover the grid point the same way SampleGerstnerHeight does (4
			// iterations, the header default), then verify grid + horizontal
			// offset re-lands on the queried (x, z) within 1 cm ABSOLUTE — the
			// buoyancy contract (doctest Approx epsilon is relative, useless for
			// coordinates that pass through zero).
			glm::vec2 grid{ x, z };
			for (int i = 0; i < 4; ++i)
			{
				const auto s = Cosmic::EvaluateGerstner(waves, grid.x, grid.y, t);
				grid = { x - s.Offset.x, z - s.Offset.z };
			}
			const auto s = Cosmic::EvaluateGerstner(waves, grid.x, grid.y, t);
			CHECK(std::abs(grid.x + s.Offset.x - x) < 0.01f);
			CHECK(std::abs(grid.y + s.Offset.z - z) < 0.01f);
		}
	}

	TEST_CASE("Water::Create resolves defaults and offsets SurfaceHeight")
	{
		Cosmic::WaterSpecification spec;
		spec.SurfaceHeight = 3.5f;
		auto water = Cosmic::Water::Create(spec);
		REQUIRE(water);
		CHECK(water->GetWaves().size() == 3);          // default swell applied

		float ampSum = 0.0f;
		for (const auto& w : water->GetWaves())
			ampSum += w.Amplitude;

		const float h = water->SampleHeight(1.0f, 2.0f, 0.8f);
		CHECK(std::abs(h - 3.5f) <= ampSum + 1e-3f);   // rides around the surface

		Cosmic::WaterSpecification bad;
		bad.Extent = { 0.0f, 10.0f };
		CHECK(Cosmic::Water::Create(bad) == nullptr);
	}
}

TEST_SUITE("Particles (S10.1 CPU step)")
{
	static Cosmic::ParticleEmitterSpec BasicSpec()
	{
		Cosmic::ParticleEmitterSpec spec;
		spec.MaxParticles = 8;
		spec.Shape        = Cosmic::EmitterShape::Point;
		spec.SpeedMin     = 1.0f;  spec.SpeedMax = 2.0f;
		spec.LifeMin      = 1.0f;  spec.LifeMax  = 2.0f;
		spec.Gravity      = { 0.0f, -10.0f, 0.0f };
		spec.Wind         = { 0.0f, 0.0f, 0.0f };
		spec.Drag         = 0.0f;
		return spec;
	}

	TEST_CASE("spawn window fills exactly its slots; the rest stay dead")
	{
		auto spec = BasicSpec();
		std::vector<Cosmic::GpuParticle> pool(spec.MaxParticles);

		Cosmic::ParticleEmitter::StepCpu(pool, spec, glm::mat4(1.0f), 0.016f,
		                                 /*spawnStart*/ 0, /*spawnCount*/ 4, /*seed*/ 7);

		for (uint32_t i = 0; i < 4; ++i)
		{
			CHECK(pool[i].PosAge.w == doctest::Approx(0.0f));            // fresh
			CHECK(pool[i].VelLife.w >= spec.LifeMin);
			CHECK(pool[i].VelLife.w <= spec.LifeMax);
			const float speed = glm::length(glm::vec3(pool[i].VelLife));
			CHECK(speed >= spec.SpeedMin - 1e-4f);
			CHECK(speed <= spec.SpeedMax + 1e-4f);
		}
		for (uint32_t i = 4; i < 8; ++i)
			CHECK(pool[i].PosAge.w >= pool[i].VelLife.w);                // still dead
	}

	TEST_CASE("spawn window wraps the ring buffer")
	{
		auto spec = BasicSpec();
		std::vector<Cosmic::GpuParticle> pool(spec.MaxParticles);

		// Start at 6, count 4 -> slots 6, 7, 0, 1.
		Cosmic::ParticleEmitter::StepCpu(pool, spec, glm::mat4(1.0f), 0.016f, 6, 4, 11);

		CHECK(pool[6].PosAge.w == doctest::Approx(0.0f));
		CHECK(pool[7].PosAge.w == doctest::Approx(0.0f));
		CHECK(pool[0].PosAge.w == doctest::Approx(0.0f));
		CHECK(pool[1].PosAge.w == doctest::Approx(0.0f));
		CHECK(pool[2].PosAge.w >= pool[2].VelLife.w);
		CHECK(pool[5].PosAge.w >= pool[5].VelLife.w);
	}

	TEST_CASE("integration: gravity + drag advance velocity, position and age")
	{
		auto spec = BasicSpec();
		spec.Drag = 0.5f;
		std::vector<Cosmic::GpuParticle> pool(spec.MaxParticles);

		Cosmic::ParticleEmitter::StepCpu(pool, spec, glm::mat4(1.0f), 0.016f, 0, 1, 3);
		const glm::vec3 v0(pool[0].VelLife);
		const glm::vec3 p0(pool[0].PosAge);

		const float dt = 0.1f;
		Cosmic::ParticleEmitter::StepCpu(pool, spec, glm::mat4(1.0f), dt, 0, 0, 4);

		const glm::vec3 vExpected = (v0 + spec.Gravity * dt) * (1.0f - spec.Drag * dt);
		const glm::vec3 pExpected = p0 + vExpected * dt;
		CHECK(glm::length(glm::vec3(pool[0].VelLife) - vExpected) < 1e-4f);
		CHECK(glm::length(glm::vec3(pool[0].PosAge) - pExpected) < 1e-4f);
		CHECK(pool[0].PosAge.w == doctest::Approx(dt));
	}

	TEST_CASE("world-space point emitter spawns at the transform origin")
	{
		auto spec = BasicSpec();
		spec.Space = Cosmic::ParticleSpace::World;
		const glm::mat4 xf = glm::translate(glm::mat4(1.0f), { 5.0f, -2.0f, 9.0f });

		std::vector<Cosmic::GpuParticle> pool(spec.MaxParticles);
		Cosmic::ParticleEmitter::StepCpu(pool, spec, xf, 0.016f, 0, 3, 21);

		for (int i = 0; i < 3; ++i)
		{
			CHECK(pool[i].PosAge.x == doctest::Approx(5.0f));
			CHECK(pool[i].PosAge.y == doctest::Approx(-2.0f));
			CHECK(pool[i].PosAge.z == doctest::Approx(9.0f));
		}
	}

	TEST_CASE("deterministic for identical inputs")
	{
		auto spec = BasicSpec();
		std::vector<Cosmic::GpuParticle> a(spec.MaxParticles), b(spec.MaxParticles);
		Cosmic::ParticleEmitter::StepCpu(a, spec, glm::mat4(1.0f), 0.02f, 2, 5, 1234);
		Cosmic::ParticleEmitter::StepCpu(b, spec, glm::mat4(1.0f), 0.02f, 2, 5, 1234);

		for (uint32_t i = 0; i < spec.MaxParticles; ++i)
		{
			CHECK(a[i].PosAge == b[i].PosAge);
			CHECK(a[i].VelLife == b[i].VelLife);
			CHECK(a[i].SeedSize == b[i].SeedSize);
		}
	}
}
