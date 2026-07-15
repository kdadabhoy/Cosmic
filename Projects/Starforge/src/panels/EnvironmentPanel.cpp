// panels/EnvironmentPanel.cpp — see EnvironmentPanel.h.

#include "panels/EnvironmentPanel.h"
#include "EditorContext.h"
#include "commands/EditorCommands.h"
#include "widgets/PropertyRows.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

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

        // X2 — SunDirection (the direction light TRAVELS) <-> sun Elevation/Azimuth.
        // toSun = -travelDir; elevation is its angle above the horizon, azimuth its
        // compass angle. The pair is an EXACT inverse of the vec3 (see the panel
        // note): angles derived from a unit toSun rebuild that same unit vector, and
        // we preserve the original magnitude, so vec3 -> angles -> vec3 == identity.
        void DirToSunAngles(const glm::vec3& travelDir, float& elevDeg, float& azimDeg)
        {
            const float len = glm::length(travelDir);
            glm::vec3 toSun = len > 1e-6f ? -travelDir / len : glm::vec3(0.0f, 1.0f, 0.0f);
            elevDeg = glm::degrees(std::asin(glm::clamp(toSun.y, -1.0f, 1.0f)));
            azimDeg = glm::degrees(std::atan2(toSun.z, toSun.x));
        }

        glm::vec3 SunAnglesToDir(float elevDeg, float azimDeg, float length)
        {
            const float el = glm::radians(elevDeg);
            const float az = glm::radians(azimDeg);
            const glm::vec3 toSun(std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az));
            return -toSun * length;
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

                PropertyRows::SlotContext slot{ &ctx.Preview, &ctx.PendingRevealAsset };
                PropertyRows::Result res = PropertyRows::DrawField(f, comp, /*mixed*/ false, &slot);
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

                // X2 — draw the Elevation/Azimuth pair right under the raw vector.
                if (f.Name == "SunDirection")
                    DrawSunAngleWidget(ctx, env, comp, *desc, f);
            }
        }
        ImGui::End();
    }

    // X2 — sun Elevation/Azimuth paired widget. Displays angles derived from the
    // current SunDirection every frame (never writing back), and writes the vector
    // ONLY while a slider is dragged — so opening the panel can't perturb it. The
    // conversion is an exact inverse (DirToSunAngles/SunAnglesToDir), and the whole
    // drag commits as ONE undoable FieldEdit through the same path as the raw vec3.
    void EnvironmentPanel::DrawSunAngleWidget(EditorContext& ctx, Entity env, void* comp,
                                              const Reflect::TypeDescriptor& desc,
                                              const Reflect::FieldDescriptor& field)
    {
        FieldValue cur = field.Get(comp);
        if (!std::holds_alternative<glm::vec3>(cur))
            return;

        const glm::vec3 dir = std::get<glm::vec3>(cur);
        const float     len = glm::length(dir);
        const float     preservedLen = len > 1e-6f ? len : 1.0f;

        float elev = 0.0f, azim = 0.0f;
        DirToSunAngles(dir, elev, azim);

        ImGui::Indent();
        ImGui::PushID("SunAngle");
        ImGui::TextDisabled("Elevation / Azimuth (drives Sun direction)");

        bool changed = false;
        bool activated = false, released = false;

        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::SliderFloat("Elevation", &elev, -89.99f, 89.99f, "%.2f deg")) changed = true;
        activated |= ImGui::IsItemActivated();
        released  |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::SliderFloat("Azimuth", &azim, -180.0f, 180.0f, "%.2f deg")) changed = true;
        activated |= ImGui::IsItemActivated();
        released  |= ImGui::IsItemDeactivatedAfterEdit();

        // Capture the pre-drag vector once, on the first frame of the drag.
        if (activated && !m_SunAngleActive)
        {
            m_SunAngleBefore = dir;
            m_SunAngleActive = true;
        }

        // Apply live (so the vec3 row + viewport track the drag), like PropertyRows.
        if (changed)
        {
            const glm::vec3 newDir = SunAnglesToDir(elev, azim, preservedLen);
            field.Set(comp, FieldValue{ newDir });
            ctx.MarkDirty();
        }

        // One undo step for the whole drag, through the raw-vec3 commit path.
        if (released && m_SunAngleActive)
        {
            FieldValue after = field.Get(comp);
            Commands::CommitFieldEditFor(ctx, env, "Env SunDirection",
                                         desc.TypeId, field.Name,
                                         FieldValue{ m_SunAngleBefore }, after);
            m_SunAngleActive = false;
        }

        ImGui::PopID();
        ImGui::Unindent();
    }
}
