#pragma once

// GerstnerWave.h
// Last Modified: 7/3/2026

/**
 * ============================================================================
 * COSMIC ENGINE — Gerstner wave math (S9)  [pure, header-only]
 * ============================================================================
 *
 * The single source of truth for the water surface: Water.glsl's vertex stage
 * and the CPU queries (Water::SampleHeight, S9.2) both evaluate THIS wave
 * model, so a floating object and the rendered surface always agree.
 *
 * Classic Gerstner (trochoidal) waves — GPU Gems ch. 1 formulation. A wave
 * displaces a grid point HORIZONTALLY toward its crests as well as vertically,
 * which is why sampling "height at (x, z)" requires the small fixed-point
 * inversion in SampleGerstnerHeight below.
 *
 * Pure math, no GPU types — unit-tested headless in tests/test_phase10_world.cpp.
 * ============================================================================
 */

#include <glm/glm.hpp>
#include <cmath>
#include <vector>

namespace Cosmic
{
	struct GerstnerWave
	{
		glm::vec2 Direction{ 1.0f, 0.0f };   // travel direction (normalized on use)
		float     Wavelength = 8.0f;         // crest-to-crest, meters
		float     Amplitude  = 0.15f;        // meters
		float     Steepness  = 0.6f;         // 0 = sine, 1 = sharpest stable crest
		float     Speed      = 0.0f;         // phase speed m/s; <= 0 -> deep-water dispersion
		float     Phase      = 0.0f;         // radians
	};

	struct GerstnerSample
	{
		glm::vec3 Offset{ 0.0f };            // displacement of the grid point (x, y, z)
		glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };// surface normal at the displaced point
	};

	/**
	 * @brief Evaluate the wave sum at GRID position (x, z) and time t.
	 * Steepness is normalized by k * A * waveCount so the summed surface cannot
	 * self-intersect (loop over crests) no matter how many waves stack.
	 */
	inline GerstnerSample EvaluateGerstner(const std::vector<GerstnerWave>& waves,
	                                       float x, float z, float timeSeconds)
	{
		constexpr float kGravity = 9.81f;

		GerstnerSample out;
		float ny = 1.0f;
		glm::vec2 nxz{ 0.0f };

		const float invCount = waves.empty() ? 0.0f : 1.0f / static_cast<float>(waves.size());
		for (const GerstnerWave& w : waves)
		{
			if (w.Wavelength <= 1e-4f || w.Amplitude <= 0.0f)
				continue;

			const float lenSq = glm::dot(w.Direction, w.Direction);
			const glm::vec2 d = lenSq > 1e-8f ? w.Direction / std::sqrt(lenSq) : glm::vec2(1.0f, 0.0f);

			const float k     = 2.0f * 3.14159265358979f / w.Wavelength;
			const float omega = w.Speed > 0.0f ? k * w.Speed : std::sqrt(kGravity * k);
			const float q     = w.Steepness <= 0.0f ? 0.0f
			                  : w.Steepness / (k * w.Amplitude) * invCount;

			const float phi = k * (d.x * x + d.y * z) - omega * timeSeconds + w.Phase;
			const float c   = std::cos(phi);
			const float s   = std::sin(phi);

			out.Offset.x += q * w.Amplitude * d.x * c;
			out.Offset.z += q * w.Amplitude * d.y * c;
			out.Offset.y += w.Amplitude * s;

			nxz.x += d.x * k * w.Amplitude * c;
			nxz.y += d.y * k * w.Amplitude * c;
			ny    -= q * k * w.Amplitude * s;
		}

		out.Normal = glm::normalize(glm::vec3(-nxz.x, std::max(ny, 1e-3f), -nxz.y));
		return out;
	}

	/**
	 * @brief Surface height at WORLD (x, z) — inverts the horizontal Gerstner
	 * displacement by fixed-point iteration (4 rounds land within ~1 cm even at
	 * high steepness; convergence is linear in the summed steepness), then
	 * reports the vertical offset there. This is the S9.2 buoyancy query; add
	 * the water's SurfaceHeight for the world-space level.
	 */
	inline float SampleGerstnerHeight(const std::vector<GerstnerWave>& waves,
	                                  float x, float z, float timeSeconds,
	                                  int iterations = 4)
	{
		glm::vec2 grid{ x, z };
		GerstnerSample s{};
		for (int i = 0; i < iterations; ++i)
		{
			s = EvaluateGerstner(waves, grid.x, grid.y, timeSeconds);
			grid.x = x - s.Offset.x;
			grid.y = z - s.Offset.z;
		}
		return EvaluateGerstner(waves, grid.x, grid.y, timeSeconds).Offset.y;
	}

	/** @brief Surface normal at WORLD (x, z), via the same inversion. */
	inline glm::vec3 SampleGerstnerNormal(const std::vector<GerstnerWave>& waves,
	                                      float x, float z, float timeSeconds,
	                                      int iterations = 4)
	{
		glm::vec2 grid{ x, z };
		for (int i = 0; i < iterations; ++i)
		{
			const GerstnerSample s = EvaluateGerstner(waves, grid.x, grid.y, timeSeconds);
			grid.x = x - s.Offset.x;
			grid.y = z - s.Offset.z;
		}
		return EvaluateGerstner(waves, grid.x, grid.y, timeSeconds).Normal;
	}
}
