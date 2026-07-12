#pragma once

// assets/MeshImport.h
//
// ============================================================================
// COSMIC ENGINE — Mesh import pipeline (Phase 13 / E16, completed Phase 20 / A1)
// ============================================================================
//
// Brings industry-standard model files into the engine as first-class meshes,
// applying a deterministic UNIT + UP-AXIS transform captured in a `.cmeta`
// sidecar so a re-import is reproducible and CAD models land at the right world
// size. Backends per format:
//
//   * Wavefront OBJ  — engine's own parser (Mesh::BuildFromOBJ) for the plain
//     single-mesh path (existing scenes stay byte-identical); the rich
//     multi-mesh/material path routes through assimp when available.
//   * FBX / STL / DAE / PLY — the vendored assimp backend (A1: vendored,
//     trimmed, default ON — COSMIC_WITH_ASSIMP). Building with
//     -DCOSMIC_WITH_ASSIMP=OFF falls back to the OBJ-only seam.
//   * glTF / GLB — cgltf (always compiled; the S4.4b Model path's library).
//
// A1 adds the RICH import surface on top of the E16 single-mesh one:
//
//   * ImportModelData() — CPU-only description of a whole source file: every
//     sub-mesh (node-transform baked), every material (factors + texture
//     references), every embedded texture blob. The editor composes these into
//     parent+child entities and generated `.cmat` files (the E16 spec).
//   * Sub-mesh paths — "project://models/gun.fbx#2" addresses sub-mesh 2 of a
//     multi-mesh source. SplitSubmeshPath parses the fragment;
//     AssetLibrary::GetMesh caches each fragment as its own asset slot. Paths
//     without a fragment keep their exact pre-A1 meaning (whole file, merged).
//
// The unit trap is handled explicitly: ImportSettings carries the source->meter
// scale and up-axis, seeded from a per-extension preset (STL mm, FBX cm) and
// then editable in the `.cmeta`. The editor shows the assumed scale; nothing is
// guessed silently.
// ============================================================================

#include "core/Core.h"
#include "graphics/Mesh.h"          // MeshData / SkinVertex (header-only CPU geometry)
#include "graphics/Skeleton.h"      // A2 — ImportedModelDesc::Bones
#include "graphics/AnimationClip.h" // A2 — ImportedModelDesc::Clips

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Cosmic
{

    /**
     * @brief Deterministic import parameters, persisted next to a source model
     * as a `<source>.cmeta` TOML sidecar. Defaults come from a per-extension
     * preset; the editor (or a hand edit) can override them and re-import.
     */
    struct COSMIC_API ImportSettings
    {
        enum class UpAxis { Y = 0, Z = 1 };   // source up axis; Z is rotated to the engine's Y-up

        float  Scale           = 1.0f;        // source unit -> meters (STL mm = 0.001, FBX cm = 0.01)
        UpAxis Up              = UpAxis::Y;
        bool   FlipUVs         = false;       // flip V (some DCC tools export flipped)
        bool   GenerateNormals = true;        // synthesize normals when the source has none

        /** @brief Preset for a lower-case extension ("stl","fbx","obj",...). */
        static ImportSettings DefaultFor(const std::string& extLower);

        /** @brief Parse a `.cmeta` TOML body (see ToCmetaText). Unknown/empty ->
         *  the passed-in fallback's value per field. */
        static ImportSettings FromCmetaText(const std::string& tomlText, const ImportSettings& fallback);

        /** @brief Serialize to `.cmeta` TOML text. `sourceFile` is recorded for humans. */
        std::string ToCmetaText(const std::string& sourceFile) const;
    };

    // ------------------------------------------------------------------------
    // Rich import description (A1) — plain CPU data, like MeshData: header-only
    // structs passed through exported MeshImport functions, never exported
    // themselves. The editor owns what to DO with them (spawn hierarchy, write
    // .cmat files, copy textures); the engine only describes the source.
    // ------------------------------------------------------------------------

    /** @brief One importable sub-mesh: geometry with the node's world transform
     *  and the `.cmeta` unit/up-axis transform already baked in. Addressed from
     *  a scene as "<source>#<index>" (see MeshImport::SubmeshPath). */
    struct ImportedMeshDesc
    {
        std::string Name;                // node/mesh name ("Mesh_<i>" fallback)
        int         MaterialIndex = -1;  // into ImportedModelDesc::Materials; -1 = none
        MeshData    Geometry;

        // A2 — per-vertex joint influences, parallel to Geometry.Vertices;
        // empty = static. Indices address ImportedModelDesc::Bones.Joints.
        // Skinned geometry bakes ONLY the unit/up-axis transform (never the
        // node's world transform — the joints own the motion; glTF ignores the
        // skinned node's own transform by spec, FBX bind poses likewise).
        std::vector<SkinVertex> Skin;
    };

    /** @brief A source material as the file states it. Texture references are
     *  either paths (absolute, or relative to the source file's directory) or
     *  "*<i>" for ImportedModelDesc::EmbeddedTextures[i]. Empty = no map. */
    struct ImportedMaterialDesc
    {
        std::string Name;
        glm::vec4   Albedo{ 1.0f, 1.0f, 1.0f, 1.0f };
        float       Metallic  = 0.0f;
        float       Roughness = 0.5f;
        glm::vec3   Emissive{ 0.0f };
        float       Opacity   = 1.0f;    // < 1 -> the editor marks the .cmat Transparent

        std::string AlbedoMap;
        std::string NormalMap;
        std::string MetalRoughMap;
        std::string AOMap;
        std::string EmissiveMap;
    };

    /** @brief A texture embedded in the source file (FBX-embedded media, GLB
     *  buffer images). Height == 0 -> Bytes are a complete compressed image
     *  file (FormatHint = extension, e.g. "png"/"jpg"); Height > 0 -> Bytes are
     *  raw RGBA8 pixels (Width*Height*4). */
    struct ImportedTextureDesc
    {
        std::string          Name;        // suggested file stem (no extension)
        std::string          FormatHint;  // "png", "jpg", ... ("" when raw RGBA)
        uint32_t             Width  = 0;
        uint32_t             Height = 0;
        std::vector<uint8_t> Bytes;
    };

    /** @brief Everything MeshImport can describe about one source model file. */
    struct ImportedModelDesc
    {
        std::vector<ImportedMeshDesc>     Meshes;
        std::vector<ImportedMaterialDesc> Materials;
        std::vector<ImportedTextureDesc>  EmbeddedTextures;

        // A2 — the file's joint hierarchy (empty Joints = no skin) and its
        // animation clips. One skeleton per file in v1: glTF reads skin 0,
        // assimp builds the closure of every mesh's bones (extra skins warn).
        // ImportCorrection carries the `.cmeta` matrix baked into the geometry.
        Skeleton                   Bones;
        std::vector<AnimationClip> Clips;
    };

    class COSMIC_API MeshImport
    {
    public:
        /** @brief Lower-case extension without the dot ("models/x.STL" -> "stl"). */
        static std::string Extension(const std::string& path);

        /** @brief True if THIS build can import the given (lower-case) extension.
         *  OBJ and glTF/GLB (cgltf) are always true; FBX/STL/DAE/PLY require
         *  the assimp backend. */
        static bool Supports(const std::string& extLower);

        /** @brief Whether the assimp backend was compiled in (COSMIC_WITH_ASSIMP). */
        static bool AssimpEnabled();

        /**
         * @brief Import a real disk path into a mesh, baking the settings' unit
         * scale + up-axis (and optional UV flip) into the geometry.
         * `submeshIndex` < 0 merges the whole file into one mesh (the E16
         * behavior — the only path existing scenes use); >= 0 imports that one
         * sub-mesh (A1 — the "<source>#<i>" children of a multi-mesh import).
         * Returns null (and logs) on parse failure or an unsupported format.
         * Main-thread / GL (uploads the mesh).
         */
        static Ref<Mesh> Import(const std::string& resolvedSourcePath, const ImportSettings& settings,
                                int submeshIndex = -1);

        /**
         * @brief CPU half of Import (no GL): parse + bake into MeshData.
         * Headless-testable; empty Vertices on failure. The OBJ merged path
         * (submeshIndex < 0) uses the engine's own parser so pre-A1 scenes load
         * byte-identically; everything else routes cgltf (glTF/GLB) or assimp.
         */
        static MeshData ImportData(const std::string& resolvedSourcePath, const ImportSettings& settings,
                                   int submeshIndex = -1);

        /**
         * @brief Describe a whole source file (CPU-only): sub-meshes, materials,
         * embedded textures. The editor's rich-import path (parent + children +
         * generated .cmat files). False (and logs) on parse failure.
         */
        static bool ImportModelData(ImportedModelDesc& out, const std::string& resolvedSourcePath,
                                    const ImportSettings& settings);

        /** @brief Compose a sub-mesh path: "models/gun.fbx" + 2 -> "models/gun.fbx#2". */
        static std::string SubmeshPath(const std::string& sourcePath, int submeshIndex);

        /** @brief Parse a "<base>#<digits>" sub-mesh path. Returns false (and
         *  leaves the outputs untouched) when `path` has no fragment. */
        static bool SplitSubmeshPath(const std::string& path, std::string& baseOut, int& submeshOut);

        /** @brief The `.cmeta` path for a source path (source + ".cmeta"). */
        static std::string CmetaPathFor(const std::string& sourcePath);

        /**
         * @brief Read the sibling `.cmeta` for a resolved source path; if it is
         * missing, seed it from the extension preset AND write it out (so the
         * next import is deterministic). Pure I/O + TOML — no GL.
         */
        static ImportSettings LoadOrInitMeta(const std::string& resolvedSourcePath);
    };
}
