#pragma once

// MirrorLakeWorld — the "realistic water" money shot (Phase 11, doc 10 item F15).
//
// A glass-calm alpine lake at golden hour: near-zero Gerstner amplitude so the
// planar reflection reads like a mirror, drifting mist banks over the surface
// (large soft particles), long god rays through instanced pines, water-v2
// caustics on the visible lakebed, and a gentle fish splash-ring every so
// often (buoyancy query + flipbook rings). The showcase for F6's water v2.
//
// SKELETON: renders the shared placeholder until F15.

#include "World.h"

namespace Frontier
{
    class MirrorLakeWorld : public World
    {
    public:
        const WorldInfo& GetInfo() const override;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(WorldContext& ctx) override;
        void OnPanels(WorldContext& ctx) override;
    };

} // namespace Frontier
