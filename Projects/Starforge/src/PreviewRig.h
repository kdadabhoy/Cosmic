#pragma once

// PreviewRig.h
//
// ============================================================================
// Starforge — shared offscreen preview service (Phase 20 / A4, gap analysis
// §14.3).
// ============================================================================
//
// One tiny FBO + a SceneRenderer-lite pass (one mesh, one key light, whatever
// IBL the viewport's SceneRenderer has registered) behind two consumption
// modes:
//
//   * INTERACTIVE — RenderMesh/RenderMaterial draw into this instance's FBO at
//     the requested size and return the color-attachment texture id for an
//     ImGui::Image. The instance owns an orbit state (Orbit/Zoom/ResetView) so
//     a panel can drag-turntable its preview. Phase 23 T7/T11 and Phase 24
//     M1/M3 instantiate one rig per document/panel for exactly this mode
//     (today: the Material Editor's preview sphere).
//
//   * BATCH THUMBNAILS — Thumbnail(vfs) hands back a cached Ref<Texture2D> for
//     a mesh or .cmat asset (null while pending); PumpThumbnails renders a
//     budgeted number of pending requests per frame and caches the PNGs under
//     the directory given by SetCacheDirectory (A4: <project>/.starforge/
//     thumbs/) so a later session loads them from disk without a render.
//     The Content Browser is the consumer (E10's parked half).
//
// STATE-RESTORE CONTRACT (doc 13 §0.5): every preview pass captures the bound
// framebuffer first and re-binds it after, and restores the engine render-state
// defaults (depth ON/ON, cull None, blend Alpha, fill). The scene must render
// byte-identically after any number of preview passes — that is A4's
// acceptance, provable in-editor via Help ▸ Preview State Self-Test.
//
// Lighting note: the pass uploads its own key-light SceneLightsDesc into the
// global lights UBO; the viewport re-uploads scene lights every frame, so the
// mutation never outlives the frame that requested the preview.
// ============================================================================

#include <Cosmic.h>

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Starforge
{
    class PreviewRig
    {
    public:
        PreviewRig() = default;
        ~PreviewRig() = default;

        // Owns a GPU target; copying would alias it (the FrameBuffer rule).
        PreviewRig(const PreviewRig&)            = delete;
        PreviewRig& operator=(const PreviewRig&) = delete;

        // ---- Interactive mode ------------------------------------------------
        // Draw into the rig's FBO at (width, height) with the rig's orbit
        // camera; returns the color-attachment texture id (0 on failure). The
        // material wins when non-null, else the Lambert color path.
        uint32_t RenderMesh(const Cosmic::Ref<Cosmic::Mesh>& mesh,
                            const Cosmic::Ref<Cosmic::Material>& material,
                            const glm::vec4& lambertColor,
                            uint32_t width, uint32_t height);

        // Material preview: the rig's UV sphere with a live-built PBR material
        // (rebuilt only when the asset's reflected fields change).
        uint32_t RenderMaterial(const Cosmic::MaterialAsset& asset,
                                uint32_t width, uint32_t height);

        // Orbit input for the interactive image (pixel drag deltas / wheel).
        void Orbit(float dxPixels, float dyPixels);
        void Zoom(float wheelSteps);
        void ResetView();

        // ---- Batch-thumbnail mode --------------------------------------------
        // Thumbnail for a mesh (.obj/.fbx/.stl/.dae/.ply/.gltf/.glb) or .cmat
        // vfs path. Null until a PumpThumbnails call generates it (or it loads
        // from the disk cache); permanently null for failed/unsupported assets.
        Cosmic::Ref<Cosmic::Texture2D> Thumbnail(const std::string& vfsPath);

        // Generate up to `budget` pending thumbnails NOW (GL must be current).
        // Call once per frame from the editor loop.
        void PumpThumbnails(int budget = 2);

        // Drop the cached thumbnail (memory + disk) so the next request
        // regenerates it — call when the source file changed (FileWatcher,
        // re-import, .cmat save).
        void Invalidate(const std::string& vfsPath);

        // Disk cache directory (A4: "<project>/.starforge/thumbs"); "" disables
        // the disk half. Call on project open/switch; clears the memory cache.
        void SetCacheDirectory(const std::string& dir);

        // True for the model extensions the thumbnail path renders (lower-case,
        // no dot) — one shared answer for the browser's tile logic.
        static bool IsMeshExtension(const std::string& extLower);

    private:
        void        EnsureResources();
        uint32_t    Draw(const Cosmic::Ref<Cosmic::Mesh>& mesh,
                         const Cosmic::Ref<Cosmic::Material>& material,
                         const glm::vec4& color, uint32_t w, uint32_t h);
        bool        Generate(const std::string& vfs);
        std::string CacheFileFor(const std::string& vfs) const;   // "" = uncacheable

        Cosmic::Ref<Cosmic::FrameBuffer> m_Fbo;
        Cosmic::Ref<Cosmic::Mesh>        m_Sphere;          // material preview mesh
        Cosmic::Ref<Cosmic::Material>    m_LiveMaterial;    // RenderMaterial's build
        std::string                      m_LiveMaterialKey; // serialized fields it was built from
        Cosmic::Ref<Cosmic::Material>    m_DefaultMaterial; // neutral mesh-thumbnail look

        // Orbit state (interactive mode).
        float m_Yaw   = 0.6109f;    // 35 deg
        float m_Pitch = -0.3491f;   // -20 deg
        float m_Zoom  = 1.0f;

        // Thumbnail service state.
        std::string                     m_CacheDir;
        std::deque<std::string>         m_Queue;
        std::unordered_set<std::string> m_Queued;
        std::unordered_set<std::string> m_Failed;
        std::unordered_map<std::string, Cosmic::Ref<Cosmic::Texture2D>> m_Ready;
    };
}
