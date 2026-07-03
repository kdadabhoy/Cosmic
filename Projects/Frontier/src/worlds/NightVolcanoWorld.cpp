// NightVolcanoWorld.cpp — night caldera close-up (skeleton; see NightVolcanoWorld.h).

#include "worlds/NightVolcanoWorld.h"

#include "ui/IconsLucide.h"

#include <imgui.h>

namespace Frontier
{
    const WorldInfo& NightVolcanoWorld::GetInfo() const
    {
        static const WorldInfo info{
            "Night Volcano",
            ICON_LC_FLAME,
            "Glowing caldera at night — lava, embers, god rays through smoke",
            { 0.0f, 120.0f, 420.0f },
            0.0f, -14.0f,
            { 0.020f, 0.012f, 0.016f, 1.0f }
        };
        return info;
    }

    void NightVolcanoWorld::OnAttach()
    {
        // TODO(F13): night-tuned volcano scene — terrain cone + caldera, lava
        //            lake + flows (FlowEmissive), smoke/ember/haze emitters,
        //            moon-lit night sky (F7), god rays, rumble (F10).
    }

    void NightVolcanoWorld::OnDetach()
    {
    }

    void NightVolcanoWorld::OnUpdate(WorldContext& ctx)
    {
        // TODO(F13): replace with the SceneRenderer frame (F2).
        DrawPlaceholder(ctx, { 1.0f, 0.45f, 0.15f, 1.0f });
    }

    void NightVolcanoWorld::OnPanels(WorldContext& ctx)
    {
        (void)ctx;
        ImGui::Begin("World Settings");
        ImGui::TextWrapped("Night Volcano — skeleton placeholder.");
        ImGui::Separator();
        ImGui::TextWrapped("Content lands with work order F13 "
                           "(docs/plans/10-phase11-frontier-plan.md).");
        ImGui::End();
    }

} // namespace Frontier
