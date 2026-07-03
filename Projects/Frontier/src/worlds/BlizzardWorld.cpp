// BlizzardWorld.cpp — whiteout mountain storm (skeleton; see BlizzardWorld.h).

#include "worlds/BlizzardWorld.h"

#include "ui/IconsLucide.h"

#include <imgui.h>

namespace Frontier
{
    const WorldInfo& BlizzardWorld::GetInfo() const
    {
        static const WorldInfo info{
            "Blizzard Peak",
            ICON_LC_SNOWFLAKE,
            "Whiteout storm — snow accumulating on a cabin and pines in real time",
            { 0.0f, 60.0f, 220.0f },
            0.0f, -8.0f,
            { 0.28f, 0.30f, 0.34f, 1.0f }
        };
        return info;
    }

    void BlizzardWorld::OnAttach()
    {
        // TODO(F14): ridged terrain patch + cabin (primitives, warm emissive
        //            windows) + instanced pines (F5); falling-snow preset (F8)
        //            in a camera-tracking box; CoverageCapture accumulation
        //            visibly building; grey sky + dense fog; wind audio (F10).
    }

    void BlizzardWorld::OnDetach()
    {
    }

    void BlizzardWorld::OnUpdate(WorldContext& ctx)
    {
        // TODO(F14): replace with the SceneRenderer frame (F2).
        DrawPlaceholder(ctx, { 0.85f, 0.92f, 1.0f, 1.0f });
    }

    void BlizzardWorld::OnPanels(WorldContext& ctx)
    {
        (void)ctx;
        ImGui::Begin("World Settings");
        ImGui::TextWrapped("Blizzard Peak — skeleton placeholder.");
        ImGui::Separator();
        ImGui::TextWrapped("Content lands with work order F14 "
                           "(docs/plans/10-phase11-frontier-plan.md).");
        ImGui::End();
    }

} // namespace Frontier
