#pragma once

// Mesh.h
// Last Modified: 7/1/2026

/**
 * ============================================================================
 * COSMIC ENGINE — Mesh (first-class GPU geometry resource)
 * ============================================================================
 *
 * A Mesh owns a VertexArray holding indexed triangle geometry in the engine's
 * CANONICAL MESH VERTEX LAYOUT (binding on every mesh shader — see doc 05's
 * forward-compatibility contract):
 *
 *   location 0 : vec3 a_Position
 *   location 1 : vec3 a_Normal
 *   location 2 : vec2 a_TexCoord
 *
 * UVs are part of the layout from day one — even meshes that don't use
 * textures yet carry them, so the layout never has to be retrofitted when
 * materials/texturing arrive (S4).
 *
 * Meshes are factory-created Ref<> resources exactly like Texture2D/Shader:
 *
 *   Ref<Mesh> box  = Mesh::CreateBox({1, 0.25f, 2});
 *   Ref<Mesh> body = Mesh::CreateFromOBJ(FileSystem::Resolve("project://models/viper.obj"));
 *   Renderer3D::DrawMesh(box, transform, color);
 *
 * Primitives are unit-ish sized and centered on the origin so a plain scale
 * matrix resizes them predictably. All primitives have outward normals and
 * simple planar/cylindrical UVs.
 * ============================================================================
 */

#include "core/Core.h"
#include "graphics/VertexArray.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Cosmic
{
	// The canonical mesh vertex — matches the attribute layout above.
	struct MeshVertex
	{
		glm::vec3 Position;
		glm::vec3 Normal;
		glm::vec2 TexCoord;
	};

	class COSMIC_API Mesh
	{
	public:
		////////////////////////////////
		// Factory Pattern
		///////////////////////////////

		/**
		 * @brief Create a mesh from raw vertex/index data (uploads to the GPU).
		 * Pre:  indices.size() is a multiple of 3; every index < vertices.size().
		 * Post: Returns a live GPU mesh, or nullptr on empty input.
		 */
		static Ref<Mesh> Create(const std::vector<MeshVertex>& vertices,
		                        const std::vector<uint32_t>&   indices);

		// ---- Primitives (unit-sized, origin-centered, outward normals) ----

		/** @brief Axis-aligned box of the given full extents (default unit cube). */
		static Ref<Mesh> CreateBox(const glm::vec3& size = { 1.0f, 1.0f, 1.0f });

		/** @brief XZ ground plane (normal +Y), width along X, depth along Z. */
		static Ref<Mesh> CreatePlane(float width = 1.0f, float depth = 1.0f);

		/** @brief Y-axis cylinder: radius, full height, radial segment count. */
		static Ref<Mesh> CreateCylinder(float radius = 0.5f, float height = 1.0f, uint32_t segments = 24);

		/** @brief Y-axis cone: base at -height/2 (radius), apex at +height/2. */
		static Ref<Mesh> CreateCone(float radius = 0.5f, float height = 1.0f, uint32_t segments = 24);

		/** @brief Latitude/longitude sphere: rings = latitude bands, segments = longitude. */
		static Ref<Mesh> CreateUVSphere(float radius = 0.5f, uint32_t rings = 16, uint32_t segments = 24);

		/**
		 * @brief Load a Wavefront OBJ (positions + normals + uvs, faces triangulated).
		 *
		 * Supports: v / vt / vn records, f records with v, v/vt, v//vn, v/vt/vn
		 * (n-gon faces are fan-triangulated; negative/relative indices supported).
		 * Ignores materials, groups, and smoothing — this is the quick-primitives
		 * interchange path; glTF import is the planned full pipeline (doc 05, S4.4).
		 * Faces missing vn get a computed flat face normal.
		 *
		 * @param resolvedPath A real disk path — resolve "project://models/x.obj"
		 *                     with FileSystem::Resolve first.
		 * @return The mesh, or nullptr (with a logged error) on parse/IO failure.
		 */
		static Ref<Mesh> CreateFromOBJ(const std::string& resolvedPath);

		////////////////////////////////
		// Accessors
		///////////////////////////////

		const Ref<VertexArray>& GetVertexArray() const	{ return m_VertexArray; }
		uint32_t                GetVertexCount() const	{ return m_VertexCount; }
		uint32_t                GetIndexCount() const	{ return m_IndexCount; }

	public:
		// Public only so Ref<Mesh> construction via new works inside Create();
		// client code should always go through the factories above.
		Mesh(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices);
		~Mesh() = default;

	private:
		Ref<VertexArray> m_VertexArray;
		uint32_t         m_VertexCount = 0;
		uint32_t         m_IndexCount  = 0;
	};
}
