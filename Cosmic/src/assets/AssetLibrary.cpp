// AssetLibrary.cpp — S4.4a path-keyed asset cache. See AssetLibrary.h.

#include "assets/AssetLibrary.h"

#include "graphics/Texture.h"
#include "graphics/Shader.h"
#include "graphics/Mesh.h"
#include "graphics/Material.h"
#include "graphics/MaterialAsset.h"
#include "scene/SceneSerializer.h"
#include "utils/FileSystem.h"
#include "core/Log.h"

#include <entt/entt.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_map>

namespace Cosmic
{
	namespace
	{
		// One cache per resource type. File-local statics — the .cpp owns all state.
		std::unordered_map<std::string, Ref<Texture2D>> s_Textures;
		std::unordered_map<std::string, Ref<Shader>>    s_Shaders;
		std::unordered_map<std::string, Ref<Material>>  s_Materials;

		// U3 — optional process-wide default sampling for freshly loaded textures
		// (the pixel-art preset). Off by default: the loader's own sampling wins.
		bool          s_SamplingOverride = false;
		TextureFilter s_SamplingFilter   = TextureFilter::Linear;
		TextureWrap   s_SamplingWrap     = TextureWrap::Repeat;

		// Create + (optionally) apply the default sampling — the ONE texture
		// loader GetTexture and Reload share.
		Ref<Texture2D> LoadTexture(const std::string& resolved)
		{
			Ref<Texture2D> tex = Texture2D::Create(resolved);
			if (tex && s_SamplingOverride)
				tex->SetSampling(s_SamplingFilter, s_SamplingWrap);
			return tex;
		}

		// Shared miss/hit logic: normalize → hit returns stored Ref; miss loads via
		// `loader` (given the RESOLVED disk path). A null load is logged once and NOT
		// cached (so a later call can retry), and returns null.
		template<typename T, typename Loader>
		Ref<T> GetOrLoad(std::unordered_map<std::string, Ref<T>>& cache,
		                 const std::string& path, Loader&& loader)
		{
			const std::string key = AssetLibrary::NormalizeKey(path);

			auto it = cache.find(key);
			if (it != cache.end())
				return it->second;

			Ref<T> asset = loader(FileSystem::Resolve(path));
			if (!asset)
			{
				CS_CORE_ERROR("AssetLibrary: failed to load '{}'", path);
				return nullptr; // don't cache the failure — retry on next request
			}

			cache.emplace(key, asset);
			return asset;
		}
	}

	std::string AssetLibrary::NormalizeKey(const std::string& path)
	{
		return std::filesystem::path(FileSystem::Resolve(path))
		           .lexically_normal()
		           .generic_string();
	}

	Ref<Texture2D> AssetLibrary::GetTexture(const std::string& path)
	{
		return GetOrLoad<Texture2D>(s_Textures, path,
			[](const std::string& resolved) { return LoadTexture(resolved); });
	}

	void AssetLibrary::SetDefaultTextureSampling(TextureFilter filter, TextureWrap wrap)
	{
		s_SamplingOverride = true;
		s_SamplingFilter   = filter;
		s_SamplingWrap     = wrap;
	}

	void AssetLibrary::ClearDefaultTextureSampling()
	{
		s_SamplingOverride = false;
	}

	Ref<Shader> AssetLibrary::GetShader(const std::string& path)
	{
		return GetOrLoad<Shader>(s_Shaders, path,
			[](const std::string& resolved) { return Shader::Create(resolved); });
	}

	Ref<Material> AssetLibrary::BuildMaterial(const MaterialAsset& a, const std::string& name)
	{
		Ref<Shader> pbr = GetShader("engine://shaders/PBR.glsl");
		if (!pbr)
			return nullptr;

		Ref<Material> m = Material::Create(pbr, name);

		// A2 — every built material carries the skinned twin so an Animator can
		// drive any .cmat'd mesh. Static draws never touch it (null-safe: a
		// failed shader load just means bind-pose rendering).
		if (Ref<Shader> skinned = GetShader("engine://shaders/PBRSkinned.glsl"))
			m->SetSkinnedShader(skinned);
		m->Set("u_Albedo",    a.Albedo);
		m->Set("u_Metallic",  a.Metallic);
		m->Set("u_Roughness", a.Roughness);
		m->Set("u_AO",        a.AO);
		m->Set("u_Emissive",  a.Emissive);
		m->SetTransparent(a.Transparent);

		auto setMap = [&](const std::string& p, const char* mapU, const char* hasU)
		{
			Ref<Texture2D> t = p.empty() ? nullptr : GetTexture(p);
			if (t) { m->Set(mapU, t); m->Set(hasU, 1.0f); }
			else   { m->Set(hasU, 0.0f); }
		};
		setMap(a.AlbedoMap,     "u_AlbedoMap",     "u_HasAlbedoMap");
		setMap(a.NormalMap,     "u_NormalMap",     "u_HasNormalMap");
		setMap(a.MetalRoughMap, "u_MetalRoughMap", "u_HasMetalRoughMap");
		setMap(a.AOMap,         "u_AOMap",         "u_HasAOMap");
		setMap(a.EmissiveMap,   "u_EmissiveMap",   "u_HasEmissiveMap");
		return m;
	}

	bool AssetLibrary::LoadMaterialAsset(MaterialAsset& out, const std::string& path)
	{
		return SceneSerializer::LoadReflectedFromFile(
			entt::type_hash<MaterialAsset>::value(), &out, FileSystem::Resolve(path));
	}

	bool AssetLibrary::SaveMaterialAsset(const MaterialAsset& asset, const std::string& path)
	{
		return SceneSerializer::SaveReflectedToFile(
			entt::type_hash<MaterialAsset>::value(), &asset, FileSystem::Resolve(path));
	}

	Ref<Material> AssetLibrary::GetMaterial(const std::string& path)
	{
		return GetOrLoad<Material>(s_Materials, path,
			[&path](const std::string& resolved) -> Ref<Material>
			{
				MaterialAsset asset;
				if (!SceneSerializer::LoadReflectedFromFile(
						entt::type_hash<MaterialAsset>::value(), &asset, resolved))
					return nullptr;
				return BuildMaterial(asset, path);
			});
	}

	void AssetLibrary::Enumerate(const std::function<void(const AssetEntry&)>& visitor)
	{
		if (!visitor)
			return;

		for (const auto& [key, tex] : s_Textures)
		{
			AssetEntry e;
			e.Path = key; e.Type = AssetType::Texture;
			e.Refs = tex.use_count();
			e.GpuBytes = tex ? tex->GetGpuBytes() : 0;
			visitor(e);
		}
		for (const auto& [key, sh] : s_Shaders)
		{
			AssetEntry e;
			e.Path = key; e.Type = AssetType::Shader;
			e.Refs = sh.use_count();
			visitor(e);   // shader binary size is not tracked → bytes 0
		}
		for (const auto& [key, mat] : s_Materials)
		{
			AssetEntry e;
			e.Path = key; e.Type = AssetType::Material;
			e.Refs = mat.use_count();
			visitor(e);   // a material's textures are counted under s_Textures
		}
	}

	void AssetLibrary::Clear()
	{
		s_Textures.clear();
		s_Shaders.clear();
		s_Materials.clear();
	}

	bool AssetLibrary::Reload(const std::string& path)
	{
		const std::string key = NormalizeKey(path);
		bool evicted = false;

		// Texture: evict, then eagerly reload so the refreshed image is ready for
		// the next GetTexture (E10 hot reload).
		if (auto it = s_Textures.find(key); it != s_Textures.end())
		{
			s_Textures.erase(it);
			evicted = true;
			if (Ref<Texture2D> fresh = LoadTexture(FileSystem::Resolve(path)))
				s_Textures.emplace(key, fresh);
		}

		// Other resource types: just evict so the next Get* reloads on demand.
		if (auto it = s_Shaders.find(key);   it != s_Shaders.end())   { s_Shaders.erase(it);   evicted = true; }
		if (auto it = s_Materials.find(key); it != s_Materials.end()) { s_Materials.erase(it); evicted = true; }

		return evicted;
	}
}
