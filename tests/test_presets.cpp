// test_presets.cpp — particle emitter preset invariants (doc 10 F8 + F9).
// Headless (no GL): the presets are pure functions returning a ParticleEmitterSpec.

#include "doctest.h"
#include "particles/Presets.h"

#include <glm/glm.hpp>

using namespace Cosmic;

TEST_SUITE("Particle presets (F8/F9)")
{
	// Every preset must produce a physically-sane spec the emitter can consume.
	static void CheckCommon(const ParticleEmitterSpec& s)
	{
		CHECK(s.SpawnRate > 0.0f);
		CHECK(s.MaxParticles > 0u);
		CHECK(s.LifeMin > 0.0f);
		CHECK(s.LifeMin <= s.LifeMax);
		CHECK(s.SpeedMin <= s.SpeedMax);
		CHECK(s.SizeStart >= 0.0f);
		CHECK(s.SizeEnd   >= 0.0f);
	}

	TEST_CASE("SoftPuff is a sane alpha puff")
	{
		const ParticleEmitterSpec s = Presets::SoftPuff();
		CheckCommon(s);
		CHECK(s.Blend == ParticleBlend::Alpha);
		CHECK(s.SizeEnd > s.SizeStart);          // grows
	}

	TEST_CASE("Snowfall: slow gravity, box shape, honors params")
	{
		const glm::vec3 box{ 120.0f, 60.0f, 120.0f };
		const ParticleEmitterSpec s = Presets::Snowfall(box, 3000.0f);
		CheckCommon(s);
		CHECK(s.SpawnRate == doctest::Approx(3000.0f));
		CHECK(s.Shape == EmitterShape::Box);
		CHECK(s.BoxExtents.x == doctest::Approx(box.x));
		CHECK(s.BoxExtents.y == doctest::Approx(box.y));
		CHECK(s.BoxExtents.z == doctest::Approx(box.z));
		// Snow flutters — gentle gravity (spec invariant: magnitude < 2 m/s^2).
		CHECK(glm::length(s.Gravity) < 2.0f);
		CHECK(s.Gravity.y < 0.0f);               // falls, not rises
	}

	TEST_CASE("Embers are additive and warm")
	{
		const ParticleEmitterSpec s = Presets::Embers(400.0f);
		CheckCommon(s);
		CHECK(s.SpawnRate == doctest::Approx(400.0f));
		CHECK(s.Blend == ParticleBlend::Additive);
		CHECK(s.Shape == EmitterShape::Cone);
		CHECK(s.Gravity.y > 0.0f);               // hot air lifts embers
		CHECK(s.ColorStart.r > s.ColorStart.b);  // warm tint
	}

	TEST_CASE("SmokeColumn expands and rises")
	{
		const ParticleEmitterSpec s = Presets::SmokeColumn(120.0f);
		CheckCommon(s);
		CHECK(s.SpawnRate == doctest::Approx(120.0f));
		CHECK(s.SizeEnd > s.SizeStart);          // puffs expand
		CHECK(s.Gravity.y > 0.0f);               // buoyant
	}

	TEST_CASE("Mist is a big low-alpha volume")
	{
		const glm::vec3 box{ 200.0f, 8.0f, 200.0f };
		const ParticleEmitterSpec s = Presets::Mist(box);
		CheckCommon(s);
		CHECK(s.Shape == EmitterShape::Box);
		CHECK(s.ColorStart.a < 0.3f);            // faint
		CHECK(s.SizeStart > 1.0f);               // huge puffs
	}

	TEST_CASE("Rain streaks downward")
	{
		const glm::vec3 box{ 200.0f, 80.0f, 200.0f };
		const ParticleEmitterSpec s = Presets::Rain(box, 4000.0f);
		CheckCommon(s);
		CHECK(s.SpawnRate == doctest::Approx(4000.0f));
		CHECK(s.Shape == EmitterShape::Box);
		CHECK(s.Gravity.y < 0.0f);               // straight down
		CHECK(s.StretchByVelocity > 0.0f);       // elongates into streaks (F9)
	}

	TEST_CASE("SplashRings expand and fade")
	{
		const ParticleEmitterSpec s = Presets::SplashRings(30.0f);
		CheckCommon(s);
		CHECK(s.SpawnRate == doctest::Approx(30.0f));
		CHECK(s.SizeEnd > s.SizeStart);          // ring expands
		CHECK(s.ColorEnd.a < s.ColorStart.a);    // fades out
	}
}
