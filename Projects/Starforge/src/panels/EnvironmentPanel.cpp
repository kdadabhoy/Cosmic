// panels/EnvironmentPanel.cpp — see EnvironmentPanel.h.

#include "panels/EnvironmentPanel.h"
#include "EditorContext.h"
#include "commands/EditorCommands.h"
#include "widgets/PropertyRows.h"

#include <imgui.h>

using namespace Cosmic;
using Cosmic::Reflect::FieldValue;

namespace Starforge
{
    namespace
    {
        // The scene keeps exactly one Environment entity (E4). Find it, or create
        // one on first use so the panel always has something to edit.
        Entity FindOrCreateEnvironment(EditorContext& ctx)
        {
            for (auto h : ctx.Scene->View<EnvironmentComponent>())
                return Entity(h, ctx.Scene.get());

            Entity e = ctx.Scene->CreateEntity("Environment");
            e.AddComponent<EnvironmentComponent>();
            ctx.MarkDirty();
            return e;
        }
    }

    void EnvironmentPanel::OnImGuiRender(EditorContext& ctx, bool* pOpen)
    {
        if (ImGui::Begin("Environment", pOpen))
        {
            if (!ctx.ProjectOpen || !ctx.Scene)
            {
                ImGui::TextDisabled("Open a project to edit its environment.");
                ImGui::End();
                return;
            }

            Entity env = FindOrCreateEnvironment(ctx);

            const auto* desc = Reflect::GetRegistry().Find<EnvironmentComponent>();
            void* comp = desc ? desc->Get(ctx.Scene->GetRegistry(), (entt::entity)env) : nullptr;
            if (!desc || !comp)
            {
                ImGui::TextDisabled("EnvironmentComponent unavailable.");
                ImGui::End();
                return;
            }

            ImGui::TextDisabled("Sun, sky, time-of-day, fog, IBL and post — drives the renderer.");

            // H3: tell the user WHY the Sun fields here may look inert — if the scene
            // has a DirectionalLight entity, THAT light defines the sun (this panel's
            // Sun fields only take effect via the owned sky/IBL); with none, the sun
            // below is the whole story.
            bool hasDirLight = false;
            for (auto h : ctx.Scene->View<DirectionalLightComponent>()) { (void)h; hasDirLight = true; break; }
            if (hasDirLight)
                ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.35f, 1.0f),
                                   "A DirectionalLight entity defines the sun for lit meshes.");
            else
                ImGui::TextColored(ImVec4(0.60f, 0.65f, 0.72f, 1.0f),
                                   "Default sun (no DirectionalLight in scene).");
            ImGui::Separator();

            for (const auto& f : desc->Fields)
            {
                if (f.HasFlag(Reflect::Field_HideInInspector))
                    continue;

                PropertyRows::Result res = PropertyRows::DrawField(f, comp, /*mixed*/ false);
                if (res.Activated) { m_ActiveBefore = res.PreValue; m_HasActive = true; }
                if (res.Committed)
                {
                    FieldValue before = m_HasActive ? m_ActiveBefore : res.PreValue;
                    Commands::CommitFieldEditFor(ctx, env, "Env " + f.Name,
                                                 desc->TypeId, f.Name, before, res.PostValue);
                    m_HasActive = false;
                }
                if (res.Changed)
                    ctx.MarkDirty();
            }
        }
        ImGui::End();
    }
}
