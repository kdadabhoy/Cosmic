// editors/PostChainEditor.cpp — see PostChainEditor.h. Q6 post-chain graph view.

#include "editors/PostChainEditor.h"
#include "EditorContext.h"
#include "commands/EditorCommands.h"
#include "widgets/PropertyRows.h"

#include "reflect/TypeRegistry.h"
#include "reflect/TypeDescriptor.h"

#include <imgui.h>

using namespace Cosmic;
using Cosmic::Reflect::FieldValue;
using Cosmic::Reflect::TypeDescriptor;

namespace Starforge
{
    namespace
    {
        // The scene keeps exactly one Environment entity (E4) — same helper the
        // Environment panel uses, so both act on the same component.
        Entity FindOrCreateEnvironment(EditorContext& ctx)
        {
            for (auto h : ctx.Scene->View<EnvironmentComponent>())
                return Entity(h, ctx.Scene.get());
            Entity e = ctx.Scene->CreateEntity("Environment");
            e.AddComponent<EnvironmentComponent>();
            ctx.MarkDirty();
            return e;
        }

        // Fixed-chain node ids + pin/link encoding (read-only topology).
        uintptr_t InPin(uintptr_t node)  { return 100 + node; }
        uintptr_t OutPin(uintptr_t node) { return 200 + node; }
        uintptr_t LinkOf(uintptr_t node) { return 300 + node; }   // node -> node+1
    }

    void PostChainEditor::DrawEnvField(EditorContext& ctx, Entity env,
                                       const TypeDescriptor* desc, void* comp, const char* field)
    {
        const auto* f = desc->FindField(field);
        if (!f) return;

        PropertyRows::SlotContext slot{ &ctx.Preview, &ctx.PendingRevealAsset };
        PropertyRows::Result res = PropertyRows::DrawField(*f, comp, /*mixed*/ false, &slot);
        if (res.Activated) { m_ActiveBefore = res.PreValue; m_HasActive = true; }
        if (res.Committed)
        {
            FieldValue before = m_HasActive ? m_ActiveBefore : res.PreValue;
            // IDENTICAL to the Environment panel: same label, typeId, field.
            Commands::CommitFieldEditFor(ctx, env, "Env " + f->Name,
                                         desc->TypeId, f->Name, before, res.PostValue);
            m_HasActive = false;
        }
        if (res.Changed)
            ctx.MarkDirty();
    }

    void PostChainEditor::OnImGuiRender(EditorContext& ctx, bool* pOpen)
    {
        if (!ImGui::Begin("Post Chain", pOpen))
        {
            ImGui::End();
            return;
        }
        if (!ctx.ProjectOpen || !ctx.Scene)
        {
            ImGui::TextDisabled("Open a project to view its post-processing chain.");
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

        ImGui::TextDisabled("The fixed HDR post chain (read-only topology). Edits here are "
                            "identical to Environment-panel edits (same undo).");

        m_Canvas.Begin("post_chain");

        // Fixed left-to-right layout (set once; the topology can't be rewired).
        if (!m_Placed)
        {
            for (int i = 1; i <= 6; ++i)
                m_Canvas.SetNodePosition((uintptr_t)i, ImVec2(30.0f + (i - 1) * 235.0f, 40.0f));
            m_Placed = true;
        }

        auto header = [&](uintptr_t id, const char* title)
        {
            ed::BeginPin(InPin(id), ed::PinKind::Input);
            ImGui::TextUnformatted(id == 1 ? "  " : "->");
            ed::EndPin();
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.4f, 1.0f), "%s", title);
        };
        auto footer = [&](uintptr_t id)
        {
            ed::BeginPin(OutPin(id), ed::PinKind::Output);
            ImGui::TextUnformatted(id == 6 ? "  " : "->");
            ed::EndPin();
        };

        ImGui::PushItemWidth(130.0f);

        // 1 — Scene source.
        ed::BeginNode(ed::NodeId(1)); ImGui::PushID(1);
        header(1, "Scene");
        ImGui::TextDisabled("HDR RGBA16F");
        footer(1);
        ImGui::PopID(); ed::EndNode();

        // 2 — SSAO.
        ed::BeginNode(ed::NodeId(2)); ImGui::PushID(2);
        header(2, "SSAO");
        DrawEnvField(ctx, env, desc, comp, "SSAO");
        DrawEnvField(ctx, env, desc, comp, "SsaoRadius");
        footer(2);
        ImGui::PopID(); ed::EndNode();

        // 3 — Bloom.
        ed::BeginNode(ed::NodeId(3)); ImGui::PushID(3);
        header(3, "Bloom");
        DrawEnvField(ctx, env, desc, comp, "Bloom");
        DrawEnvField(ctx, env, desc, comp, "BloomThreshold");
        DrawEnvField(ctx, env, desc, comp, "BloomIntensity");
        footer(3);
        ImGui::PopID(); ed::EndNode();

        // 4 — God Rays (engine pass, not authored on EnvironmentComponent).
        ed::BeginNode(ed::NodeId(4)); ImGui::PushID(4);
        header(4, "God Rays");
        ImGui::TextDisabled("app-driven pass\n(needs shadows)");
        footer(4);
        ImGui::PopID(); ed::EndNode();

        // 5 — Tonemap (fog / vignette / haze fold in here).
        ed::BeginNode(ed::NodeId(5)); ImGui::PushID(5);
        header(5, "Tonemap");
        DrawEnvField(ctx, env, desc, comp, "Exposure");
        ImGui::SeparatorText("Fog");
        DrawEnvField(ctx, env, desc, comp, "Fog");
        DrawEnvField(ctx, env, desc, comp, "FogDensity");
        ImGui::SeparatorText("Vignette");
        DrawEnvField(ctx, env, desc, comp, "Vignette");
        DrawEnvField(ctx, env, desc, comp, "VignetteAmount");
        DrawEnvField(ctx, env, desc, comp, "VignetteColor");
        footer(5);
        ImGui::PopID(); ed::EndNode();

        // 6 — FXAA.
        ed::BeginNode(ed::NodeId(6)); ImGui::PushID(6);
        header(6, "FXAA");
        DrawEnvField(ctx, env, desc, comp, "FXAA");
        footer(6);
        ImGui::PopID(); ed::EndNode();

        ImGui::PopItemWidth();

        // Fixed links (read-only): 1→2→3→4→5→6.
        for (uintptr_t i = 1; i <= 5; ++i)
            ed::Link(ed::LinkId(LinkOf(i)), ed::PinId(OutPin(i)), ed::PinId(InPin(i + 1)));

        // Topology is READ-ONLY: drain interaction queries but apply nothing (no
        // re-wiring, no node/link deletion — this is a VIEW, decision #13).
        NodeCanvas::Edits edits;
        m_Canvas.QueryEdits(edits);
        (void)edits;

        m_Canvas.End();
        ImGui::End();
    }
}
