#pragma once

// Wind.h
//
// Steady wind + Dryden-ish gusts from the engine's E14 seeded noise (doc 04
// §2.1: "wind = steady + E14 noise gusts"). Deterministic: same seed + same
// sim time = same gust history, so recordings replay exactly.

#include <Cosmic.h>

#include <glm/glm.hpp>

namespace Viper
{
	class WindField
	{
	public:
		glm::vec3 steadyNed{ 0.0f };   // m/s
		float     gustSigma = 0.0f;    // m/s, ~1-sigma turbulence amplitude
		float     gustFreqHz = 0.35f;  // dominant gust frequency

		explicit WindField(uint32_t seed = 1337) : m_Noise(seed) {}

		// Wind vector at sim time t (seconds). Three independent Perlin
		// channels (offset in noise space) approximate low-passed turbulence.
		glm::vec3 Sample(float t) const
		{
			if (gustSigma <= 0.0f)
				return steadyNed;

			const float x = t * gustFreqHz;
			// Perlin1D ∈ [-1,1]; scale ~2σ so peaks reach the advertised gust.
			const glm::vec3 gust{
				m_Noise.Perlin1D(x)           + 0.5f * m_Noise.Perlin1D(x * 3.1f + 11.3f),
				m_Noise.Perlin1D(x + 101.7f)  + 0.5f * m_Noise.Perlin1D(x * 3.1f + 57.9f),
				0.35f * m_Noise.Perlin1D(x + 233.1f),
			};
			return steadyNed + gust * (2.0f * gustSigma);
		}

	private:
		Cosmic::Noise m_Noise;
	};
}
