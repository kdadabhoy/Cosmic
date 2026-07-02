// test_random.cpp — math/Random.h (E15): committed PCG32 reference sequence
// (canonical pcg32 demo values, independently verified), Gaussian moments,
// range bounds, determinism across instances.

#include <string>
#include <vector>
#include <cmath>
#include <cstdint>

#include "doctest.h"
#include "math/Random.h"

TEST_SUITE("Random (E15)")
{
	TEST_CASE("fixed seed reproduces the canonical PCG32 reference sequence")
	{
		// pcg32 XSH-RR with seed=42, stream=54 — the sequence published by the
		// PCG author's demo program. If this test ever fails, the generator no
		// longer matches canonical PCG32 and every committed replay is invalid.
		Cosmic::Random rng(42, 54);

		const uint32_t expected[] = {
			0xa15c02b7u, 0x7b47f409u, 0xba1d3330u,
			0x83d2f293u, 0xbfa4784bu, 0xcbed606eu,
		};
		for (uint32_t e : expected)
			CHECK(rng.NextUInt32() == e);
	}

	TEST_CASE("same seed => identical stream; different stream => different values")
	{
		Cosmic::Random a(1234, 7);
		Cosmic::Random b(1234, 7);
		for (int i = 0; i < 100; ++i)
			REQUIRE(a.NextUInt32() == b.NextUInt32());

		Cosmic::Random c(1234, 8);
		int same = 0;
		Cosmic::Random a2(1234, 7);
		for (int i = 0; i < 100; ++i)
			if (a2.NextUInt32() == c.NextUInt32())
				++same;
		CHECK(same < 5);
	}

	TEST_CASE("NextFloat stays in [0, 1); Range respects bounds")
	{
		Cosmic::Random rng(99);
		for (int i = 0; i < 10000; ++i)
		{
			const float f = rng.NextFloat();
			REQUIRE(f >= 0.0f);
			REQUIRE(f < 1.0f);

			const float r = rng.Range(-2.5f, 7.5f);
			REQUIRE(r >= -2.5f);
			REQUIRE(r < 7.5f);
		}

		for (int i = 0; i < 1000; ++i)
		{
			const int v = rng.RangeInt(3, 9);
			REQUIRE(v >= 3);
			REQUIRE(v <= 9);
		}
	}

	TEST_CASE("Gaussian mean/sigma within tolerance over 1e5 samples")
	{
		Cosmic::Random rng(2026);
		const float mean = 3.0f, sigma = 2.0f;
		const int n = 100000;

		double sum = 0.0, sumSq = 0.0;
		for (int i = 0; i < n; ++i)
		{
			const double g = rng.Gaussian(mean, sigma);
			sum += g;
			sumSq += g * g;
		}

		const double m = sum / n;
		const double var = sumSq / n - m * m;

		// standard error of the mean = sigma/sqrt(n) ~ 0.0063 — allow 5x
		CHECK(m == doctest::Approx(mean).epsilon(0.011));
		CHECK(std::sqrt(var) == doctest::Approx(sigma).epsilon(0.02));
	}

	TEST_CASE("InUnitSphere/OnUnitSphere/InUnitDisc geometric bounds")
	{
		Cosmic::Random rng(7);

		for (int i = 0; i < 2000; ++i)
		{
			const glm::vec3 v = rng.InUnitSphere();
			REQUIRE(glm::dot(v, v) <= 1.0f + 1e-6f);

			const glm::vec3 s = rng.OnUnitSphere();
			REQUIRE(glm::length(s) == doctest::Approx(1.0f).epsilon(1e-4));

			const glm::vec2 d = rng.InUnitDisc();
			REQUIRE(glm::dot(d, d) <= 1.0f + 1e-6f);
		}

		// InUnitSphere covers all octants (sanity against sign bugs)
		int octants[8] = {};
		Cosmic::Random rng2(8);
		for (int i = 0; i < 4000; ++i)
		{
			const glm::vec3 v = rng2.InUnitSphere();
			const int idx = (v.x > 0 ? 1 : 0) | (v.y > 0 ? 2 : 0) | (v.z > 0 ? 4 : 0);
			++octants[idx];
		}
		for (int count : octants)
			CHECK(count > 200);
	}
}
