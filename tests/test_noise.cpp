// test_noise.cpp — math/Noise.h (E14): seed determinism, range bounds,
// lattice-zero property of Perlin, fBm octave falloff and normalization.

#include <string>
#include <vector>
#include <cmath>

#include "doctest.h"
#include "math/Noise.h"

TEST_SUITE("Noise (E14)")
{
	TEST_CASE("same seed => identical field; different seed => different field")
	{
		Cosmic::Noise a(1337), b(1337), c(42);

		int diffs = 0;
		for (int i = 0; i < 500; ++i)
		{
			const float x = static_cast<float>(i) * 0.173f;
			const float y = static_cast<float>(i) * 0.317f;

			REQUIRE(a.Perlin2D(x, y) == b.Perlin2D(x, y));
			REQUIRE(a.Value3D(x, y, x + y) == b.Value3D(x, y, x + y));
			REQUIRE(a.Fbm2D(x, y, 5) == b.Fbm2D(x, y, 5));

			if (std::abs(a.Perlin2D(x, y) - c.Perlin2D(x, y)) > 1e-6f)
				++diffs;
		}
		CHECK(diffs > 400);   // fields decorrelate across seeds
	}

	TEST_CASE("all variants stay within [-1, 1]")
	{
		Cosmic::Noise n(7);

		for (int i = 0; i < 3000; ++i)
		{
			const float x = static_cast<float>(i) * 0.0913f - 137.0f;
			const float y = static_cast<float>(i) * 0.0577f + 41.0f;
			const float z = static_cast<float>(i) * 0.0331f - 3.0f;

			for (float v : { n.Value1D(x), n.Value2D(x, y), n.Value3D(x, y, z),
			                 n.Perlin1D(x), n.Perlin2D(x, y), n.Perlin3D(x, y, z),
			                 n.Fbm1D(x, 6), n.Fbm2D(x, y, 6), n.Fbm3D(x, y, z, 6) })
			{
				REQUIRE(v >= -1.0f);
				REQUIRE(v <= 1.0f);
			}
		}
	}

	TEST_CASE("noise actually varies (not a constant field)")
	{
		Cosmic::Noise n(11);
		float mn = 1e9f, mx = -1e9f;
		for (int i = 0; i < 1000; ++i)
		{
			const float v = n.Perlin2D(static_cast<float>(i) * 0.139f, static_cast<float>(i) * 0.211f);
			mn = std::min(mn, v);
			mx = std::max(mx, v);
		}
		CHECK(mx - mn > 0.5f);
	}

	TEST_CASE("Perlin is zero on the integer lattice")
	{
		Cosmic::Noise n(3);
		for (int i = -5; i <= 5; ++i)
		{
			CHECK(n.Perlin1D(static_cast<float>(i)) == doctest::Approx(0.0f).epsilon(1e-5));
			CHECK(n.Perlin2D(static_cast<float>(i), static_cast<float>(-i)) == doctest::Approx(0.0f).epsilon(1e-5));
			CHECK(n.Perlin3D(static_cast<float>(i), 2.0f * i, static_cast<float>(-i)) == doctest::Approx(0.0f).epsilon(1e-5));
		}
	}

	TEST_CASE("fBm octave falloff: successive octaves contribute geometrically less")
	{
		Cosmic::Noise n(2026);
		const float gain = 0.5f;

		// max |fbm_k - fbm_{k-1}| over samples, per octave count k
		float prevDelta = 1e9f;
		for (int k = 2; k <= 6; ++k)
		{
			float maxDelta = 0.0f;
			for (int i = 0; i < 400; ++i)
			{
				const float x = static_cast<float>(i) * 0.0731f;
				const float y = static_cast<float>(i) * 0.0417f;
				maxDelta = std::max(maxDelta,
					std::abs(n.Fbm2D(x, y, k, 2.0f, gain) - n.Fbm2D(x, y, k - 1, 2.0f, gain)));
			}

			// adding octave k perturbs the (normalized) field by roughly
			// gain^(k-1); require monotone shrink with slack
			CHECK(maxDelta < prevDelta * 0.9f);
			prevDelta = maxDelta;
		}

		// 1-octave fBm reduces to plain Perlin
		CHECK(n.Fbm2D(0.37f, 0.61f, 1) == doctest::Approx(n.Perlin2D(0.37f, 0.61f)));
	}
}
