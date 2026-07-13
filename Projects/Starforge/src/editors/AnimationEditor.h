#pragma once

// editors/AnimationEditor.h
//
// ============================================================================
// Starforge Animation Editor — document editor for rigged models
// (Phase 24 / M3, gap §8.2; deps A1, A2, M1, M2, A4).
// ============================================================================
//
// Opens a skinned model asset as an M1 document: a skeleton tree (left), an
// interactive PreviewRig viewport with a bone overlay + click-to-select-joint
// (center), joint details (right), and an M2 timeline of the active clip's
// keyframes with transport + scrub driving A2's sampling (bottom).
//
// INSPECT-ONLY (v1): the editor plays/scrubs/inspects — it does NOT author joint
// transforms or clips. The runtime is doc 19 A2 (unchanged); full controller
// graphs stay parked. Socket authoring (M4) appends to the details pane.
// ============================================================================

#include "IAssetEditor.h"
#include "PreviewRig.h"
#include "widgets/Timeline.h"

#include <Cosmic.h>

#include <string>
#include <vector>

namespace Starforge
{
    class AnimationEditor : public IAssetEditor
    {
    public:
        explicit AnimationEditor(std::string vfsPath);

        const std::string& Path() const override { return m_Path; }
        std::string        Title() const override { return m_Title; }
        const char*        Icon()  const override { return ICON_LC_BONE; }

        // Inspect-only: nothing to persist (the runtime clips live in the source).
        bool Dirty() const override { return false; }

        void OnUpdate(EditorContext& ctx, float ts) override;
        void OnImGuiRender(EditorContext& ctx) override;

    private:
        void EnsureLoaded();
        void SelectClip(int index);
        void SamplePose();

        void DrawSkeletonTree(EditorContext& ctx);
        void DrawJointNode(int jointIndex);
        void DrawPreview(EditorContext& ctx);
        void DrawDetails(EditorContext& ctx);
        void DrawSocketSection(EditorContext& ctx);   // M4 — attach a prop to the selected joint
        void DrawTimeline(EditorContext& ctx);
        void RebuildTracks();

        std::string m_Path;
        std::string m_Title;
        bool        m_Loaded  = false;
        bool        m_HasSkin = false;

        Cosmic::Ref<Cosmic::Mesh>          m_Mesh;    // merged skinned mesh (carries the Skeleton)
        std::vector<std::string>           m_ClipNames;
        int                                m_ClipIndex = -1;
        Cosmic::Ref<Cosmic::AnimationClip> m_Clip;

        TimelineState               m_Timeline;
        std::vector<TimelineTrack>  m_Tracks;   // active clip's channels (display)

        // Per-frame sampled pose (kept across Draw* calls).
        std::vector<glm::mat4> m_Locals;
        std::vector<glm::mat4> m_Palette;      // skinning matrices → the mesh
        std::vector<glm::mat4> m_Globals;      // model-space joint globals
        std::vector<glm::mat4> m_JointModels;  // ImportCorrection·global (overlay + picking)
        std::vector<int>       m_Parents;      // cached joint parents (overlay)
        std::vector<std::vector<int>> m_Children;   // tree adjacency

        int  m_SelJoint  = -1;
        bool m_ShowBones = true;
        bool m_Dragged   = false;   // orbit-drag vs click-select discriminator

        PreviewRig m_Preview;       // interactive per-document rig (its own FBO)
        uint32_t   m_PreviewW = 0, m_PreviewH = 0;
    };
}
