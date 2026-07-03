#pragma once

// StormOceanWorld — heavy weather at sea (Phase 11, doc 10 item F16).
//
// A storm ocean: large 8-wave Gerstner swell with whitecaps (water v2,
// u_WhitecapStrength), driving rain (velocity-stretched particles, F9) with
// splash rings on the surface, periodic lightning (sky + directional-light
// pulse + delayed thunder via F10), storm-grey sky, and a buoyant marker
// bobbing on SampleHeight to sell the swell scale.
//
// SKELETON: renders the shared placeholder until F16.

#include "World.h"

namespace Frontier
{
    class StormOceanWorld : public World
    {
    public:
        const WorldInfo& GetInfo() const override;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(WorldContext& ctx) override;
        void OnPanels(WorldContext& ctx) override;
    };

} // namespace Frontier
