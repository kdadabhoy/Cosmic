#pragma once

// Noise.h
// Last Modified 7/1/2026
//
// E14 (docs/plans/03-simulation-engine-plan.md): seeded procedural noise —
// value noise, Perlin gradient noise, and fBm octave stacks in 1D/2D/3D.
// Consumers on both roadmap tracks: terrain heightmaps (doc 05 S8), wind
// gusts / Dryden-ish turbulence (doc 04), procedural textures.
//
// DETERMINISM IS THE CONTRACT: a given (seed, coordinate) pair returns the
// same value on every run, platform, and compiler — replays must match.
// Header-only, allocation-free after construction.
//
// Ranges: Value*/Perlin* return [-1, 1] (Perlin's practical range is tighter;
// the bound is guaranteed). Fbm* is normalized back to [-1, 1].
//
// Usage:
//     Cosmic::Noise noise(1337);
//     float h  = noise.Fbm2D(x * 0.01f, y * 0.01f, 6);     // terrain
//     float gust = noise.Perlin1D(t * 0.4f);                // wind channel

#include "math/Random.h"

#include <cstdint>
#include <cmath>
#include <array>
#include <algorithm>   // std::clamp (range-contract guard)

namespace Cosmic
{
	class Noise
	{
	public:
		explicit Noise(uint32_t seed = 0)
			: m_Seed(seed)
		{
			// Seeded permutation table (Fisher-Yates with the E15 PCG32 —
			// portable shuffles, unlike std::shuffle+std::mt19937).
			for (int i = 0; i < 256; ++i)
				m_Perm[i] = static_cast<uint8_t>(i);

			Random rng(0x9E3779B97F4A7C15ULL ^ seed, seed);
			for (int i = 255; i > 0; --i)
			{
				const int j = static_cast<int>(rng.NextUInt32(static_cast<uint32_t>(i + 1)));
				const uint8_t tmp = m_Perm[i];
				m_Perm[i] = m_Perm[j];
				m_Perm[j] = tmp;
			}
			for (int i = 0; i < 256; ++i)
				m_Perm[256 + i] = m_Perm[i];   // wrap-free indexing
		}

		uint32_t GetSeed() const { return m_Seed; }

		// =================================================================
		// Value noise — random values on the integer lattice, smoothly
		// interpolated. Cheaper and "blockier" than Perlin; fine for gusts.
		// =================================================================

		float Value1D(float x) const
		{
			const int xi = FloorToInt(x);
			const float t = Fade(x - static_cast<float>(xi));
			return Lerp(LatticeValue(xi, 0, 0), LatticeValue(xi + 1, 0, 0), t);
		}

		float Value2D(float x, float y) const
		{
			const int xi = FloorToInt(x), yi = FloorToInt(y);
			const float tx = Fade(x - static_cast<float>(xi));
			const float ty = Fade(y - static_cast<float>(yi));

			const float v00 = LatticeValue(xi,     yi,     0);
			const float v10 = LatticeValue(xi + 1, yi,     0);
			const float v01 = LatticeValue(xi,     yi + 1, 0);
			const float v11 = LatticeValue(xi + 1, yi + 1, 0);

			return Lerp(Lerp(v00, v10, tx), Lerp(v01, v11, tx), ty);
		}

		float Value3D(float x, float y, float z) const
		{
			const int xi = FloorToInt(x), yi = FloorToInt(y), zi = FloorToInt(z);
			const float tx = Fade(x - static_cast<float>(xi));
			const float ty = Fade(y - static_cast<float>(yi));
			const float tz = Fade(z - static_cast<float>(zi));

			float v[2];
			for (int k = 0; k <= 1; ++k)
			{
				const float v00 = LatticeValue(xi,     yi,     zi + k);
				const float v10 = LatticeValue(xi + 1, yi,     zi + k);
				const float v01 = LatticeValue(xi,     yi + 1, zi + k);
				const float v11 = LatticeValue(xi + 1, yi + 1, zi + k);
				v[k] = Lerp(Lerp(v00, v10, tx), Lerp(v01, v11, tx), ty);
			}
			return Lerp(v[0], v[1], tz);
		}

		// =================================================================
		// Perlin gradient noise (Ken Perlin's improved noise, seeded table).
		// Zero at lattice points; smoother spectral character than value
		// noise — the terrain/texture workhorse.
		// =================================================================

		float Perlin1D(float x) const
		{
			const int xi = FloorToInt(x);
			const float xf = x - static_cast<float>(xi);
			const float t = Fade(xf);

			const float g0 = Grad1(Hash(xi),     xf);
			const float g1 = Grad1(Hash(xi + 1), xf - 1.0f);
			// 1D gradient noise peaks at +/-0.5 — rescale to use [-1, 1]
			return ClampUnit(2.0f * Lerp(g0, g1, t));
		}

		float Perlin2D(float x, float y) const
		{
			const int xi = FloorToInt(x), yi = FloorToInt(y);
			const float xf = x - static_cast<float>(xi);
			const float yf = y - static_cast<float>(yi);
			const float tx = Fade(xf), ty = Fade(yf);

			const float n00 = Grad2(Hash(xi,     yi),     xf,        yf);
			const float n10 = Grad2(Hash(xi + 1, yi),     xf - 1.0f, yf);
			const float n01 = Grad2(Hash(xi,     yi + 1), xf,        yf - 1.0f);
			const float n11 = Grad2(Hash(xi + 1, yi + 1), xf - 1.0f, yf - 1.0f);

			// max magnitude sqrt(2)/2 — normalize to [-1, 1]
			return ClampUnit(1.41421356f * Lerp(Lerp(n00, n10, tx), Lerp(n01, n11, tx), ty));
		}

		float Perlin3D(float x, float y, float z) const
		{
			const int xi = FloorToInt(x), yi = FloorToInt(y), zi = FloorToInt(z);
			const float xf = x - static_cast<float>(xi);
			const float yf = y - static_cast<float>(yi);
			const float zf = z - static_cast<float>(zi);
			const float tx = Fade(xf), ty = Fade(yf), tz = Fade(zf);

			float n[2];
			for (int k = 0; k <= 1; ++k)
			{
				const float zo = zf - static_cast<float>(k);
				const float n00 = Grad3(Hash(xi,     yi,     zi + k), xf,        yf,        zo);
				const float n10 = Grad3(Hash(xi + 1, yi,     zi + k), xf - 1.0f, yf,        zo);
				const float n01 = Grad3(Hash(xi,     yi + 1, zi + k), xf,        yf - 1.0f, zo);
				const float n11 = Grad3(Hash(xi + 1, yi + 1, zi + k), xf - 1.0f, yf - 1.0f, zo);
				n[k] = Lerp(Lerp(n00, n10, tx), Lerp(n01, n11, tx), ty);
			}
			// max magnitude sqrt(3)/2 ~ 0.866 — normalize to [-1, 1]. The
			// theoretical bound is a slight under-estimate of the empirical
			// peak, so clamp to keep the documented range contract exact.
			return ClampUnit(1.15470054f * Lerp(n[0], n[1], tz));
		}

		// =================================================================
		// fBm — fractal Brownian motion: octave stack of Perlin noise.
		// Each octave: frequency *= lacunarity, amplitude *= gain. Output is
		// re-normalized by the amplitude sum, so the [-1, 1] bound holds for
		// any octave count.
		// =================================================================

		float Fbm1D(float x, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const
		{
			float sum = 0.0f, amp = 1.0f, freq = 1.0f, norm = 0.0f;
			for (int i = 0; i < octaves; ++i)
			{
				sum  += amp * Perlin1D(x * freq);
				norm += amp;
				amp  *= gain;
				freq *= lacunarity;
			}
			return norm > 0.0f ? sum / norm : 0.0f;
		}

		float Fbm2D(float x, float y, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const
		{
			float sum = 0.0f, amp = 1.0f, freq = 1.0f, norm = 0.0f;
			for (int i = 0; i < octaves; ++i)
			{
				sum  += amp * Perlin2D(x * freq, y * freq);
				norm += amp;
				amp  *= gain;
				freq *= lacunarity;
			}
			return norm > 0.0f ? sum / norm : 0.0f;
		}

		float Fbm3D(float x, float y, float z, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const
		{
			float sum = 0.0f, amp = 1.0f, freq = 1.0f, norm = 0.0f;
			for (int i = 0; i < octaves; ++i)
			{
				sum  += amp * Perlin3D(x * freq, y * freq, z * freq);
				norm += amp;
				amp  *= gain;
				freq *= lacunarity;
			}
			return norm > 0.0f ? sum / norm : 0.0f;
		}

	private:
		// -----------------------------------------------------------------
		// Lattice plumbing
		// -----------------------------------------------------------------

		static int FloorToInt(float v)
		{
			const int i = static_cast<int>(v);
			return (v < static_cast<float>(i)) ? i - 1 : i;
		}

		static float Fade(float t)   { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
		static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
		static float ClampUnit(float v) { return std::clamp(v, -1.0f, 1.0f); }

		// Permutation-table hash of integer lattice coordinates (wrap 256).
		uint8_t Hash(int x, int y = 0, int z = 0) const
		{
			const uint8_t xi = static_cast<uint8_t>(x & 255);
			const uint8_t yi = static_cast<uint8_t>(y & 255);
			const uint8_t zi = static_cast<uint8_t>(z & 255);
			return m_Perm[m_Perm[m_Perm[xi] + yi] + zi];
		}

		// Deterministic lattice value in [-1, 1] for value noise.
		float LatticeValue(int x, int y, int z) const
		{
			return static_cast<float>(Hash(x, y, z)) * (2.0f / 255.0f) - 1.0f;
		}

		// Perlin gradient dot products.
		static float Grad1(uint8_t h, float x)
		{
			return (h & 1) ? -x : x;
		}

		static float Grad2(uint8_t h, float x, float y)
		{
			// 8 gradient directions
			switch (h & 7)
			{
			case 0: return  x + y;
			case 1: return  x - y;
			case 2: return -x + y;
			case 3: return -x - y;
			case 4: return  x;
			case 5: return -x;
			case 6: return  y;
			default: return -y;
			}
		}

		static float Grad3(uint8_t h, float x, float y, float z)
		{
			// Ken Perlin's 12-gradient scheme (improved noise reference)
			const uint8_t hh = h & 15;
			const float u = hh < 8 ? x : y;
			const float v = hh < 4 ? y : (hh == 12 || hh == 14 ? x : z);
			return ((hh & 1) ? -u : u) + ((hh & 2) ? -v : v);
		}

		uint32_t m_Seed = 0;
		std::array<uint8_t, 512> m_Perm{};
	};
}
