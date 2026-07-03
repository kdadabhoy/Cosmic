// MirrorLakeWorld.cpp — golden-hour mirror lake (skeleton; see MirrorLakeWorld.h).

#include "worlds/MirrorLakeWorld.h"

#include "ui/IconsLucide.h"

#include <imgui.h>

namespace Frontier
{
    const WorldInfo& MirrorLakeWorld::GetInfo() const
    {
        static const WorldInfo info{
            "Dawn Mirror Lake",
            ICON_LC_DROPLETS,
            "Glass-calm water at golden hour — mirror reflections and mist banks",
            { 0.0f, 25.0f, 160.0f },
            0.0f, -6.0f,
            { 0.16f, 0.12f, 0.10f, 1.0f }
        };
        return info;
    }

    void MirrorLakeWorld::OnAttach()
    {
        // TODO(F15): basin terrain + calm water v2 (tiny waves, caustics,
        //            high-res reflection), golden-hour sun + lens flare (F7),
        //            mist-bank emitters, instanced pine shoreline (F5), fish
        //            splash rings via SampleHeight, loon/wind ambience (F10).
    }

    void MirrorLakeWorld::OnDetach()
    {
    }

    void MirrorLakeWorld::OnUpdate(WorldContext& ctx)
    {
        // TODO(F15): replace with the SceneRenderer frame (F2).
        DrawPlaceholder(ctx, { 1.0f, 0.72f, 0.35f, 1.0f });
    }

    void MirrorLakeWorld::OnPanels(WorldContext& ctx)
    {
        (void)ctx;
        ImGui::Begin("World Settings");
        ImGui::TextWrapped("Dawn Mirror Lake — skeleton placeholder.");
        ImGui::Separator();
        ImGui::TextWrapped("Content lands with work order F15 "
                           "(docs/plans/10-phase11-frontier-plan.md).");
        ImGui::End();
    }

} // namespace Frontier
