// IslandWorld.cpp — flagship seamless island (skeleton; see IslandWorld.h).

#include "worlds/IslandWorld.h"

#include "ui/IconsLucide.h"

#include <imgui.h>

namespace Frontier
{
    const WorldInfo& IslandWorld::GetInfo() const
    {
        static const WorldInfo info{
            "Frontier Island",
            ICON_LC_GLOBE,
            "Volcano, snowy range, alpine lake, ocean coast — one seamless flight",
            { 0.0f, 220.0f, 1400.0f },   // spawn: offshore, looking at the island
            0.0f, -12.0f,
            { 0.055f, 0.075f, 0.110f, 1.0f }
        };
        return info;
    }

    void IslandWorld::OnAttach()
    {
        // TODO(F11): build the composed heightfield (HeightfieldComposer) and
        //            Terrain::Create with the island spec.
        // TODO(F12a): ocean + lake Water::Create, sky v2 setup, time-of-day defaults.
        // TODO(F12b): lava strips + caldera lake + volcano emitters + rumble.
        // TODO(F12c): forest/rock scatter, waterfall + river sheets, wildlife.
    }

    void IslandWorld::OnDetach()
    {
    }

    void IslandWorld::OnUpdate(WorldContext& ctx)
    {
        // TODO(F12a): replace the placeholder with the SceneRenderer frame (F2).
        DrawPlaceholder(ctx, { 0.35f, 0.75f, 0.45f, 1.0f });
    }

    void IslandWorld::OnPanels(WorldContext& ctx)
    {
        (void)ctx;
        ImGui::Begin("World Settings");
        ImGui::TextWrapped("Frontier Island — skeleton placeholder.");
        ImGui::Separator();
        ImGui::TextWrapped("Content lands with work orders F11 + F12a/b/c "
                           "(docs/plans/10-phase11-frontier-plan.md).");
        ImGui::End();
    }

} // namespace Frontier
