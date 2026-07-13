#pragma once

// graphics/AnimationClip.h
//
// ============================================================================
// COSMIC ENGINE — AnimationClip (keyframed joint animation + pure sampling) [A2]
// ============================================================================
//
// One named clip: per-joint keyframe channels over translation / rotation /
// scale. Sampling is pure CPU math (headless-tested): position and scale keys
// interpolate linearly, rotations slerp along the shortest arc, and a joint
// with no channel keeps its skeleton bind local. Loop wraps time by Duration;
// non-loop clamps to the ends.
//
// The v1 scope (doc 19 A2): ONE clip plays per Animator; blend trees / state
// machines stay parked (FEATURE-MATRIX row). BakeFixedRate() resamples every
// channel at a fixed rate — the "fixed-rate bake option" for content whose
// authored keys are too sparse or too dense for runtime taste.
// ============================================================================

#include "core/Core.h"
#include "graphics/Skeleton.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

namespace Cosmic
{
    /** @brief The keyframe tracks animating ONE joint. Any track may be empty
     *  (that TRS part stays at the bind value). Times ascend, in seconds. */
    struct AnimationChannel
    {
        int JointIndex = -1;

        std::vector<float>     PosTimes;
        std::vector<glm::vec3> PosValues;
        std::vector<float>     RotTimes;
        std::vector<glm::quat> RotValues;
        std::vector<float>     SclTimes;
        std::vector<glm::vec3> SclValues;
    };

    class COSMIC_API AnimationClip
    {
    public:
        std::string Name;
        float       Duration = 0.0f;   // seconds (max key time)
        std::vector<AnimationChannel> Channels;

        /**
         * @brief Resolve a play-head time: loop wraps into [0, Duration),
         * non-loop clamps to [0, Duration]. Duration 0 returns 0.
         */
        float ResolveTime(float t, bool loop) const;

        /**
         * @brief Sample the clip at `t` (already-resolved or raw — ResolveTime
         * is applied) into per-joint LOCAL transforms, seeded from the
         * skeleton's bind locals so unanimated joints hold their bind pose.
         * `outLocals` is sized to the skeleton's joint count.
         */
        void Sample(const Skeleton& skeleton, float t, bool loop,
                    std::vector<glm::mat4>& outLocals) const;

        /**
         * @brief Resample every channel at a fixed key rate (keys at 0, 1/hz,
         * 2/hz, … Duration). Returns the baked clip; the source is untouched.
         */
        AnimationClip BakeFixedRate(float hz) const;

        /**
         * @brief Pose-space blend of two per-joint LOCAL transform sets (Phase 24 /
         * M6 crossfade): `out[j] = blend(a[j], b[j], w)` decomposed into TRS —
         * translation/scale mix linearly, rotation slerps the shortest arc — then
         * recomposed. w in [0,1]: 0 ⇒ a, 1 ⇒ b. `a` and `b` come from Sample() on
         * the SAME skeleton (matching sizes). `out` may alias `a`. Pure / headless.
         */
        static void BlendLocals(const std::vector<glm::mat4>& a,
                                const std::vector<glm::mat4>& b,
                                float w, std::vector<glm::mat4>& out);
    };
}
