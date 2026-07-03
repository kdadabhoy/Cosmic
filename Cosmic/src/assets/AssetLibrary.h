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

	class COSMIC_API AssetLibrary
	{
	public:
		/** @brief Cached texture (VFS or raw path). Miss loads via Texture2D::Create. */
		static Ref<Texture2D> GetTexture(const std::string& path);

		/** @brief Cached shader. Miss loads via Shader::Create. */
		static Ref<Shader>    GetShader(const std::string& path);

		/** @brief Cached mesh (.obj). Miss loads via Mesh::CreateFromOBJ. */
		static Ref<Mesh>      GetMesh(const std::string& path);

		/** @brief Cached model (.gltf/.glb). Miss loads via Model::CreateFromGLTF (S4.4b). */
		static Ref<Model>     GetModel(const std::string& path);

		/** @brief Release all cached Refs. Call while a GL context is still current. */
		static void           Clear();

		/**
		 * @brief Canonical cache key for a path — FileSystem::Resolve then
		 * lexically_normal().generic_string(). Public for tests. No disk I/O.
		 */
		static std::string    NormalizeKey(const std::string& path);
	};
}
