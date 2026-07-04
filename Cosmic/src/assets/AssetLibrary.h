#pragma once

// AssetLibrary.h
// Last Modified: 7/2/2026

/**
 * ============================================================================
 * COSMIC ENGINE — AssetLibrary (path-keyed asset cache)  [S4.4a]
 * ============================================================================
 *
 * A process-wide cache that hands out shared Ref<> handles to loaded assets so
 * the same file is only uploaded to the GPU once. Ask for an asset by path;
 * a cache HIT returns the already-loaded Ref, a MISS loads it, stores it, and
 * returns it (closes IMPROVEMENTS §5.1).
 *
 *   auto tex   = AssetLibrary::GetTexture("engine://textures/grid.png");
 *   auto mesh  = AssetLibrary::GetMesh("project://models/rover.obj");
 *   auto model = AssetLibrary::GetModel("engine://models/Duck.glb"); // S4.4b
 *
 * KEYING: paths are normalized (NormalizeKey) before lookup, so VFS and raw
 * spellings of the same file share one slot:
 *   engine://models/duck.glb  ==  assets/models/../models/duck.glb
 * Normalization is purely lexical (no disk I/O) → headless-testable.
 *
 * LIFETIME: Clear() releases every cached Ref. It MUST run while a live GL
 * context still exists (Application shutdown, before context teardown) — the
 * cached GPU resources delete their handles in their destructors.
 *
 * THREADING: main-thread only (matches the factories and FileSystem::Resolve
 * for project:// paths).
 * ============================================================================
 */

#include "core/Core.h"
#include <string>

namespace Cosmic
{
	class Texture2D;
	class Shader;
	class Mesh;
	class Model; // defined in S4.4b; Ref<Model> is fine on an incomplete type
	class Material;         // E17
	struct MaterialAsset;   // E17 — graphics/MaterialAsset.h

	class COSMIC_API AssetLibrary
	{
	public:
		/** @brief Cached texture (VFS or raw path). Miss loads via Texture2D::Create. */
		static Ref<Texture2D> GetTexture(const std::string& path);

		/** @brief Cached shader. Miss loads via Shader::Create. */
		static Ref<Shader>    GetShader(const std::string& path);

		/** @brief Cached mesh. Miss imports via MeshImport (OBJ + gated assimp), applying .cmeta. */
		static Ref<Mesh>      GetMesh(const std::string& path);

		/** @brief Cached model (.gltf/.glb). Miss loads via Model::CreateFromGLTF (S4.4b). */
		static Ref<Model>     GetModel(const std::string& path);

		/** @brief Cached material (E17). Miss loads the `.cmat` MaterialAsset and
		 *  binds it to the engine PBR shader (BuildMaterial). */
		static Ref<Material>  GetMaterial(const std::string& path);

		/** @brief Build a live PBR Ref<Material> from a MaterialAsset (no caching /
		 *  file I/O) — used for live previews and the Material Editor. */
		static Ref<Material>  BuildMaterial(const MaterialAsset& asset, const std::string& name);

		/** @brief Load / save a `.cmat` MaterialAsset (paths go through the VFS).
		 *  Thin typed wrappers over SceneSerializer's reflected-struct (de)serializer. */
		static bool           LoadMaterialAsset(MaterialAsset& out, const std::string& path);
		static bool           SaveMaterialAsset(const MaterialAsset& asset, const std::string& path);

		/** @brief Release all cached Refs. Call while a GL context is still current. */
		static void           Clear();

		/**
		 * @brief Drop the cached entry for `path` and, if it was a texture,
		 * re-load it from disk into the cache (E10 hot reload). Returns true when
		 * something was evicted. The next GetTexture(path) returns the refreshed
		 * image — the content browser re-queries per frame so its thumbnail
		 * updates within one FileWatcher poll. NOTE: Refs already handed out
		 * (e.g. a material sampling the old texture) keep the previous GPU handle;
		 * in-place re-upload into existing Refs is a documented follow-up.
		 */
		static bool           Reload(const std::string& path);

		/**
		 * @brief Canonical cache key for a path — FileSystem::Resolve then
		 * lexically_normal().generic_string(). Public for tests. No disk I/O.
		 */
		static std::string    NormalizeKey(const std::string& path);
	};
}
