// graphics/AnimationClip.cpp — see AnimationClip.h.

#include "graphics/AnimationClip.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Cosmic
{
    namespace
    {
        // Find the key pair bracketing `t` in an ascending time track and the
        // interpolation factor between them. Times outside clamp to the ends.
        template <typename T, typename Lerp>
        T SampleTrack(const std::vector<float>& times, const std::vector<T>& values,
                      float t, const T& fallback, Lerp&& lerp)
        {
            const size_t n = std::min(times.size(), values.size());
            if (n == 0)
                return fallback;
            if (n == 1 || t <= times[0])
                return values[0];
            if (t >= times[n - 1])
                return values[n - 1];

            // First key strictly greater than t; its predecessor starts the span.
            const size_t hi = (size_t)(std::upper_bound(times.begin(), times.begin() + n, t)
                                       - times.begin());
            const size_t lo = hi - 1;
            const float span = times[hi] - times[lo];
            const float f    = span > 1e-8f ? (t - times[lo]) / span : 0.0f;
            return lerp(values[lo], values[hi], f);
        }

        glm::quat SlerpShortest(const glm::quat& a, glm::quat b, float f)
        {
            if (glm::dot(a, b) < 0.0f)
                b = -b;   // take the short arc
            return glm::normalize(glm::slerp(a, b, f));
        }
    }

    float AnimationClip::ResolveTime(float t, bool loop) const
    {
        if (Duration <= 0.0f)
            return 0.0f;
        if (loop)
        {
            t = std::fmod(t, Duration);
            if (t < 0.0f)
                t += Duration;
            return t;
        }
        return glm::clamp(t, 0.0f, Duration);
    }

    void AnimationClip::Sample(const Skeleton& skeleton, float t, bool loop,
                               std::vector<glm::mat4>& outLocals) const
    {
        skeleton.GetBindLocals(outLocals);
        const float time = ResolveTime(t, loop);

        for (const AnimationChannel& ch : Channels)
        {
            if (ch.JointIndex < 0 || (size_t)ch.JointIndex >= outLocals.size())
                continue;

            // Bind-pose TRS parts for the tracks this channel does not animate.
            // Decomposing the bind local per sample would be wasteful and
            // lossy; instead a channel missing a track keeps the whole bind
            // local ONLY when it animates nothing, and otherwise composes from
            // the tracks with sensible neutral fallbacks: importers always
            // emit all three tracks for animated joints (glTF samplers and
            // assimp channels carry full TRS), so the fallbacks are theory.
            const glm::vec3 pos = SampleTrack(ch.PosTimes, ch.PosValues, time,
                glm::vec3(outLocals[(size_t)ch.JointIndex][3]),
                [](const glm::vec3& a, const glm::vec3& b, float f) { return glm::mix(a, b, f); });
            const glm::quat rot = SampleTrack(ch.RotTimes, ch.RotValues, time,
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                [](const glm::quat& a, const glm::quat& b, float f) { return SlerpShortest(a, b, f); });
            const glm::vec3 scl = SampleTrack(ch.SclTimes, ch.SclValues, time,
                glm::vec3(1.0f),
                [](const glm::vec3& a, const glm::vec3& b, float f) { return glm::mix(a, b, f); });

            const bool hasRot = !ch.RotTimes.empty();
            const bool hasScl = !ch.SclTimes.empty();
            const bool hasPos = !ch.PosTimes.empty();
            if (!hasRot && !hasScl && !hasPos)
                continue;   // nothing animated — keep the bind local untouched

            glm::mat4 local = hasRot ? glm::mat4_cast(rot)
                                     : glm::mat4(glm::mat3(outLocals[(size_t)ch.JointIndex]));
            if (hasScl)
                local = local * glm::scale(glm::mat4(1.0f), scl);
            local[3] = glm::vec4(pos, 1.0f);
            outLocals[(size_t)ch.JointIndex] = local;
        }
    }

    AnimationClip AnimationClip::BakeFixedRate(float hz) const
    {
        AnimationClip baked;
        baked.Name     = Name;
        baked.Duration = Duration;
        if (hz <= 0.0f || Duration <= 0.0f)
        {
            baked.Channels = Channels;
            return baked;
        }

        const float step  = 1.0f / hz;
        const int   count = (int)std::ceil(Duration * hz) + 1;   // key at 0 and at/after Duration

        baked.Channels.reserve(Channels.size());
        for (const AnimationChannel& ch : Channels)
        {
            AnimationChannel b;
            b.JointIndex = ch.JointIndex;
            for (int k = 0; k < count; ++k)
            {
                const float t = std::min(Duration, (float)k * step);
                if (!ch.PosTimes.empty())
                {
                    b.PosTimes.push_back(t);
                    b.PosValues.push_back(SampleTrack(ch.PosTimes, ch.PosValues, t, glm::vec3(0.0f),
                        [](const glm::vec3& a, const glm::vec3& v, float f) { return glm::mix(a, v, f); }));
                }
                if (!ch.RotTimes.empty())
                {
                    b.RotTimes.push_back(t);
                    b.RotValues.push_back(SampleTrack(ch.RotTimes, ch.RotValues, t,
                        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                        [](const glm::quat& a, const glm::quat& v, float f) { return SlerpShortest(a, v, f); }));
                }
                if (!ch.SclTimes.empty())
                {
                    b.SclTimes.push_back(t);
                    b.SclValues.push_back(SampleTrack(ch.SclTimes, ch.SclValues, t, glm::vec3(1.0f),
                        [](const glm::vec3& a, const glm::vec3& v, float f) { return glm::mix(a, v, f); }));
                }
            }
            baked.Channels.push_back(std::move(b));
        }
        return baked;
    }
}
