#pragma once

// graphics/Skeleton.h
//
// ============================================================================
// COSMIC ENGINE — Skeleton (joint hierarchy + skinning palette math) [A2]
// ============================================================================
//
// The pure-CPU half of skeletal animation: a joint tree with bind-pose locals
// and inverse-bind matrices, plus the palette math a skinned draw consumes.
// No GL anywhere — everything here is headless-testable.
//
//   locals  = clip.Sample(skeleton, t, loop)        (AnimationClip.h)
//   palette = skeleton.ComputePalette(locals)       (model space, then the
//                                                    import-correction conjugation)
//   PBRSkinned.glsl: worldPos = u_Model * (Σ w_i * palette[j_i]) * position
//
// IMPORT CORRECTION: imported geometry bakes the `.cmeta` unit/up-axis matrix M
// into its vertices (MeshImport). Joint animation is authored in the source's
// model space, so the palette must be conjugated: palette' = M * palette * M⁻¹
// (then baked vertices come out exactly as if the whole animated model had been
// scaled by M). Importers store M here; ComputePalette applies it.
//
// JOINT ORDER: any — parents do not need to precede children (ComputeGlobals
// resolves recursively with memoization). Vertex joint indices index this
// array directly, in the order the importer discovered the skin's joints.
// ============================================================================

#include "core/Core.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Cosmic
{
    struct SkeletonJoint
    {
        std::string Name;
        int         Parent = -1;            // index into Skeleton::Joints; -1 = root
        glm::mat4   LocalBind{ 1.0f };      // bind-pose local (parent-relative) transform
        glm::mat4   InverseBind{ 1.0f };    // model-space inverse bind matrix
    };

    class COSMIC_API Skeleton
    {
    public:
        std::vector<SkeletonJoint> Joints;

        // The `.cmeta` unit/up-axis matrix baked into the sibling geometry (see
        // the header note). Identity when the import transform was identity.
        glm::mat4 ImportCorrection{ 1.0f };
        glm::mat4 ImportCorrectionInv{ 1.0f };

        size_t JointCount() const { return Joints.size(); }

        /** @brief Index of the joint named `name`, or -1. Linear — cache it. */
        int Find(const std::string& name) const;

        /** @brief Per-joint bind-pose locals — the seed pose Sample() starts from. */
        void GetBindLocals(std::vector<glm::mat4>& out) const;

        /**
         * @brief Parent-accumulate `locals` into model-space globals. `locals`
         * must hold JointCount() matrices (joint order). Handles any joint
         * ordering (memoized recursion).
         */
        void ComputeGlobals(const std::vector<glm::mat4>& locals,
                            std::vector<glm::mat4>& outGlobals) const;

        /**
         * @brief The skinning palette for a sampled pose: for each joint,
         * ImportCorrection * (global * inverseBind) * ImportCorrection⁻¹.
         * At the bind pose every entry is ImportCorrection-conjugated identity
         * == identity — a skinned mesh with no animator renders untouched.
         */
        void ComputePalette(const std::vector<glm::mat4>& locals,
                            std::vector<glm::mat4>& outPalette) const;
    };
}
