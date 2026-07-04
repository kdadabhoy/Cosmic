#pragma once

// assets/MeshImport.h
//
// ============================================================================
// COSMIC ENGINE — Mesh import pipeline (Phase 13 / E16)
// ============================================================================
//
// Brings industry-standard model files into the engine as first-class meshes,
// applying a deterministic UNIT + UP-AXIS transform captured in a `.cmeta`
// sidecar so a re-import is reproducible and CAD models land at the right world
// size. glTF keeps its dedicated cgltf path (assets/Model); MeshImport owns the
// single-mesh formats:
//
//   * Wavefront OBJ  — always available (engine's own parser, Mesh::BuildFromOBJ).
//   * FBX / STL / DAE / PLY — via the vendored assimp backend, compiled in only
//     when COSMIC_WITH_ASSIMP is defined (assimp is the phase's one heavyweight
//     dependency — see the vendoring note in MeshImport.cpp). Until it is
//     vendored, MeshImport::Supports() reports those formats as unavailable and
//     Import() returns null with a clear "rebuild with assimp" error, so the
//     seam ships and the editor UX is complete today for OBJ/glTF.
//
// The unit trap is handled explicitly: ImportSettings carries the source->meter
// scale and up-axis, seeded from a per-extension preset (STL mm, FBX cm) and
// then editable in the `.cmeta`. The editor shows the assumed scale; nothing is
// guessed silently.
// ============================================================================

#include "core/Core.h"

#include <string>

namespace Cosmic
{
    class Mesh;

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

    class COSMIC_API MeshImport
    {
    public:
        /** @brief Lower-case extension without the dot ("models/x.STL" -> "stl"). */
        static std::string Extension(const std::string& path);

        /** @brief True if THIS build can import the given (lower-case) extension.
         *  OBJ is always true; FBX/STL/DAE/PLY require the assimp backend. */
        static bool Supports(const std::string& extLower);

        /** @brief Whether the assimp backend was compiled in (COSMIC_WITH_ASSIMP). */
        static bool AssimpEnabled();

        /**
         * @brief Import a real disk path into a single merged mesh, baking the
         * settings' unit scale + up-axis (and optional UV flip) into the geometry.
         * Returns null (and logs) on parse failure or an unsupported format.
         * Main-thread / GL (uploads the mesh).
         */
        static Ref<Mesh> Import(const std::string& resolvedSourcePath, const ImportSettings& settings);

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
