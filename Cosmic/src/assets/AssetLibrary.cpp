// AssetLibrary.cpp — S4.4a path-keyed asset cache. See AssetLibrary.h.

#include "assets/AssetLibrary.h"

#include "graphics/Texture.h"
#include "graphics/Shader.h"
#include "graphics/Mesh.h"
#include "graphics/Model.h"
#include "utils/FileSystem.h"
#include "core/Log.h"

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
			[](const std::string& resolved) { return Mesh::CreateFromOBJ(resolved); });
	}

	Ref<Model> AssetLibrary::GetModel(const std::string& path)
	{
		return GetOrLoad<Model>(s_Models, path,
			[](const std::string& resolved) { return Model::CreateFromGLTF(resolved); });
	}

	void AssetLibrary::Clear()
	{
		s_Textures.clear();
		s_Shaders.clear();
		s_Meshes.clear();
		s_Models.clear();
	}
}
