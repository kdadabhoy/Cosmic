#pragma once

// Random.h
// Last Modified 7/1/2026
//
// E15 (docs/plans/03-simulation-engine-plan.md): seedable, PORTABLE randomness.
// std::mt19937 sequences are portable, but the standard <random> DISTRIBUTIONS
// are not (implementations differ across compilers) — which silently breaks
// replay determinism the moment a log is replayed on another toolchain. This
// wrapper owns both the generator (PCG32, O'Neill 2014, public-domain
// algorithm) and the distributions, so a seed produces the same sensor noise
// everywhere, forever. Header-only, allocation-free.
//
// Usage:
//     Cosmic::Random rng(1234);                 // deterministic stream
//     float u  = rng.NextFloat();               // [0, 1)
//     float g  = rng.Gaussian(0.0f, 0.02f);     // gyro noise
//     glm::vec3 p = rng.InUnitSphere();

#include <cstdint>
#include <cmath>
#include <glm/glm.hpp>

namespace Cosmic
{
	class Random
	{
	public:
		// Default seed gives a valid, deterministic stream; pass your own for
		// independent streams (one per sensor keeps replays stable when a new
		// consumer is added).
		explicit Random(uint64_t seed = 0x853c49e6748fea9bULL, uint64_t stream = 0xda3e39cb94b95bdbULL)
		{
			Seed(seed, stream);
		}

		void Seed(uint64_t seed, uint64_t stream = 0xda3e39cb94b95bdbULL)
		{
			// PCG32 seeding recipe: ensure the increment is odd, advance once
			// around the initial state so seed=0 is fine.
			m_Inc = (stream << 1u) | 1u;
			m_State = 0u;
			NextUInt32();
			m_State += seed;
			NextUInt32();
			m_HasSpareGaussian = false;
		}

		// ---------------------------------------------------------------
		// Core generator — PCG32 (XSH-RR variant): 64-bit LCG state, 32-bit
		// xorshift+rotate output.
		// ---------------------------------------------------------------
		uint32_t NextUInt32()
		{
			const uint64_t old = m_State;
			m_State = old * 6364136223846793005ULL + m_Inc;
			const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
			const uint32_t rot = static_cast<uint32_t>(old >> 59u);
			return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
		}

		// Uniform in [0, bound) without modulo bias.
		uint32_t NextUInt32(uint32_t bound)
		{
			if (bound == 0)
				return 0;
			const uint32_t threshold = (0u - bound) % bound;
			for (;;)
			{
				const uint32_t r = NextUInt32();
				if (r >= threshold)
					return r % bound;
			}
		}

		// Uniform float in [0, 1) with 24-bit resolution (exact float grid).
		float NextFloat()
		{
			return static_cast<float>(NextUInt32() >> 8) * (1.0f / 16777216.0f);
		}

		// Uniform float in [min, max).
		float Range(float min, float max)
		{
			return min + (max - min) * NextFloat();
		}

		// Uniform int in [min, max] inclusive.
		int RangeInt(int min, int max)
		{
			if (max <= min)
				return min;
			return min + static_cast<int>(NextUInt32(static_cast<uint32_t>(max - min + 1)));
		}

		// ---------------------------------------------------------------
		// Gaussian(mean, sigma) — Box-Muller with spare caching. Fixed
		// algorithm = portable sequences (the reason std::normal_distribution
		// is banned here).
		// ---------------------------------------------------------------
		float Gaussian(float mean = 0.0f, float sigma = 1.0f)
		{
			if (m_HasSpareGaussian)
			{
				m_HasSpareGaussian = false;
				return mean + sigma * m_SpareGaussian;
			}

			float u1;
			do { u1 = NextFloat(); } while (u1 <= 1e-12f);   // avoid log(0)
			const float u2 = NextFloat();

			const float r = std::sqrt(-2.0f * std::log(u1));
			const float theta = 6.28318530717958647692f * u2;

			m_SpareGaussian = r * std::sin(theta);
			m_HasSpareGaussian = true;
			return mean + sigma * (r * std::cos(theta));
		}

		// ---------------------------------------------------------------
		// Geometric helpers
		// ---------------------------------------------------------------

		// Uniform inside the unit sphere (rejection sampling — exact, and the
		// loop runs ~1.9 iterations on average).
		glm::vec3 InUnitSphere()
		{
			for (;;)
			{
				const glm::vec3 v(Range(-1.0f, 1.0f), Range(-1.0f, 1.0f), Range(-1.0f, 1.0f));
				if (glm::dot(v, v) <= 1.0f)
					return v;
			}
		}

		// Uniform on the unit sphere surface.
		glm::vec3 OnUnitSphere()
		{
			// Marsaglia (1972) — no trig-heavy latitude bias
			for (;;)
			{
				const float a = Range(-1.0f, 1.0f);
				const float b = Range(-1.0f, 1.0f);
				const float s = a * a + b * b;
				if (s >= 1.0f)
					continue;
				const float root = 2.0f * std::sqrt(1.0f - s);
				return glm::vec3(a * root, b * root, 1.0f - 2.0f * s);
			}
		}

		// Uniform inside the unit disc (z = 0).
		glm::vec2 InUnitDisc()
		{
			for (;;)
			{
				const glm::vec2 v(Range(-1.0f, 1.0f), Range(-1.0f, 1.0f));
				if (glm::dot(v, v) <= 1.0f)
					return v;
			}
		}

	private:
		uint64_t m_State = 0;
		uint64_t m_Inc = 0;

		float m_SpareGaussian = 0.0f;
		bool  m_HasSpareGaussian = false;
	};
}
