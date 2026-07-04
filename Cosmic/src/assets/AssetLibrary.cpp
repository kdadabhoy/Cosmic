// AssetLibrary.cpp — S4.4a path-keyed asset cache. See AssetLibrary.h.

#include "assets/AssetLibrary.h"

#include "graphics/Texture.h"
#include "graphics/Shader.h"
#include "graphics/Mesh.h"
#include "graphics/Model.h"
#include "graphics/Material.h"
#include "graphics/MaterialAsset.h"
#include "assets/MeshImport.h"
#include "scene/SceneSerializer.h"
#include "utils/FileSystem.h"
#include "core/Log.h"

#include <entt/entt.hpp>
#include <filesystem>
#include <unordered_map>

namespace Cosmic
{
	namespace
	{
		// One cache per resource type. File-local statics — the .cpp owns all state.
		std::unordered_map<std::string, Ref<Texture2D>> s_Textures;
		std::unordered_map<std::string, Ref<Shader>>    s_Shaders;
		std::unordered_map<std::string, Ref<Mesh>>      s_Meshes;
		std::unordered_map<std::string, Ref<Model>>     s_Models;
		std::unordered_map<std::string, Ref<Material>>  s_Materials;

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
			[](const std::string& resolved) { return Texture2D::Create(resolved); });
	}

	Ref<Shader> AssetLibrary::GetShader(const std::string& path)
	{
		return GetOrLoad<Shader>(s_Shaders, path,
			[](const std::string& resolved) { return Shader::Create(resolved); });
	}

	Ref<Mesh> AssetLibrary::GetMesh(const std::string& path)
	{
		return GetOrLoad<Mesh>(s_Meshes, path,
			[](const std::string& resolved) -> Ref<Mesh>
			{
				// Route single-mesh formats through the E16 importer so the source's
				// units/up-axis (its .cmeta) are applied; OBJ handled either way.
				// glTF stays on the dedicated Model path (AssetLibrary::GetModel).
				const std::string ext = MeshImport::Extension(resolved);
				if (MeshImport::Supports(ext))
					return MeshImport::Import(resolved, MeshImport::LoadOrInitMeta(resolved));
				return Mesh::CreateFromOBJ(resolved);   // legacy fallback (also OBJ)
			});
	}

	Ref<Model> AssetLibrary::GetModel(const std::string& path)
	{
		return GetOrLoad<Model>(s_Models, path,
			[](const std::string& resolved) { return Model::CreateFromGLTF(resolved); });
	}

	Ref<Material> AssetLibrary::BuildMaterial(const MaterialAsset& a, const std::string& name)
	{
		Ref<Shader> pbr = GetShader("engine://shaders/PBR.glsl");
		if (!pbr)
			return nullptr;

		Ref<Material> m = Material::Create(pbr, name);
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

	void AssetLibrary::Clear()
	{
		s_Textures.clear();
		s_Shaders.clear();
		s_Meshes.clear();
		s_Models.clear();
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
			if (Ref<Texture2D> fresh = Texture2D::Create(FileSystem::Resolve(path)))
				s_Textures.emplace(key, fresh);
		}

		// Other resource types: just evict so the next Get* reloads on demand.
		if (auto it = s_Shaders.find(key);   it != s_Shaders.end())   { s_Shaders.erase(it);   evicted = true; }
		if (auto it = s_Meshes.find(key);    it != s_Meshes.end())    { s_Meshes.erase(it);    evicted = true; }
		if (auto it = s_Models.find(key);    it != s_Models.end())    { s_Models.erase(it);    evicted = true; }
		if (auto it = s_Materials.find(key); it != s_Materials.end()) { s_Materials.erase(it); evicted = true; }

		return evicted;
	}
}
