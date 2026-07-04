// test_worldsystems.cpp — E18: world-system authoring recipes. Headless (no GL):
//   - recipe -> engine-spec mapping (terrain / water / particles)
//   - terrain Resolution clamp to a valid 32*2^k + 1
//   - .cemitter round-trip through the generic reflected-struct serializer
//   - parameter-signature change detection (drives Scene::SyncWorldSystems)
// World-coordinate tolerances are ABSOLUTE (doctest::Approx.epsilon is relative).

#include "doctest.h"

#include "scene/Components.h"
#include "scene/WorldSystemRecipes.h"
#include "scene/SceneSerializer.h"
#include "terrain/Terrain.h"
#include "water/Water.h"
#include "water/Presets.h"
#include "particles/ParticleSystem.h"

#include <entt/entt.hpp>
#include <cmath>
#include <string>

using namespace Cosmic;

namespace
{
	bool NearAbs(float a, float b, float tol = 1e-5f) { return std::fabs(a - b) <= tol; }
}

TEST_SUITE("World systems / authoring recipes (E18)")
{
	// ---- Terrain --------------------------------------------------------------

	TEST_CASE("Terrain resolution clamps to a valid 32*2^k + 1")
	{
		CHECK(ClampTerrainResolution(100) == 129u);   // 29 from 129 vs 35 from 65
		CHECK(ClampTerrainResolution(500) == 513u);
		CHECK(ClampTerrainResolution(40)  == 65u);    // below range -> smallest valid
		CHECK(ClampTerrainResolution(9000) == 1025u); // above range -> largest valid
		CHECK(ClampTerrainResolution(257) == 257u);   // already valid
	}

	TEST_CASE("Terrain recipe maps onto TerrainSpecification and builds headless")
	{
		TerrainComponent c;
		c.WorldSize   = 1024.0f;
		c.Resolution  = 100;          // -> 129
		c.HeightScale = 42.0f;
		c.BaseHeight  = -3.0f;
		c.Seed        = 4242u;
		c.Octaves     = 7;
		c.Frequency   = 5.5f;
		c.Lacunarity  = 2.25f;
		c.Gain        = 0.6f;
		c.EdgeFalloff = 0.3f;
		c.GrassColor  = { 0.1f, 0.7f, 0.2f };
		c.SnowColor   = { 0.9f, 0.95f, 1.0f };
		c.SnowHeight  = 55.0f;
		c.SnowBlend   = 4.0f;

		const TerrainSpecification s = BuildTerrainSpec(c);
		CHECK(s.Resolution == 129u);
		CHECK(NearAbs(s.WorldSize, 1024.0f));
		CHECK(NearAbs(s.HeightScale, 42.0f));
		CHECK(NearAbs(s.BaseHeight, -3.0f));
		CHECK(s.Seed == 4242u);
		CHECK(s.Octaves == 7);
		CHECK(NearAbs(s.Frequency, 5.5f));
		CHECK(NearAbs(s.Lacunarity, 2.25f));
		CHECK(NearAbs(s.Gain, 0.6f));
		CHECK(NearAbs(s.EdgeFalloff, 0.3f));
		CHECK(NearAbs(s.Layers[0].Color.g, 0.7f));   // grass -> layer 0
		CHECK(NearAbs(s.Layers[2].Color.b, 1.0f));   // snow  -> layer 2
		CHECK(NearAbs(s.Material.HighHeight, 55.0f));
		CHECK(NearAbs(s.Material.HighBlend, 4.0f));

		// The mapped spec is a valid terrain (procedural, no GL needed at Create).
		CHECK(Terrain::Create(s) != nullptr);
	}

	TEST_CASE("SnowBlend is floored so the smoothstep band is never degenerate")
	{
		TerrainComponent c;
		c.SnowBlend = 0.0f;
		CHECK(BuildTerrainSpec(c).Material.HighBlend >= 0.01f);
	}

	// ---- Water ----------------------------------------------------------------

	TEST_CASE("Water recipe seeds a preset and applies overrides")
	{
		WaterComponent c;
		c.Preset        = WaterPreset::Ocean;
		c.Center        = { 12.0f, -8.0f };
		c.Extent        = { 300.0f, 250.0f };
		c.SurfaceHeight = 2.5f;
		c.GridResolution = 65;
		c.Amplitude     = 2.0f;
		c.Choppiness    = 0.5f;
		c.ShallowColor  = { 0.2f, 0.5f, 0.6f };
		c.DeepColor     = { 0.0f, 0.1f, 0.2f };
		c.WhitecapStrength = 0.4f;

		const WaterSpecification s = BuildWaterSpec(c);
		CHECK(NearAbs(s.Center.x, 12.0f));
		CHECK(NearAbs(s.Center.y, -8.0f));
		CHECK(NearAbs(s.Extent.x, 300.0f));
		CHECK(NearAbs(s.SurfaceHeight, 2.5f));
		CHECK(s.GridResolution == 65u);
		CHECK(NearAbs(s.ShallowColor.g, 0.5f));
		CHECK(NearAbs(s.DeepColor.b, 0.2f));
		CHECK(NearAbs(s.WhitecapStrength, 0.4f));

		// Preset waves present; amplitude/choppiness scaled the ocean stack in place.
		REQUIRE(!s.Waves.empty());
		const WaterSpecification ocean = Presets::OceanWater();
		REQUIRE(ocean.Waves.size() == s.Waves.size());
		CHECK(NearAbs(s.Waves[0].Amplitude, ocean.Waves[0].Amplitude * 2.0f, 1e-4f));
		CHECK(NearAbs(s.Waves[0].Steepness, ocean.Waves[0].Steepness * 0.5f, 1e-4f));

		CHECK(Water::Create(s) != nullptr);
	}

	// ---- Particles ------------------------------------------------------------

	TEST_CASE("Emitter recipe maps onto ParticleEmitterSpec")
	{
		ParticleEmitterComponent c;
		c.MaxParticles = 4096;
		c.SpawnRate    = 120.0f;
		c.Shape        = EmitterShape::Sphere;
		c.SpeedMin     = 3.0f;
		c.SpeedMax     = 1.0f;      // deliberately < min -> clamped up
		c.LifeMin      = 2.0f;
		c.LifeMax      = 1.0f;      // deliberately < min -> clamped up
		c.ColorStart   = { 1.0f, 0.5f, 0.1f, 1.0f };
		c.Blend        = ParticleBlend::Additive;
		c.Space        = ParticleSpace::Local;
		c.StretchByVelocity = 0.03f;

		const ParticleEmitterSpec s = BuildEmitterSpec(c);
		CHECK(s.MaxParticles == 4096u);
		CHECK(NearAbs(s.SpawnRate, 120.0f));
		CHECK(s.Shape == EmitterShape::Sphere);
		CHECK(NearAbs(s.SpeedMax, 3.0f));   // clamped up to SpeedMin
		CHECK(NearAbs(s.LifeMax, 2.0f));    // clamped up to LifeMin
		CHECK(NearAbs(s.ColorStart.r, 1.0f));
		CHECK(s.Blend == ParticleBlend::Additive);
		CHECK(s.Space == ParticleSpace::Local);
		CHECK(NearAbs(s.StretchByVelocity, 0.03f));
	}

	TEST_CASE(".cemitter round-trips through the reflected serializer")
	{
		const uint32_t tid = entt::type_hash<ParticleEmitterComponent>::value();

		ParticleEmitterComponent a;
		a.UseRecipe   = true;
		a.SpawnRate   = 200.0f;
		a.Shape       = EmitterShape::Cone;
		a.ConeAngleDeg = 33.0f;
		a.LifeMin     = 1.5f;
		a.ColorEnd    = { 1.0f, 0.2f, 0.0f, 0.0f };
		a.Blend       = ParticleBlend::Additive;
		a.TexturePath = "project://textures/smoke.png";
		a.StretchByVelocity = 0.05f;

		const std::string json = SceneSerializer::SaveReflectedToString(tid, &a);
		CHECK(json.find("SpawnRate") != std::string::npos);
		CHECK(json.find("cosmic_type") != std::string::npos);

		ParticleEmitterComponent b;
		REQUIRE(SceneSerializer::LoadReflectedFromString(tid, &b, json));
		CHECK(b.UseRecipe == true);
		CHECK(NearAbs(b.SpawnRate, 200.0f));
		CHECK(b.Shape == EmitterShape::Cone);
		CHECK(NearAbs(b.ConeAngleDeg, 33.0f));
		CHECK(NearAbs(b.LifeMin, 1.5f));
		CHECK(NearAbs(b.ColorEnd.r, 1.0f));
		CHECK(b.Blend == ParticleBlend::Additive);
		CHECK(b.TexturePath == "project://textures/smoke.png");
		CHECK(NearAbs(b.StretchByVelocity, 0.05f));
	}

	// ---- Signatures -----------------------------------------------------------

	TEST_CASE("Recipe signatures detect parameter changes (not UseRecipe)")
	{
		// Terrain
		TerrainComponent t1, t2;
		CHECK(TerrainRecipeSignature(t1) == TerrainRecipeSignature(t2));
		t2.HeightScale += 1.0f;
		CHECK(TerrainRecipeSignature(t1) != TerrainRecipeSignature(t2));
		TerrainComponent t3;
		t3.UseRecipe = !t3.UseRecipe;                 // gate flag, NOT a build parameter
		CHECK(TerrainRecipeSignature(t1) == TerrainRecipeSignature(t3));

		// Water
		WaterComponent w1, w2;
		CHECK(WaterRecipeSignature(w1) == WaterRecipeSignature(w2));
		w2.Preset = WaterPreset::Storm;
		CHECK(WaterRecipeSignature(w1) != WaterRecipeSignature(w2));

		// Particles
		ParticleEmitterComponent p1, p2;
		CHECK(EmitterRecipeSignature(p1) == EmitterRecipeSignature(p2));
		p2.TexturePath = "project://textures/x.png";
		CHECK(EmitterRecipeSignature(p1) != EmitterRecipeSignature(p2));
	}
}
