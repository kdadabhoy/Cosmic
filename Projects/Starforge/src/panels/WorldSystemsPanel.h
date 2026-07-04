#pragma once

// panels/WorldSystemsPanel.h
//
// ============================================================================
// Starforge — World Systems panel (E18): terrain / water / particle authoring.
// ============================================================================
//
// Author the scene's world-system components on the selected entity through
// their reflected recipes (Cosmic::TerrainComponent / WaterComponent /
// ParticleEmitterComponent). Reflection-driven field editing with per-edit undo
// (mirrors the Environment panel), plus the three things a raw Inspector cannot
// give: a JobSystem-offloaded terrain Regenerate button (the terrain build is
// slow), water/particle presets, and `.cemitter` save/load. v1 = parameters,
// not sculpt/paint brushes (§9 P5, parked).
// ============================================================================

#include <Cosmic.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Starforge
{
    struct EditorContext;

    class WorldSystemsPanel
    {
    public:
        // Poll the async terrain build every frame (independent of panel
        // visibility) so a Regenerate result lands even if the window is closed.
        void OnUpdate(EditorContext& ctx);
        void OnImGuiRender(EditorContext& ctx);

    private:
        void DrawTerrain(EditorContext& ctx, Cosmic::Entity e);
        void DrawWater(EditorContext& ctx, Cosmic::Entity e);
        void DrawParticles(EditorContext& ctx, Cosmic::Entity e);

        // Reflection-driven field rows for one component, with per-edit undo.
        void DrawReflected(EditorContext& ctx, Cosmic::Entity e,
                           const Cosmic::Reflect::TypeDescriptor* desc, const char* labelPrefix);

        // Add a recipe-authored world component to `e` (sets UseRecipe), undoable.
        void AddRecipeComponent(EditorContext& ctx, Cosmic::Entity e, entt::id_type typeId);

        // Kick a background terrain build (JobSystem) for the entity's recipe.
        void StartTerrainBuild(EditorContext& ctx, Cosmic::Entity e);

        // Per-edit undo capture (one active drag at a time across all sections).
        Cosmic::Reflect::FieldValue m_ActiveBefore;
        bool                        m_HasActive = false;

        // Async terrain build state (shared with the worker via shared_ptr so it
        // outlives the panel if the app closes mid-build).
        bool                                          m_TerrainBuilding = false;
        std::shared_ptr<std::atomic<bool>>            m_BuildDone;
        std::shared_ptr<Cosmic::Ref<Cosmic::Terrain>> m_BuildResult;
        uint64_t                                      m_BuildEntity = 0;
        std::size_t                                   m_BuildSignature = 0;

        char m_EmitterName[128] = "campfire";
    };
}
