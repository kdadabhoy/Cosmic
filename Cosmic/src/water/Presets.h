#pragma once

// water/Presets.h
// Last Modified: 7/4/2026

/**
 * ============================================================================
 * COSMIC ENGINE — Water surface presets (E18)
 * ============================================================================
 *
 * Header-only PURE functions returning a WaterSpecification for the common
 * water bodies an authoring recipe seeds from: a calm alpine LAKE, an open
 * OCEAN swell, and a heavy STORM sea. They are ENGINE-GENERIC (no scenario
 * constants, no GPU state) — the WorldSystemsPanel recipe (E18) picks one and
 * then overrides center/extent/surface-height/optics on top of it. Because they
 * are pure they are unit-testable headless.
 *
 * Mirrors particles/Presets.h in spirit (Cosmic::Presets namespace).
 * ============================================================================
 */

#include "water/Water.h"
#include "water/GerstnerWave.h"

#include <glm/glm.hpp>

namespace Cosmic::Presets
{
	/** Calm reflective lake: tiny slow wavelets — a near-mirror surface. */
	inline WaterSpecification LakeWater()
	{
		WaterSpecification s;
		s.Waves = {
			{ {  1.00f,  0.20f }, 6.0f, 0.030f, 0.30f, 0.0f, 0.0f },
			{ {  0.30f, -0.95f }, 3.5f, 0.018f, 0.25f, 0.0f, 1.3f },
			{ { -0.60f,  0.80f }, 2.0f, 0.010f, 0.20f, 0.0f, 2.6f },
		};
		s.ShallowColor      = { 0.12f, 0.40f, 0.42f };
		s.DeepColor         = { 0.03f, 0.14f, 0.20f };
		s.DepthFadeDistance = 4.0f;
		s.SpecularPower     = 300.0f;
		s.DetailStrength    = 0.20f;
		return s;
	}

	/** Open-water swell: a primary long wave plus crossing detail — gentle seas. */
	inline WaterSpecification OceanWater()
	{
		WaterSpecification s;
		s.Waves = {
			{ {  1.00f,  0.25f }, 22.0f, 0.32f, 0.60f, 0.0f, 0.0f },
			{ {  0.65f, -0.75f }, 11.0f, 0.16f, 0.50f, 0.0f, 1.3f },
			{ { -0.30f,  0.95f },  6.0f, 0.08f, 0.40f, 0.0f, 2.6f },
			{ {  0.85f,  0.55f },  3.5f, 0.04f, 0.35f, 0.0f, 3.9f },
		};
		s.ShallowColor      = { 0.08f, 0.36f, 0.44f };
		s.DeepColor         = { 0.01f, 0.09f, 0.18f };
		s.DepthFadeDistance = 8.0f;
		s.SpecularPower     = 200.0f;
		s.WhitecapStrength  = 0.25f;
		s.ShoreDepthRange   = 6.0f;
		return s;
	}

	/** Heavy storm sea: tall steep whitecapping swell from several directions. */
	inline WaterSpecification StormWater()
	{
		WaterSpecification s;
		s.Waves = {
			{ {  1.00f,  0.15f }, 30.0f, 0.75f, 0.85f, 0.0f, 0.0f },
			{ {  0.55f, -0.85f }, 18.0f, 0.45f, 0.75f, 0.0f, 1.1f },
			{ { -0.40f,  0.90f }, 11.0f, 0.28f, 0.70f, 0.0f, 2.2f },
			{ {  0.90f,  0.45f },  6.5f, 0.14f, 0.60f, 0.0f, 3.3f },
			{ { -0.75f, -0.65f },  3.5f, 0.07f, 0.55f, 0.0f, 4.4f },
		};
		s.ShallowColor      = { 0.10f, 0.28f, 0.34f };
		s.DeepColor         = { 0.02f, 0.06f, 0.12f };
		s.DepthFadeDistance = 9.0f;
		s.SpecularPower     = 160.0f;
		s.WhitecapStrength  = 0.6f;
		s.ShoreDepthRange   = 7.0f;
		return s;
	}
}
