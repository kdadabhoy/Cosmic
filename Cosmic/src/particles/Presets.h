#pragma once

// Presets.h
// Last Modified: 7/3/2026

/**
 * ============================================================================
 * COSMIC ENGINE — Particle emitter presets (S11 / doc 10 F8 + F9)
 * ============================================================================
 *
 * Header-only PURE functions returning a ParticleEmitterSpec — the common
 * atmospheric emitters every showcase scene reaches for (smoke, embers, snow,
 * mist, rain, splash rings). They are ENGINE-GENERIC: no scenario constants, no
 * GPU state, no globals — just tuned specs the app feeds to
 * ParticleEmitter::Create and positions with SetTransform. Because they are pure
 * they are unit-testable headless (tests/test_presets.cpp).
 *
 * Fields left at their ParticleEmitterSpec defaults are intentional. The app
 * still owns placement (SetTransform), texture assignment, and per-scene tuning.
 * ============================================================================
 */

#include "particles/ParticleSystem.h"

#include <glm/glm.hpp>

namespace Cosmic::Presets
{
	/** A single soft alpha puff that grows + fades — steam vents, small poofs. */
	inline ParticleEmitterSpec SoftPuff()
	{
		ParticleEmitterSpec s;
		s.MaxParticles = 512;
		s.SpawnRate    = 30.0f;
		s.Shape        = EmitterShape::Sphere;
		s.ShapeRadius  = 0.15f;
		s.SpeedMin = 0.1f; s.SpeedMax = 0.5f;
		s.LifeMin  = 0.8f; s.LifeMax  = 1.8f;
		s.Gravity  = { 0.0f, 0.4f, 0.0f };     // gentle rise
		s.Drag     = 0.7f;
		s.SizeStart = 0.4f; s.SizeEnd = 1.2f;
		s.ColorStart = { 1.0f, 1.0f, 1.0f, 0.5f };
		s.ColorEnd   = { 1.0f, 1.0f, 1.0f, 0.0f };
		s.Blend = ParticleBlend::Alpha;
		s.SoftFadeDistance = 0.5f;
		return s;
	}

	/** Slow fluttering snowfall inside a `boxExtents` volume (the app tracks the
	 *  camera with SetTransform). Low gravity + drift wind = drifting flakes. */
	inline ParticleEmitterSpec Snowfall(const glm::vec3& boxExtents, float rate)
	{
		ParticleEmitterSpec s;
		s.MaxParticles = 8192;
		s.SpawnRate    = rate;
		s.Shape        = EmitterShape::Box;
		s.BoxExtents   = boxExtents;
		s.SpeedMin = 0.0f; s.SpeedMax = 0.2f;
		s.LifeMin  = 4.0f; s.LifeMax  = 9.0f;
		s.Gravity  = { 0.0f, -1.2f, 0.0f };    // slow flutter (magnitude < 2 m/s^2)
		s.Drag     = 0.8f;
		s.Wind     = { 0.6f, 0.0f, 0.3f };     // lateral drift
		s.SizeStart = 0.06f; s.SizeEnd = 0.06f;
		s.ColorStart = { 0.98f, 0.99f, 1.0f, 0.9f };
		s.ColorEnd   = { 0.98f, 0.99f, 1.0f, 0.6f };
		s.Blend = ParticleBlend::Alpha;
		s.Space = ParticleSpace::World;
		s.SoftFadeDistance = 0.2f;
		return s;
	}

	/** Additive warm embers rising in a narrow cone (emitter +Y) — lava, fire pits. */
	inline ParticleEmitterSpec Embers(float rate)
	{
		ParticleEmitterSpec s;
		s.MaxParticles = 2048;
		s.SpawnRate    = rate;
		s.Shape        = EmitterShape::Cone;
		s.ShapeRadius  = 0.5f;
		s.ConeAngleDeg = 20.0f;
		s.SpeedMin = 2.0f; s.SpeedMax = 5.0f;
		s.LifeMin  = 1.2f; s.LifeMax  = 2.8f;
		s.Gravity  = { 0.0f, 1.5f, 0.0f };     // hot air lifts embers
		s.Drag     = 0.7f;
		s.Wind     = { 0.4f, 0.0f, 0.0f };
		s.SizeStart = 0.09f; s.SizeEnd = 0.01f;
		s.ColorStart = { 1.0f, 0.75f, 0.30f, 1.0f };
		s.ColorEnd   = { 1.0f, 0.25f, 0.05f, 0.0f };
		s.Blend = ParticleBlend::Additive;
		s.SoftFadeDistance = 0.2f;
		return s;
	}

	/** Big soft grey puffs rising + expanding in a narrow cone — a volcano/chimney
	 *  smoke column (bent by Wind). Assign a smoke flipbook texture for detail. */
	inline ParticleEmitterSpec SmokeColumn(float rate)
	{
		ParticleEmitterSpec s;
		s.MaxParticles = 2048;
		s.SpawnRate    = rate;
		s.Shape        = EmitterShape::Cone;
		s.ShapeRadius  = 1.0f;
		s.ConeAngleDeg = 14.0f;
		s.SpeedMin = 1.5f; s.SpeedMax = 3.0f;
		s.LifeMin  = 4.0f; s.LifeMax  = 9.0f;
		s.Gravity  = { 0.0f, 1.0f, 0.0f };     // buoyant rise
		s.Drag     = 0.5f;
		s.Wind     = { 1.2f, 0.0f, 0.5f };
		s.SizeStart = 2.0f; s.SizeEnd = 9.0f;  // large expanding puffs
		s.ColorStart = { 0.30f, 0.30f, 0.32f, 0.75f };
		s.ColorEnd   = { 0.20f, 0.20f, 0.22f, 0.0f };
		s.Blend = ParticleBlend::Alpha;
		s.SoftFadeDistance = 1.0f;
		return s;
	}

	/** Huge slow low-alpha soft puffs inside `boxExtents` — ground mist, fog banks. */
	inline ParticleEmitterSpec Mist(const glm::vec3& boxExtents)
	{
		ParticleEmitterSpec s;
		s.MaxParticles = 512;
		s.SpawnRate    = 12.0f;
		s.Shape        = EmitterShape::Box;
		s.BoxExtents   = boxExtents;
		s.SpeedMin = 0.05f; s.SpeedMax = 0.2f;
		s.LifeMin  = 6.0f;  s.LifeMax  = 12.0f;
		s.Gravity  = { 0.0f, 0.05f, 0.0f };
		s.Drag     = 0.9f;
		s.Wind     = { 0.3f, 0.0f, 0.1f };
		s.SizeStart = 6.0f; s.SizeEnd = 12.0f;
		s.ColorStart = { 0.85f, 0.88f, 0.92f, 0.12f };
		s.ColorEnd   = { 0.85f, 0.88f, 0.92f, 0.0f };
		s.Blend = ParticleBlend::Alpha;
		s.SoftFadeDistance = 2.0f;
		return s;
	}

	/** Fast straight-down rain inside `boxExtents` (the app positions the box so its
	 *  bottom sits at the ground/water). Box emission has no initial direction, so
	 *  gravity+drag drive a near-terminal-velocity fall; StretchByVelocity elongates
	 *  each quad into a streak (F9). (doc 10 F9) */
	inline ParticleEmitterSpec Rain(const glm::vec3& boxExtents, float rate)
	{
		ParticleEmitterSpec s;
		s.MaxParticles = 16384;
		s.SpawnRate    = rate;
		s.Shape        = EmitterShape::Box;
		s.BoxExtents   = boxExtents;
		s.SpeedMin = 0.0f; s.SpeedMax = 0.0f;  // box: no initial dir; gravity drives it
		s.LifeMin  = 1.2f; s.LifeMax  = 2.6f;  // short — fade before the ground
		s.Gravity  = { 0.0f, -30.0f, 0.0f };   // strong straight-down pull
		s.Drag     = 2.0f;                     // terminal ~15 m/s (uniform streaks)
		s.Wind     = { 1.5f, 0.0f, 0.0f };     // slight slant
		s.SizeStart = 0.03f; s.SizeEnd = 0.03f;
		s.ColorStart = { 0.62f, 0.70f, 0.85f, 0.35f };
		s.ColorEnd   = { 0.62f, 0.70f, 0.85f, 0.15f };
		s.Blend = ParticleBlend::Alpha;
		s.Space = ParticleSpace::World;
		s.StretchByVelocity = 0.02f;           // elongate into rain streaks (F9)
		s.SoftFadeDistance  = 0.0f;
		return s;
	}

	/** Expanding rings on a water surface — tiny life, size ramps UP while alpha
	 *  ramps DOWN (reads as a splash ripple with the soft-puff sheet). The app
	 *  Burst()s + SetTransform()s one to a chosen surface point. (doc 10 F9) */
	inline ParticleEmitterSpec SplashRings(float rate)
	{
		ParticleEmitterSpec s;
		s.MaxParticles = 256;
		s.SpawnRate    = rate;
		s.Shape        = EmitterShape::Point;
		s.SpeedMin = 0.0f; s.SpeedMax = 0.0f;
		s.LifeMin  = 0.4f; s.LifeMax  = 0.8f;  // tiny life
		s.Gravity  = { 0.0f, 0.0f, 0.0f };
		s.Drag     = 0.0f;
		s.SizeStart = 0.10f; s.SizeEnd = 1.4f; // expanding ring
		s.ColorStart = { 0.90f, 0.95f, 1.0f, 0.5f };
		s.ColorEnd   = { 0.90f, 0.95f, 1.0f, 0.0f };
		s.Blend = ParticleBlend::Alpha;
		s.Space = ParticleSpace::World;
		s.SoftFadeDistance = 0.3f;
		return s;
	}
}
