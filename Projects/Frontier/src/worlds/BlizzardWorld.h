#pragma once

// BlizzardWorld — the "realistic snow" money shot (Phase 11, doc 10 item F14).
//
// A whiteout mountain storm: dense wind-blown snow (velocity-stretched
// particles + a box emitter tracking the camera), the S11.1 snow overlay on
// everything, DYNAMIC accumulation visibly building on a cabin + instanced
// pines (CoverageCapture, F8), heavy fog, grey-sky preset, warm emissive
// window lights. The showcase for F8's accumulation mask.
//
// SKELETON: renders the shared placeholder until F14.

#include "World.h"

namespace Frontier
{
    class BlizzardWorld : public World
    {
    public:
        const WorldInfo& GetInfo() const override;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(WorldContext& ctx) override;
        void OnPanels(WorldContext& ctx) override;
    };

} // namespace Frontier
