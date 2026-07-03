#pragma once

// Terrain.h
// Last Modified: 7/3/2026

/**
 * ============================================================================
 * COSMIC ENGINE — Terrain (heightmap terrain system)  [S8]
 * ============================================================================
 *
 * A heightmap-based terrain: a CPU heightfield (the source of truth for
 * queries) plus a chunked-quadtree LOD renderer that draws one shared patch
 * mesh per visible node, displaced in the vertex shader from a packed
 * height+normal texture. Skirts hide LOD cracks (chosen over stitching for
 * simplicity — doc 05 S8.1); CPU-generated normals are baked at load.
 *
 *   S8.1  Chunked quadtree + distance LOD + skirts      (Terrain.glsl)
 *   S8.2  4 splat layers, auto-splat by height/slope,
 *         triplanar projection on steep slopes           (parameterized)
 *   S8.3  SampleHeight / SampleNormal CPU queries        (unit-tested)
 *
 * SOURCES: a grayscale heightmap image (8/16-bit; 16-bit preserved) or a
 * seeded fBm procedural field (math/Noise.h, E14). Both normalize to [0, 1]
 * and scale by HeightScale.
 *
 * WIRING (inside a Renderer3D scene — the camera UBO must be live):
 *
 *   m_Terrain = Terrain::Create(spec);            // CPU-only; safe headless
 *   ...
 *   Renderer3D::BeginScene(camera);
 *   m_Terrain->Render(camera.GetPosition());      // lazily creates GPU state
 *   ...
 *   float ground = m_Terrain->SampleHeight(x, z); // sim/physics ground truth
 *
 * The renderer consumes the engine scene conventions: LightsBlock (binding 0),
 * CameraBlock (binding 1), and the IBL/shadow set via
 * Renderer3D::ApplySceneBindings — terrain receives sun shadows and IBL
 * ambient exactly like material meshes. CPU queries never touch the GPU, so
 * Terrain is constructible in headless tests.
 * ============================================================================
 */

#include "core/Core.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Cosmic
{
	class Mesh;
	class Shader;
	class Texture2D;

	/** One splat layer (S8.2). A null Albedo uses the shared procedural detail
	 *  texture; Color multiplies whichever texture is bound. */
	struct TerrainLayer
	{
		glm::vec3      Color{ 0.5f };
		float          Tiling = 0.25f;        // texture repeats per world meter
		Ref<Texture2D> Albedo;                // optional; null -> procedural detail
	};

	/** Auto-splat + triplanar tuning (S8.2). Parameterized — scenario values
	 *  (snow line, rock slope) belong to the app, not constants in the shader. */
	struct TerrainMaterialParams
	{
		float SlopeRockThreshold = 0.72f;   // normal.y below this blends to the slope layer
		float SlopeBlend         = 0.10f;   // smoothstep half-width around the threshold
		float HighHeight         = 30.0f;   // world Y where the high layer (snow) fades in
		float HighBlend          = 6.0f;
		float LowHeight          = 1.5f;    // world Y where the low layer (sand/ash) fades out
		float LowBlend           = 1.5f;
		float TriplanarSharpness = 4.0f;    // blend-weight exponent on steep slopes

		// --- Wet band (S11.1 / doc 10 F4): darken the surface at/below a waterline
		//     (wet sand at the beach, lakeshore mud). WetDarken = 0 (default) is a
		//     no-op — the shipped S8 look is byte-identical until an app feeds it. ---
		float WetLine   = 0.0f;             // world Y at/below which the surface reads wet
		float WetBand   = 1.5f;             // fade band above the line (m)
		float WetDarken = 0.0f;             // 0 = off; 1 = full wet darkening
	};

	struct TerrainSpecification
	{
		// --- Heightfield shape ---
		uint32_t Resolution  = 513;         // vertices per side; MUST be (64 * 2^k) + 1
		float    WorldSize   = 512.0f;      // meters along X and Z
		float    HeightScale = 60.0f;       // world height of a 1.0 sample
		float    BaseHeight  = 0.0f;        // world Y of a 0.0 sample
		glm::vec2 Origin{ 0.0f };           // world XZ center of the terrain

		// --- Source A: heightmap image (grayscale; 16-bit PNG preserved) ---
		std::string HeightmapPath;          // empty -> procedural fBm below

		// --- Source B: procedural fBm (math/Noise.h) ---
		uint32_t Seed       = 1337;
		int      Octaves    = 6;
		float    Frequency  = 3.0f;         // fBm periods across the terrain
		float    Lacunarity = 2.0f;
		float    Gain       = 0.5f;
		float    EdgeFalloff = 0.0f;        // 0 = none; else fraction of half-size faded to 0 (island)

		// --- Source C: an app-supplied heightfield function (S11.1 / doc 10 F4) ---
		// When set it WINS over HeightmapPath/fBm: sampled at every heightfield
		// texel with u,v in [0,1] (texel centers, exactly as the fBm path samples),
		// its return clamped to [0,1]. Must be deterministic — SampleHeight's unit
		// tests apply unchanged. The Frontier island composer (F11) plugs in here.
		std::function<float(float u, float v)> HeightFunction;

		// --- LOD ---
		float LodDistanceFactor = 2.5f;     // split a node when camera dist < size * factor
		float SkirtDepth        = 2.0f;     // meters of downward skirt per patch

		// --- Material (S8.2): 0 = base, 1 = slope, 2 = high, 3 = low ---
		TerrainLayer          Layers[4] = {
			{ { 0.24f, 0.38f, 0.15f }, 0.35f, nullptr },   // grass
			{ { 0.36f, 0.33f, 0.31f }, 0.50f, nullptr },   // rock
			{ { 0.92f, 0.94f, 0.98f }, 0.30f, nullptr },   // snow
			{ { 0.55f, 0.48f, 0.36f }, 0.45f, nullptr },   // sand / ash
		};
		TerrainMaterialParams Material;
	};

	class COSMIC_API Terrain
	{
	public:
		/**
		 * @brief Build the CPU heightfield (image or procedural) and normals.
		 * GPU resources are created lazily on first Render, so Create is safe
		 * without a GL context (headless tests use the queries only).
		 * @return The terrain, or nullptr on a bad spec / unreadable image.
		 */
		static Ref<Terrain> Create(const TerrainSpecification& spec);

		~Terrain();

		// GPU resource owner with lazy init — copying would alias ownership.
		Terrain(const Terrain&)            = delete;
		Terrain& operator=(const Terrain&) = delete;

		/**
		 * @brief Draw the terrain with quadtree LOD around `cameraPos`.
		 * Call between Renderer3D::BeginScene/EndScene (camera UBO live). Applies
		 * the scene IBL/shadow bindings; restores no global render state because
		 * it changes none (depth defaults, no blending).
		 */
		void Render(const glm::vec3& cameraPos, int entityID = -1);

		/**
		 * @brief Depth-only draw for shadow casting (S11.1 / doc 10 F4). Walks the
		 * SAME quadtree cut as Render(cameraPos) — so caster and receiver agree on
		 * LOD — but binds TerrainDepth.glsl with the sun's ortho matrix. Call from
		 * inside a ShadowMap depth pass (state already set by BeginDepthPass); this
		 * changes no global render state of its own. cameraPos is the REAL camera
		 * (not the light) so the cut matches the lit pass.
		 */
		void RenderDepth(const glm::mat4& lightViewProj, const glm::vec3& cameraPos);

		////////////////////////////////
		// Queries (S8.3) — pure CPU
		///////////////////////////////

		/**
		 * @brief Ground height at world (x, z), interpolated on the SAME triangle
		 * split the renderer draws (diagonal toward +x+z), so the value matches
		 * the full-detail rendered surface. Outside the terrain returns BaseHeight.
		 */
		float SampleHeight(float x, float z) const;

		/** @brief Unit surface normal at world (x, z); +Y outside the terrain. */
		glm::vec3 SampleNormal(float x, float z) const;

		/** @brief True when world (x, z) lies inside the heightfield extent. */
		bool Contains(float x, float z) const;

		////////////////////////////////
		// Accessors
		///////////////////////////////

		const TerrainSpecification& GetSpecification() const { return m_Spec; }
		float GetMinHeight() const { return m_MinHeight; }    // world-space
		float GetMaxHeight() const { return m_MaxHeight; }    // world-space

		/** @brief Raw normalized sample [0,1] at grid (i, j) — test/tool access. */
		float GetSample(uint32_t i, uint32_t j) const;

		/** @brief Nodes drawn by the last Render call (LOD debug HUD). */
		uint32_t GetLastDrawnNodeCount() const { return m_LastDrawnNodes; }

		////////////////////////////////
		// Shore-awareness accessors (doc 10 F4 → consumed by Water v2, F6)
		///////////////////////////////

		/** @brief The packed height+normal texture's renderer ID (RG = 16-bit
		 *  height hi/lo byte, BA = normal.xz). 0 until the first Render/RenderDepth
		 *  builds the GPU resources. Water v2 texelFetches it for shore attenuation. */
		uint32_t  GetHeightTextureID() const;
		/** @brief World XZ of the terrain's minimum corner (Origin - half size). */
		glm::vec2 GetWorldMinCorner() const;
		/** @brief World size (meters) along X and Z. */
		float     GetWorldSize()   const { return m_Spec.WorldSize; }
		/** @brief World height of a 1.0 normalized sample. */
		float     GetHeightScale() const { return m_Spec.HeightScale; }
		/** @brief World Y of a 0.0 normalized sample. */
		float     GetBaseHeight()  const { return m_Spec.BaseHeight; }

	public:
		// Public only so Ref<Terrain> construction works inside Create();
		// client code should always go through the factory.
		Terrain() = default;

	private:
		/** One quadtree leaf selected by the LOD cut: grid corner (X, Z) and the
		 *  texel span. Both Render and RenderDepth walk the same cut. */
		struct NodeDraw { int X; int Z; int Texels; };

		bool  BuildHeights();                       // image / fBm / HeightFunction -> m_Heights
		void  BuildNormals();                       // CPU normals (S8.1)
		bool  EnsureGpuResources();                 // patch mesh, textures, shader

		// Recursively select the visible leaf nodes for `cameraPos` (the LOD cut).
		// Shared by Render (lit) and RenderDepth (shadow) so caster/receiver agree.
		void  CollectCut(int nodeX, int nodeZ, int nodeTexels,
		                 const glm::vec3& cameraPos, int depth, std::vector<NodeDraw>& out) const;
		// Set the per-node uniforms + draw the shared patch for each cut node.
		void  DrawCut(const Ref<Shader>& shader, const std::vector<NodeDraw>& cut);

		float HeightAtGrid(int i, int j) const;     // clamped world-space height

		TerrainSpecification   m_Spec;
		std::vector<float>     m_Heights;           // Resolution^2 normalized samples
		std::vector<glm::vec3> m_Normals;           // per-vertex, world-space
		float m_MinHeight = 0.0f;
		float m_MaxHeight = 0.0f;
		float m_CellSize  = 1.0f;                   // WorldSize / (Resolution - 1)
		int   m_MaxDepth  = 0;                      // quadtree depth where a node = one patch

		// --- GPU state (lazy; null until the first Render) ---
		bool           m_GpuReady = false;
		Ref<Mesh>      m_Patch;                     // shared 33x33 grid + skirt
		Ref<Shader>    m_Shader;                    // Terrain.glsl
		Ref<Shader>    m_DepthShader;               // TerrainDepth.glsl (lazy; F4 shadow cast)
		Ref<Texture2D> m_HeightTex;                 // RG = height hi/lo byte, BA = normal xz
		Ref<Texture2D> m_DetailTex;                 // shared procedural albedo detail
		uint32_t       m_LastDrawnNodes = 0;
	};
}
