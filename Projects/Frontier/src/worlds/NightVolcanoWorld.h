#pragma once

// NightVolcanoWorld — the "realistic volcano" money shot (Phase 11, doc 10 item F13).
//
// A dusk-to-night close-up of a caldera: the FlowEmissive lava lake and flows
// dominate the exposure (bloom does the glow), ember storms (additive
// particles), god rays through the smoke column, heat haze over every vent,
// the SkyDetail star field + moon overhead, and the F10 rumble loop swelling
// with proximity. Reuses the island's volcano recipe (F12b) at closer range
// with night-tuned lighting/exposure.
//
// SKELETON: renders the shared placeholder until F13.

#include "World.h"

namespace Frontier
{
    class NightVolcanoWorld : public World
    {
    public:
        const WorldInfo& GetInfo() const override;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(WorldContext& ctx) override;
        void OnPanels(WorldContext& ctx) override;
    };

} // namespace Frontier
