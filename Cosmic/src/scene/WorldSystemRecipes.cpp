// scene/WorldSystemRecipes.cpp — E18 recipe -> spec mapping + signatures. See header.

#include "scene/WorldSystemRecipes.h"
#include "water/Presets.h"
#include "assets/AssetLibrary.h"
#include "graphics/Texture.h"
#include "utils/FileSystem.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

namespace Cosmic
{
	namespace
	{
		// 64-bit hash-combine (boost-style), matching Scene.cpp's PrimitiveSignature.
		inline void HashMix(std::size_t& h, std::size_t v)
		{
			h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
		}
		inline void HashF(std::size_t& h, float v)  { HashMix(h, std::hash<float>{}(v)); }
		inline void HashI(std::size_t& h, int64_t v){ HashMix(h, std::hash<int64_t>{}(v)); }
		inline void HashS(std::size_t& h, const std::string& v) { HashMix(h, std::hash<std::string>{}(v)); }
		inline void HashV2(std::size_t& h, const glm::vec2& v) { HashF(h, v.x); HashF(h, v.y); }
		inline void HashV3(std::size_t& h, const glm::vec3& v) { HashF(h, v.x); HashF(h, v.y); HashF(h, v.z); }
		inline void HashV4(std::size_t& h, const glm::vec4& v) { HashF(h, v.x); HashF(h, v.y); HashF(h, v.z); HashF(h, v.w); }
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Terrain
	/////////////////////////////////////////////////////////////////////////////////

	uint32_t ClampTerrainResolution(int32_t resolution)
	{
		// Terrain::Create requires (Resolution - 1) == 32 * 2^k. Snap to the nearest
		// valid value in a sane editor range; 2049+ is intentionally excluded (4M+
		// samples stalls interactive authoring — that is a code-driven use).
		static const int32_t kValid[] = { 65, 129, 257, 513, 1025 };
		int32_t best = kValid[0];
		int32_t bestDist = std::abs(resolution - kValid[0]);
		for (int32_t v : kValid)
		{
			const int32_t d = std::abs(resolution - v);
			if (d < bestDist) { bestDist = d; best = v; }
		}
		return static_cast<uint32_t>(best);
	}

	TerrainSpecification BuildTerrainSpec(const TerrainComponent& c)
	{
		TerrainSpecification s;
		s.Resolution   = ClampTerrainResolution(c.Resolution);
		s.WorldSize    = std::max(c.WorldSize, 1.0f);
		s.HeightScale  = c.HeightScale;
		s.BaseHeight   = c.BaseHeight;
		s.HeightmapPath = c.HeightmapPath;              // caller resolves VFS -> fs path
		s.Seed         = c.Seed;
		s.Octaves      = std::max(1, c.Octaves);
		s.Frequency    = c.Frequency;
		s.Lacunarity   = c.Lacunarity;
		s.Gain         = c.Gain;
		s.EdgeFalloff  = std::clamp(c.EdgeFalloff, 0.0f, 1.0f);

		s.Layers[0].Color = c.GrassColor;   // base
		s.Layers[1].Color = c.RockColor;    // slope
		s.Layers[2].Color = c.SnowColor;    // high
		s.Layers[3].Color = c.SandColor;    // low
		// Layer albedo Refs stay null here; the caller resolves splat textures on
		// the main thread (GL) before Terrain::Create.

		s.Material.HighHeight = c.SnowHeight;
		s.Material.HighBlend  = std::max(c.SnowBlend, 0.01f);
		return s;
	}

	std::size_t TerrainRecipeSignature(const TerrainComponent& c)
	{
		std::size_t h = 0xC0FFEEULL;
		HashF(h, c.WorldSize);
		HashI(h, c.Resolution);
		HashF(h, c.HeightScale);
		HashF(h, c.BaseHeight);
		HashI(h, static_cast<int64_t>(c.Seed));
		HashI(h, c.Octaves);
		HashF(h, c.Frequency);
		HashF(h, c.Lacunarity);
		HashF(h, c.Gain);
		HashF(h, c.EdgeFalloff);
		HashS(h, c.HeightmapPath);
		HashV3(h, c.GrassColor); HashV3(h, c.RockColor);
		HashV3(h, c.SnowColor);  HashV3(h, c.SandColor);
		HashS(h, c.GrassTex); HashS(h, c.RockTex); HashS(h, c.SnowTex); HashS(h, c.SandTex);
		HashF(h, c.SnowHeight);
		HashF(h, c.SnowBlend);
		return h;
	}

	void ResolveTerrainSpecAssets(const TerrainComponent& c, TerrainSpecification& spec)
	{
		if (!spec.HeightmapPath.empty())
			spec.HeightmapPath = FileSystem::Resolve(spec.HeightmapPath);

		const std::string* paths[4] = { &c.GrassTex, &c.RockTex, &c.SnowTex, &c.SandTex };
		for (int i = 0; i < 4; ++i)
			if (!paths[i]->empty())
				spec.Layers[i].Albedo = AssetLibrary::GetTexture(*paths[i]);
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Water
	/////////////////////////////////////////////////////////////////////////////////

	WaterSpecification BuildWaterSpec(const WaterComponent& c)
	{
		WaterSpecification s;
		switch (c.Preset)
		{
			case WaterPreset::Ocean: s = Presets::OceanWater(); break;
			case WaterPreset::Storm: s = Presets::StormWater(); break;
			case WaterPreset::Lake:
			default:                 s = Presets::LakeWater();  break;
		}

		s.Center        = c.Center;
		s.Extent        = { std::max(c.Extent.x, 1.0f), std::max(c.Extent.y, 1.0f) };
		s.SurfaceHeight = c.SurfaceHeight;
		s.GridResolution = static_cast<uint32_t>(std::max(c.GridResolution, 2));
		s.ShallowColor   = c.ShallowColor;
		s.DeepColor      = c.DeepColor;
		s.CausticStrength  = std::max(c.CausticStrength, 0.0f);
		s.WhitecapStrength = std::max(c.WhitecapStrength, 0.0f);
		s.SparkleStrength  = std::max(c.SparkleStrength, 0.0f);

		// Amplitude / choppiness scale the preset wave stack in place.
		const float amp = std::max(c.Amplitude, 0.0f);
		const float chp = std::max(c.Choppiness, 0.0f);
		for (GerstnerWave& w : s.Waves)
		{
			w.Amplitude *= amp;
			w.Steepness = std::clamp(w.Steepness * chp, 0.0f, 1.0f);
		}
		return s;
	}

	std::size_t WaterRecipeSignature(const WaterComponent& c)
	{
		std::size_t h = 0xBEEFULL;
		HashI(h, static_cast<int64_t>(c.Preset));
		HashV2(h, c.Center);
		HashV2(h, c.Extent);
		HashF(h, c.SurfaceHeight);
		HashI(h, c.GridResolution);
		HashF(h, c.Amplitude);
		HashF(h, c.Choppiness);
		HashV3(h, c.ShallowColor);
		HashV3(h, c.DeepColor);
		HashF(h, c.CausticStrength);
		HashF(h, c.WhitecapStrength);
		HashF(h, c.SparkleStrength);
		return h;
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Particles
	/////////////////////////////////////////////////////////////////////////////////

	ParticleEmitterSpec BuildEmitterSpec(const ParticleEmitterComponent& c)
	{
		ParticleEmitterSpec s;
		s.MaxParticles  = std::max<uint32_t>(c.MaxParticles, 1);
		s.SpawnRate     = std::max(c.SpawnRate, 0.0f);
		s.Shape         = c.Shape;
		s.ShapeRadius   = c.ShapeRadius;
		s.ConeAngleDeg  = c.ConeAngleDeg;
		s.BoxExtents    = c.BoxExtents;
		s.SpeedMin      = c.SpeedMin;
		s.SpeedMax      = std::max(c.SpeedMax, c.SpeedMin);
		s.LifeMin       = std::max(c.LifeMin, 0.01f);
		s.LifeMax       = std::max(c.LifeMax, s.LifeMin);
		s.Gravity       = c.Gravity;
		s.Drag          = c.Drag;
		s.Wind          = c.Wind;
		s.SizeStart     = c.SizeStart;
		s.SizeEnd       = c.SizeEnd;
		s.ColorStart    = c.ColorStart;
		s.ColorEnd      = c.ColorEnd;
		s.Blend         = c.Blend;
		s.Space         = c.Space;
		s.FlipbookTilesX = static_cast<uint32_t>(std::max(c.FlipbookTilesX, 1));
		s.FlipbookTilesY = static_cast<uint32_t>(std::max(c.FlipbookTilesY, 1));
		s.FlipbookFps    = std::max(c.FlipbookFps, 0.0f);
		s.FlipbookBlend  = c.FlipbookBlend;
		s.SoftFadeDistance  = std::max(c.SoftFadeDistance, 0.0f);
		s.StretchByVelocity = std::max(c.StretchByVelocity, 0.0f);
		// spec.Texture left null — caller resolves TexturePath (GL) before Create.
		return s;
	}

	std::size_t EmitterRecipeSignature(const ParticleEmitterComponent& c)
	{
		std::size_t h = 0xF00DULL;
		HashI(h, static_cast<int64_t>(c.MaxParticles));
		HashF(h, c.SpawnRate);
		HashI(h, static_cast<int64_t>(c.Shape));
		HashF(h, c.ShapeRadius);
		HashF(h, c.ConeAngleDeg);
		HashV3(h, c.BoxExtents);
		HashF(h, c.SpeedMin); HashF(h, c.SpeedMax);
		HashF(h, c.LifeMin);  HashF(h, c.LifeMax);
		HashV3(h, c.Gravity);
		HashF(h, c.Drag);
		HashV3(h, c.Wind);
		HashF(h, c.SizeStart); HashF(h, c.SizeEnd);
		HashV4(h, c.ColorStart); HashV4(h, c.ColorEnd);
		HashI(h, static_cast<int64_t>(c.Blend));
		HashI(h, static_cast<int64_t>(c.Space));
		HashS(h, c.TexturePath);
		HashI(h, c.FlipbookTilesX); HashI(h, c.FlipbookTilesY);
		HashF(h, c.FlipbookFps);
		HashI(h, c.FlipbookBlend ? 1 : 0);
		HashF(h, c.SoftFadeDistance);
		HashF(h, c.StretchByVelocity);
		return h;
	}
}
