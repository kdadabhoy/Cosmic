// graphics/Skeleton.cpp — see Skeleton.h.

#include "graphics/Skeleton.h"

#include "core/Log.h"

namespace Cosmic
{
    int Skeleton::Find(const std::string& name) const
    {
        for (size_t i = 0; i < Joints.size(); ++i)
            if (Joints[i].Name == name)
                return (int)i;
        return -1;
    }

    void Skeleton::GetBindLocals(std::vector<glm::mat4>& out) const
    {
        out.resize(Joints.size());
        for (size_t i = 0; i < Joints.size(); ++i)
            out[i] = Joints[i].LocalBind;
    }

    void Skeleton::ComputeGlobals(const std::vector<glm::mat4>& locals,
                                  std::vector<glm::mat4>& outGlobals) const
    {
        const size_t n = Joints.size();
        if (locals.size() != n)
        {
            CS_CORE_WARN("Skeleton::ComputeGlobals: {0} locals for {1} joints — pose ignored.",
                         locals.size(), n);
            outGlobals.assign(n, glm::mat4(1.0f));
            return;
        }

        outGlobals.resize(n);
        std::vector<uint8_t> done(n, 0);

        // Memoized parent-first resolution — joint order is unconstrained, and
        // an explicit stack keeps deep chains off the call stack.
        std::vector<int> stack;
        for (size_t i = 0; i < n; ++i)
        {
            if (done[i])
                continue;
            stack.clear();
            int j = (int)i;
            while (j >= 0 && !done[j])
            {
                stack.push_back(j);
                j = (Joints[(size_t)j].Parent >= 0 && (size_t)Joints[(size_t)j].Parent < n)
                        ? Joints[(size_t)j].Parent : -1;
            }
            while (!stack.empty())
            {
                const int k = stack.back();
                stack.pop_back();
                const int p = ((size_t)Joints[(size_t)k].Parent < n) ? Joints[(size_t)k].Parent : -1;
                outGlobals[(size_t)k] = (p >= 0 ? outGlobals[(size_t)p] : glm::mat4(1.0f)) * locals[(size_t)k];
                done[(size_t)k] = 1;
            }
        }
    }

    void Skeleton::ComputePalette(const std::vector<glm::mat4>& locals,
                                  std::vector<glm::mat4>& outPalette) const
    {
        std::vector<glm::mat4> globals;
        ComputeGlobals(locals, globals);

        outPalette.resize(Joints.size());
        for (size_t i = 0; i < Joints.size(); ++i)
            outPalette[i] = ImportCorrection * globals[i] * Joints[i].InverseBind * ImportCorrectionInv;
    }
}
