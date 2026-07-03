#pragma once

// Model.h
// Last Modified: 7/2/2026

/**
 * ============================================================================
 * COSMIC ENGINE — Model (glTF 2.0 import)  [S4.4b]
 * ============================================================================
 *
 * A Model is a small collection of ModelParts — each part is one triangle
 * primitive from a glTF file, baked into an engine Mesh (canonical
 * position/normal/uv layout) with the node's world transform folded into the
 * vertices, plus its material base color.
 *
 *   Ref<Model> duck = Model::CreateFromGLTF(FileSystem::Resolve("engine://models/Duck.glb"));
 *   Renderer3D::DrawModel(duck, transform);
 *
 * Or via the cache:  AssetLibrary::GetModel("engine://models/Duck.glb").
 *
 * FRAME: glTF is right-handed, +Y up, meters — that IS the render frame, so no
 * NED conversion happens here (Spatial.h is for sim STATE, not assets).
 *
 * SCOPE (S4.4b): positions, normals, UV0, and base-color factor. S6.2 extends
 * this: meshes now carry tangents (Mesh generates them), and import reads the
 * full glTF metallic-roughness material — factors AND textures (base color /
 * normal / metallic-roughness / occlusion / emissive, embedded or external) —
 * and builds a ready-to-draw PBR Material per part (PBR.glsl). Skins, animation,
 * and non-triangle primitives are still ignored (warned + skipped).
 * ============================================================================
 */

#include "core/Core.h"
#include "graphics/Mesh.h"
#include "graphics/Material.h"
#include "graphics/Texture.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Cosmic
{
	struct ModelPart
	{
		Ref<Mesh>   Geometry;
		glm::vec4   BaseColor{ 1.0f };   // glTF pbr_metallic_roughness.base_color_factor
		std::string Name;

		// --- glTF metallic-roughness material (S6.2) ---
		float     Metallic  = 1.0f;
		float     Roughness = 1.0f;
		glm::vec3 Emissive{ 0.0f };

		Ref<Texture2D> AlbedoMap;       // base color (sRGB)
		Ref<Texture2D> NormalMap;       // tangent-space normals
		Ref<Texture2D> MetalRoughMap;   // glTF: rough=G, metal=B
		Ref<Texture2D> AOMap;           // occlusion in R
		Ref<Texture2D> EmissiveMap;     // emissive (sRGB)

		// A PBR.glsl material with the factors + maps above already applied. Built
		// at import (null if the PBR shader failed to load); Renderer3D::DrawModel
		// uses it, falling back to the BaseColor Lambert path when null.
		Ref<Material> PbrMaterial;
	};

	class COSMIC_API Model
	{
	public:
		/**
		 * @brief Load a glTF/glb file into a Model (world transforms baked in).
		 * @param resolvedPath A real disk path (.gltf or .glb) — resolve VFS first.
		 * @return The model, or nullptr (with a logged error) on parse/IO failure.
		 */
		static Ref<Model> CreateFromGLTF(const std::string& resolvedPath);

		const std::vector<ModelPart>& GetParts() const { return m_Parts; }

	public:
		// Public so Ref<Model> construction via make_shared works inside the
		// factory; client code should always go through CreateFromGLTF.
		Model() = default;
		~Model() = default;

	private:
		std::vector<ModelPart> m_Parts;
	};
}
