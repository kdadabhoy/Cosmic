// AssetLibrary.cpp — S4.4a path-keyed asset cache. See AssetLibrary.h.

#include "assets/AssetLibrary.h"

#include "graphics/Texture.h"
#include "graphics/Shader.h"
#include "graphics/Mesh.h"
#include "graphics/Model.h"
#include "graphics/Material.h"
#include "graphics/MaterialAsset.h"
#include "graphics/AnimationClip.h"
#include "assets/MeshImport.h"
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
		std::unordered_map<std::string, Ref<Mesh>>      s_Meshes;
		std::unordered_map<std::string, Ref<Model>>     s_Models;
		std::unordered_map<std::string, Ref<Material>>  s_Materials;

		// A2 — a model file's whole clip set, keyed by the BASE path (no
		// fragment); GetAnimationClip aliases Refs into the vector's elements.
		std::unordered_map<std::string, Ref<std::vector<AnimationClip>>> s_ClipSets;

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

	Ref<Mesh> AssetLibrary::GetMesh(const std::string& path)
	{
		// Sub-mesh fragment ("models/gun.fbx#2" — A1 multi-mesh children): the
		// full fragment path is the cache key (each sub-mesh is its own slot),
		// but the FILE the importer opens — and the .cmeta that governs it —
		// is the base path before the '#'.
		std::string base;
		int         submesh = -1;
		if (MeshImport::SplitSubmeshPath(path, base, submesh))
		{
			return GetOrLoad<Mesh>(s_Meshes, path,
				[&base, submesh](const std::string&) -> Ref<Mesh>
				{
					const std::string resolved = FileSystem::Resolve(base);
					return MeshImport::Import(resolved, MeshImport::LoadOrInitMeta(resolved), submesh);
				});
		}

		return GetOrLoad<Mesh>(s_Meshes, path,
			[](const std::string& resolved) -> Ref<Mesh>
			{
				// Route model formats through the E16 importer so the source's
				// units/up-axis (its .cmeta) are applied — since A1 that includes
				// glTF/GLB (cgltf-merged), so MeshPath drops of .glb files work.
				// AssetLibrary::GetModel remains the dedicated per-part glTF path.
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

	namespace
	{
		// The cached clip set of a model file (parse-once). Null on parse
		// failure (NOT cached, so a later call can retry); an empty set IS
		// cached — "this file has no clips" is a valid answer.
		Ref<std::vector<AnimationClip>> LoadClipSet(const std::string& basePath)
		{
			const std::string key = AssetLibrary::NormalizeKey(basePath);
			if (auto it = s_ClipSets.find(key); it != s_ClipSets.end())
				return it->second;

			const std::string resolved = FileSystem::Resolve(basePath);
			ImportedModelDesc desc;
			if (!MeshImport::ImportModelData(desc, resolved, MeshImport::LoadOrInitMeta(resolved)))
			{
				CS_CORE_ERROR("AssetLibrary: failed to read clips from '{}'", basePath);
				return nullptr;
			}

			auto set = std::make_shared<std::vector<AnimationClip>>(std::move(desc.Clips));
			s_ClipSets.emplace(key, set);
			return set;
		}

		// Split "base#frag"; false when there is no fragment.
		bool SplitClipFragment(const std::string& path, std::string& base, std::string& frag)
		{
			const size_t hash = path.find_last_of('#');
			if (hash == std::string::npos)
				return false;
			base = path.substr(0, hash);
			frag = path.substr(hash + 1);
			return true;
		}
	}

	Ref<AnimationClip> AssetLibrary::GetAnimationClip(const std::string& path)
	{
		std::string base = path, frag;
		SplitClipFragment(path, base, frag);

		Ref<std::vector<AnimationClip>> set = LoadClipSet(base);
		if (!set || set->empty())
		{
			if (set)
				CS_CORE_WARN("AssetLibrary: '{}' contains no animation clips.", base);
			return nullptr;
		}

		size_t index = 0;
		if (!frag.empty())
		{
			const bool numeric = std::all_of(frag.begin(), frag.end(),
				[](unsigned char c) { return std::isdigit(c); });
			if (numeric)
			{
				index = (size_t)std::stoul(frag);
			}
			else
			{
				index = set->size();   // not-found sentinel
				for (size_t i = 0; i < set->size(); ++i)
					if ((*set)[i].Name == frag)
					{
						index = i;
						break;
					}
			}
			if (index >= set->size())
			{
				CS_CORE_WARN("AssetLibrary: clip '{}' not found in '{}' ({} clip(s)).",
				             frag, base, set->size());
				return nullptr;
			}
		}

		// Alias into the cached set — the vector stays alive with the Ref.
		return Ref<AnimationClip>(set, &(*set)[index]);
	}

	std::vector<std::string> AssetLibrary::GetAnimationClipNames(const std::string& path)
	{
		std::string base = path, frag;
		SplitClipFragment(path, base, frag);

		std::vector<std::string> names;
		if (Ref<std::vector<AnimationClip>> set = LoadClipSet(base))
			for (const AnimationClip& c : *set)
				names.push_back(c.Name);
		return names;
	}

	void AssetLibrary::Clear()
	{
		s_Textures.clear();
		s_Shaders.clear();
		s_Meshes.clear();
		s_Models.clear();
		s_Materials.clear();
		s_ClipSets.clear();
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
		if (auto it = s_Meshes.find(key);    it != s_Meshes.end())    { s_Meshes.erase(it);    evicted = true; }
		if (auto it = s_Models.find(key);    it != s_Models.end())    { s_Models.erase(it);    evicted = true; }
		if (auto it = s_Materials.find(key); it != s_Materials.end()) { s_Materials.erase(it); evicted = true; }

		// Sub-mesh entries ("<key>#N" — A1) share the base file: evict them with
		// it so a reimport refreshes every child of a multi-mesh source.
		for (auto it = s_Meshes.begin(); it != s_Meshes.end(); )
		{
			const std::string& k = it->first;
			if (k.size() > key.size() + 1 && k.compare(0, key.size(), key) == 0 && k[key.size()] == '#')
			{
				it = s_Meshes.erase(it);
				evicted = true;
			}
			else
				++it;
		}

		// Clip sets are keyed by the base file (A2).
		if (auto it = s_ClipSets.find(key); it != s_ClipSets.end())
		{
			s_ClipSets.erase(it);
			evicted = true;
		}

		return evicted;
	}
}
