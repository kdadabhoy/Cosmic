// panels/WorldSystemsPanel.cpp — see WorldSystemsPanel.h.

#include "panels/WorldSystemsPanel.h"
#include "EditorContext.h"
#include "commands/EditorCommands.h"
#include "widgets/PropertyRows.h"

#include <imgui.h>

#include <filesystem>
#include <string>

using namespace Cosmic;
using Cosmic::Reflect::FieldValue;
using Cosmic::Reflect::TypeDescriptor;
namespace fs = std::filesystem;

namespace Starforge
{
    namespace
    {
        template<typename T>
        const TypeDescriptor* DescOf() { return Reflect::GetRegistry().Find<T>(); }

        uint64_t EntityId(Entity e)
        {
            return e.HasComponent<IDComponent>() ? (uint64_t)e.GetComponent<IDComponent>().ID : 0;
        }
    }

    // =========================================================================
    // Async terrain build (JobSystem)
    // =========================================================================
    void WorldSystemsPanel::StartTerrainBuild(EditorContext& ctx, Entity e)
    {
        if (m_TerrainBuilding)
        {
            ctx.Log("[World] A terrain build is already running.", LogSeverity::Warn);
            return;
        }
        if (!e || !e.HasComponent<TerrainComponent>())
            return;

        auto& tc = e.GetComponent<TerrainComponent>();

        // Build + resolve the spec on the MAIN thread (VFS + GL texture loads),
        // then run the CPU heightfield build (GL-free) on a worker.
        TerrainSpecification spec = BuildTerrainSpec(tc);
        ResolveTerrainSpecAssets(tc, spec);

        const std::size_t sig = TerrainRecipeSignature(tc);
        tc.BuiltSignature = sig;   // claim it so Scene::SyncWorldSystems won't also build

        m_BuildEntity    = EntityId(e);
        m_BuildSignature = sig;
        m_BuildDone      = std::make_shared<std::atomic<bool>>(false);
        m_BuildResult    = std::make_shared<Ref<Terrain>>();
        m_TerrainBuilding = true;

        auto done = m_BuildDone;
        auto out  = m_BuildResult;
        JobSystem::Get().Submit([spec, done, out]()
        {
            *out = Terrain::Create(spec);
            done->store(true, std::memory_order_release);
        });
        ctx.Log("[World] Building terrain in the background...");
    }

    void WorldSystemsPanel::OnUpdate(EditorContext& ctx)
    {
        if (!m_TerrainBuilding || !m_BuildDone || !m_BuildDone->load(std::memory_order_acquire))
            return;

        m_TerrainBuilding = false;
        Ref<Terrain> built = m_BuildResult ? *m_BuildResult : nullptr;
        m_BuildDone.reset();
        m_BuildResult.reset();

        Entity e = ctx.Scene ? ctx.Scene->FindByUUID(UUID(m_BuildEntity)) : Entity{};
        if (e && e.HasComponent<TerrainComponent>() && built)
        {
            auto& tc = e.GetComponent<TerrainComponent>();
            tc.TerrainAsset   = built;
            tc.BuiltSignature = m_BuildSignature;
            ctx.MarkDirty();
            ctx.Log("[World] Terrain ready.");
        }
        else if (!built)
        {
            ctx.Log("[World] Terrain build failed (see the log — bad resolution or heightmap?).",
                    LogSeverity::Error);
        }
    }

    // =========================================================================
    // Reflected field rows (per-edit undo, mirrors the Environment panel)
    // =========================================================================
    void WorldSystemsPanel::DrawReflected(EditorContext& ctx, Entity e,
                                          const TypeDescriptor* desc, const char* labelPrefix)
    {
        if (!desc)
            return;
        void* comp = desc->Get(ctx.Scene->GetRegistry(), (entt::entity)e);
        if (!comp)
            return;

        ImGui::PushID(labelPrefix);
        for (const auto& f : desc->Fields)
        {
            if (f.HasFlag(Reflect::Field_HideInInspector))
                continue;

            PropertyRows::Result res = PropertyRows::DrawField(f, comp, /*mixed*/ false);
            if (res.Activated) { m_ActiveBefore = res.PreValue; m_HasActive = true; }
            if (res.Committed)
            {
                FieldValue before = m_HasActive ? m_ActiveBefore : res.PreValue;
                Commands::CommitFieldEditFor(ctx, e, std::string(labelPrefix) + " " + f.Name,
                                             desc->TypeId, f.Name, before, res.PostValue);
                m_HasActive = false;
            }
            if (res.Changed)
                ctx.MarkDirty();
        }
        ImGui::PopID();
    }

    void WorldSystemsPanel::AddRecipeComponent(EditorContext& ctx, Entity e, entt::id_type typeId)
    {
        Commands::AddComponent(ctx, e, typeId);
        const TypeDescriptor* desc = Reflect::GetRegistry().Find(typeId);
        if (!desc) return;
        void* comp = desc->Get(ctx.Scene->GetRegistry(), (entt::entity)e);
        if (const auto* f = desc->FindField("UseRecipe"); f && comp)
            f->Set(comp, FieldValue{ true });   // part of the fresh component (undo removes it all)
        ctx.MarkDirty();
    }

    // =========================================================================
    // Sections
    // =========================================================================
    void WorldSystemsPanel::DrawTerrain(EditorContext& ctx, Entity e)
    {
        const entt::id_type tid = DescOf<TerrainComponent>()->TypeId;

        if (!e.HasComponent<TerrainComponent>())
        {
            if (ImGui::Button("Add Terrain"))
            {
                AddRecipeComponent(ctx, e, tid);
                StartTerrainBuild(ctx, e);      // first build is async too
            }
            return;
        }

        if (!ImGui::CollapsingHeader("Terrain", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        DrawReflected(ctx, e, DescOf<TerrainComponent>(), "Terrain");

        auto& tc = e.GetComponent<TerrainComponent>();
        ImGui::Separator();
        const bool buildingThis = m_TerrainBuilding && m_BuildEntity == EntityId(e);
        if (buildingThis)
        {
            ImGui::TextDisabled("Building terrain in the background...");
        }
        else
        {
            if (TerrainRecipeSignature(tc) != tc.BuiltSignature || !tc.TerrainAsset)
                ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.30f, 1.0f),
                                   "Parameters changed — Regenerate to apply.");
            ImGui::BeginDisabled(m_TerrainBuilding);
            if (ImGui::Button("Regenerate Terrain"))
                StartTerrainBuild(ctx, e);
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove##terrain"))
            Commands::RemoveComponent(ctx, e, tid);
    }

    void WorldSystemsPanel::DrawWater(EditorContext& ctx, Entity e)
    {
        const entt::id_type tid = DescOf<WaterComponent>()->TypeId;

        if (!e.HasComponent<WaterComponent>())
        {
            if (ImGui::Button("Add Water"))
                AddRecipeComponent(ctx, e, tid);   // Scene::SyncWorldSystems builds it (cheap)
            return;
        }

        if (!ImGui::CollapsingHeader("Water", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        ImGui::TextDisabled("Pick a preset, then tune. Changes apply live.");
        DrawReflected(ctx, e, DescOf<WaterComponent>(), "Water");
        ImGui::Separator();
        if (ImGui::Button("Remove##water"))
            Commands::RemoveComponent(ctx, e, tid);
    }

    void WorldSystemsPanel::DrawParticles(EditorContext& ctx, Entity e)
    {
        const entt::id_type tid = DescOf<ParticleEmitterComponent>()->TypeId;

        if (!e.HasComponent<ParticleEmitterComponent>())
        {
            if (ImGui::Button("Add Particle Emitter"))
                AddRecipeComponent(ctx, e, tid);   // default recipe = a campfire ember cone
            return;
        }

        if (!ImGui::CollapsingHeader("Particle Emitter", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        ImGui::TextDisabled("Live preview updates in the viewport. Changes apply live.");
        DrawReflected(ctx, e, DescOf<ParticleEmitterComponent>(), "Emitter");

        ImGui::Separator();
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputText("##emittername", m_EmitterName, sizeof(m_EmitterName));
        ImGui::SameLine();
        if (ImGui::Button("Save .cemitter"))
        {
            std::string dir = FileSystem::Resolve("project://emitters");
            std::error_code ec; fs::create_directories(dir, ec);
            std::string path = std::string("project://emitters/") + m_EmitterName + ".cemitter";
            void* comp = DescOf<ParticleEmitterComponent>()->Get(ctx.Scene->GetRegistry(), (entt::entity)e);
            if (comp && SceneSerializer::SaveReflectedToFile(tid, comp, FileSystem::Resolve(path)))
                ctx.Log("[World] Saved emitter preset " + path + ".");
            else
                ctx.Log("[World] Failed to save emitter preset.", LogSeverity::Error);
        }
        ImGui::SameLine();
        if (ImGui::Button("Load .cemitter"))
        {
            std::string path = std::string("project://emitters/") + m_EmitterName + ".cemitter";
            void* comp = DescOf<ParticleEmitterComponent>()->Get(ctx.Scene->GetRegistry(), (entt::entity)e);
            if (comp && SceneSerializer::LoadReflectedFromFile(tid, comp, FileSystem::Resolve(path)))
            {
                auto& pc = e.GetComponent<ParticleEmitterComponent>();
                pc.UseRecipe = true;          // a loaded preset is recipe-authored
                pc.Emitter.reset();           // force Scene::SyncWorldSystems to rebuild
                ctx.MarkDirty();
                ctx.Log("[World] Loaded emitter preset " + path + ".");
            }
            else
            {
                ctx.Log("[World] Failed to load emitter preset " + path + ".", LogSeverity::Error);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove##emitter"))
            Commands::RemoveComponent(ctx, e, tid);
    }

    // =========================================================================
    // Panel
    // =========================================================================
    void WorldSystemsPanel::OnImGuiRender(EditorContext& ctx)
    {
        if (ImGui::Begin("World Systems"))
        {
            if (!ctx.ProjectOpen || !ctx.Scene)
            {
                ImGui::TextDisabled("Open a project to author world systems.");
                ImGui::End();
                return;
            }

            Entity e = ctx.PrimaryEntity();
            if (!e)
            {
                ImGui::TextDisabled("Select an entity, then add Terrain / Water / a Particle Emitter.");
                ImGui::TextDisabled("Tip: Entity ▸ World ▸ ... creates a fresh one.");
                ImGui::End();
                return;
            }

            const std::string tag = e.HasComponent<TagComponent>()
                ? e.GetComponent<TagComponent>().Tag : std::string("Entity");
            ImGui::Text("Entity: %s", tag.c_str());
            ImGui::Separator();

            DrawTerrain(ctx, e);
            DrawWater(ctx, e);
            DrawParticles(ctx, e);
        }
        ImGui::End();
    }
}
