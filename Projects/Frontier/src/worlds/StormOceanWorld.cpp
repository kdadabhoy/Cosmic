// StormOceanWorld.cpp — storm at sea (skeleton; see StormOceanWorld.h).

#include "worlds/StormOceanWorld.h"

#include "ui/IconsLucide.h"

#include <imgui.h>

namespace Frontier
{
    const WorldInfo& StormOceanWorld::GetInfo() const
    {
        static const WorldInfo info{
            "Storm Ocean",
            ICON_LC_CLOUD_LIGHTNING,
            "Heavy swell, driving rain, lightning — the water v2 stress test",
            { 0.0f, 35.0f, 120.0f },
            0.0f, -10.0f,
            { 0.10f, 0.12f, 0.15f, 1.0f }
        };
        return info;
    }

    void StormOceanWorld::OnAttach()
    {
        // TODO(F16): open-water Gerstner set (8 waves, whitecaps), rain preset
        //            (F9 velocity stretch) tracking the camera + splash rings,
        //            lightning controller (sun-intensity/sky pulse + delayed
        //            thunder, F10), storm sky preset, buoy on SampleHeight.
    }

    void StormOceanWorld::OnDetach()
    {
    }

    void StormOceanWorld::OnUpdate(WorldContext& ctx)
    {
        // TODO(F16): replace with the SceneRenderer frame (F2).
        DrawPlaceholder(ctx, { 0.45f, 0.65f, 0.95f, 1.0f });
    }

    void StormOceanWorld::OnPanels(WorldContext& ctx)
    {
        (void)ctx;
        ImGui::Begin("World Settings");
        ImGui::TextWrapped("Storm Ocean — skeleton placeholder.");
        ImGui::Separator();
        ImGui::TextWrapped("Content lands with work order F16 "
                           "(docs/plans/10-phase11-frontier-plan.md).");
        ImGui::End();
    }

} // namespace Frontier
