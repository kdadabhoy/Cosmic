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
 *   location 3 : vec4 a_Tangent   (xyz = tangent, w = handedness sign; S6.2)
 *
 * UVs are part of the layout from day one — even meshes that don't use
 * textures yet carry them, so the layout never has to be retrofitted when
 * materials/texturing arrive (S4). Tangents (S6.2) are the additive layout
 * extension the forward-compat contract (rule 3) anticipated: every factory
 * generates them automatically (see ComputeTangents in Mesh.cpp) so normal
 * mapping in PBR.glsl has a consistent TBN basis, and meshes that don't sample
 * a normal map simply ignore attribute 3.
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
	class Skeleton;   // graphics/Skeleton.h (A2) — carried by skinned meshes

	// The canonical mesh vertex — matches the attribute layout above.
	// Tangent (S6.2): xyz is the surface tangent aligned to +U; w is the
	// bitangent handedness (+1 or -1) so the shader reconstructs
	// bitangent = w * cross(normal, tangent). Filled by Mesh::Create if left at
	// its default, so every producer (primitives / OBJ / glTF) gets a basis.
	struct MeshVertex
	{
		glm::vec3 Position;
		glm::vec3 Normal;
		glm::vec2 TexCoord;
		glm::vec4 Tangent{ 1.0f, 0.0f, 0.0f, 1.0f };
	};

	/**
	 * @brief CPU-side geometry (positions/normals/uvs + indices) with NO GPU
	 * resources — the headless-testable half of a mesh (E15). Every parametric
	 * primitive is produced by a pure Mesh::Build* function returning one of
	 * these; Mesh::Create(MeshData) uploads it. Splitting geometry generation
	 * from the GPU upload mirrors Terrain (CPU heights vs. GPU texture) so unit
	 * tests can assert vertex counts / bounds / normals without a GL context,
	 * and lets importers (E16) hand raw geometry straight to the uploader.
	 */
	// Per-vertex skinning influences (A2) — a PARALLEL array to MeshData's
	// Vertices, kept out of the canonical MeshVertex so every existing mesh's
	// layout and memory stay byte-identical. Joints are joint-array indices
	// stored as floats: the VAO layer's attribute pointers are float-typed, and
	// float32 is exact for any realistic joint count (< 2^24) — PBRSkinned.glsl
	// rounds back to int.
	struct SkinVertex
	{
		glm::vec4 Joints{ 0.0f };    // 4 joint indices (as floats)
		glm::vec4 Weights{ 0.0f };   // matching blend weights (renormalized in-shader)
	};

	struct MeshData
	{
		std::vector<MeshVertex> Vertices;
		std::vector<uint32_t>   Indices;

		/**
		 * @brief Bake a model matrix into the geometry: positions by `transform`,
		 * normals/tangents by its normal matrix (inverse-transpose), renormalised.
		 * Used by the importer (E16) to apply CAD unit scale + up-axis conversion
		 * so a placed model lands at the correct world size regardless of source.
		 * Inline (header-only) so it links across DLLs without exporting MeshData.
		 */
		void ApplyTransform(const glm::mat4& transform)
		{
			const glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(transform)));
			for (MeshVertex& v : Vertices)
			{
				v.Position = glm::vec3(transform * glm::vec4(v.Position, 1.0f));
				v.Normal   = glm::normalize(normalMat * v.Normal);
				glm::vec3 t = normalMat * glm::vec3(v.Tangent);
				const float len = glm::length(t);
				if (len > 1e-8f)
					v.Tangent = glm::vec4(t / len, v.Tangent.w);
			}
		}
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

		/** @brief Upload CPU geometry produced by a Build* function (or an importer). */
		static Ref<Mesh> Create(const MeshData& data);

		/**
		 * @brief Upload SKINNED geometry (A2): the canonical layout plus a second
		 * vertex buffer carrying joints/weights at attribute locations 4/5
		 * (PBRSkinned.glsl / ShadowDepthSkinned.glsl read them; static shaders
		 * simply never enable those attributes' consumption). `skin` must be
		 * vertex-parallel to `data.Vertices`; `skeleton` rides along for the
		 * animator's palette math. Falls back to a static mesh (warned) when the
		 * sizes disagree.
		 */
		static Ref<Mesh> CreateSkinned(const MeshData& data, const std::vector<SkinVertex>& skin,
		                               const Ref<Skeleton>& skeleton);

		// ---- Parametric geometry (pure, GL-free, headless-testable — E15) ----
		//
		// The Build* functions generate CANONICAL primitive geometry on the CPU;
		// the matching Create* factory just uploads Build*'s result. Tests assert
		// vertex/index counts, local bounds and normals directly on the MeshData.

		static MeshData BuildBox(const glm::vec3& size = { 1.0f, 1.0f, 1.0f });
		static MeshData BuildPlane(float width = 1.0f, float depth = 1.0f);
		static MeshData BuildCylinder(float radius = 0.5f, float height = 1.0f, uint32_t segments = 24);
		static MeshData BuildCone(float radius = 0.5f, float height = 1.0f, uint32_t segments = 24);
		static MeshData BuildUVSphere(float radius = 0.5f, uint32_t rings = 16, uint32_t segments = 24);
		/** @brief Torus in the XZ plane: `radius` = ring (major) radius, `tubeRadius`
		 *  = tube (minor) radius; `segments` steps around the ring, `sides` around
		 *  the tube. Outward normals point away from the tube's centre circle. */
		static MeshData BuildTorus(float radius = 0.5f, float tubeRadius = 0.2f,
		                           uint32_t segments = 32, uint32_t sides = 16);

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

		/** @brief Torus (donut) in the XZ plane; see BuildTorus for the parameters. */
		static Ref<Mesh> CreateTorus(float radius = 0.5f, float tubeRadius = 0.2f,
		                             uint32_t segments = 32, uint32_t sides = 16);

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

		/** @brief Pure (GL-free) Wavefront OBJ parse → CPU geometry; empty on failure.
		 *  The importer (E16) uses this so it can transform the geometry before
		 *  upload; CreateFromOBJ is the thin GPU wrapper over it. */
		static MeshData BuildFromOBJ(const std::string& resolvedPath);

		////////////////////////////////
		// Accessors
		///////////////////////////////

		const Ref<VertexArray>& GetVertexArray() const	{ return m_VertexArray; }
		uint32_t                GetVertexCount() const	{ return m_VertexCount; }
		uint32_t                GetIndexCount() const	{ return m_IndexCount; }

		/**
		 * @brief Estimated GPU memory of this mesh's buffers (T2 asset accounting):
		 * the canonical vertex buffer + the index buffer, plus the parallel skin
		 * buffer for skinned meshes (A2). A figure for the Resources panel /
		 * status bar (§13.3 / §1.4), not an exact driver allocation.
		 */
		uint64_t GetGpuBytes() const
		{
			uint64_t bytes = (uint64_t)m_VertexCount * sizeof(MeshVertex)
			               + (uint64_t)m_IndexCount  * sizeof(uint32_t);
			if (m_Skeleton)
				bytes += (uint64_t)m_VertexCount * sizeof(SkinVertex);
			return bytes;
		}

		/** @brief Skinned meshes (A2) carry their joint hierarchy; null otherwise. */
		bool                    IsSkinned() const       { return (bool)m_Skeleton; }
		const Ref<Skeleton>&    GetSkeleton() const     { return m_Skeleton; }

		////////////////////////////////
		// Local-space Bounds (AABB)
		///////////////////////////////

		/**
		 * @brief Axis-aligned bounding box in the mesh's LOCAL space, computed once
		 * at construction from the vertex positions.
		 *
		 * Consumers: frame-to-fit navigation (S5.2), selection outlines and CPU
		 * picking (S5.4), and frustum culling (S12) — transform the 8 corners by an
		 * entity's model matrix to get its world AABB. An empty mesh reports a
		 * degenerate box at the origin (min == max == 0).
		 */
		const glm::vec3& GetLocalMin() const	{ return m_LocalMin; }
		const glm::vec3& GetLocalMax() const	{ return m_LocalMax; }
		glm::vec3        GetLocalCenter() const	{ return 0.5f * (m_LocalMin + m_LocalMax); }

	public:
		// Public only so Ref<Mesh> construction via new works inside Create();
		// client code should always go through the factories above.
		Mesh(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices);
		~Mesh() = default;

	private:
		Ref<VertexArray> m_VertexArray;
		uint32_t         m_VertexCount = 0;
		uint32_t         m_IndexCount  = 0;

		// Local-space bounds (see GetLocalMin/Max). Filled in the constructor.
		glm::vec3        m_LocalMin{ 0.0f };
		glm::vec3        m_LocalMax{ 0.0f };

		// A2 — set only by CreateSkinned (bind-pose bounds; the palette can move
		// geometry outside them — the animator pads culling accordingly).
		Ref<Skeleton>    m_Skeleton;
	};
}
