#pragma once

// commands/EditorCommands.h
//
// ============================================================================
// Starforge — concrete editor commands (E7), exposed as free functions.
// ============================================================================
//
// Every scene mutation in the editor routes through one of these so it lands on
// the EditorContext::CommandStack (undo/redo). The concrete Cosmic::ICommand
// subclasses live entirely in the .cpp; panels only call these helpers. Entities
// are referenced by UUID inside the commands (never a raw entt handle) so undo
// after a delete/recreate stays valid (plan §E7 gotcha).
// ============================================================================

#include <Cosmic.h>
#include <entt/entt.hpp>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Starforge
{
    struct EditorContext;

    namespace Commands
    {
        using FieldValue = Cosmic::Reflect::FieldValue;

        // A discrete change NOT yet applied (checkbox toggle handled elsewhere,
        // "Add Component", etc.). Executes now (applies `after`), records undo.
        void SetField(EditorContext& ctx, Cosmic::Entity e, entt::id_type typeId,
                      const std::string& field, FieldValue after);

        // A change ALREADY applied live to the primary entity this frame (drag /
        // instantaneous widget). Fans `after` to the rest of the selection that
        // owns the component and records ONE batch step (drag-coalescing key so a
        // continuous drag is a single undo). `primaryBefore` is the drag-start
        // value of the primary.
        void CommitFieldEdit(EditorContext& ctx, const std::string& label,
                             entt::id_type typeId, const std::string& field,
                             FieldValue primaryBefore, FieldValue after);

        // A reflected-field change ALREADY applied live to a SPECIFIC entity (not
        // the current selection) — records one undo step. Used by panels that edit
        // a known entity, e.g. the Environment panel editing the scene's
        // "Environment" entity. `before` is the value at drag-start.
        void CommitFieldEditFor(EditorContext& ctx, Cosmic::Entity target, const std::string& label,
                                entt::id_type typeId, const std::string& field,
                                FieldValue before, FieldValue after);

        // Create a fresh entity (optionally parented), letting `build` add its
        // components. Selects it, records undo, returns it.
        Cosmic::Entity Create(EditorContext& ctx, const std::string& name,
                              Cosmic::Entity parent, std::function<void(Cosmic::Entity)> build);

        // Clone `src` + its subtree with fresh UUIDs; select + record; returns the clone root.
        Cosmic::Entity Duplicate(EditorContext& ctx, Cosmic::Entity src);

        // Destroy `e` + its subtree (undo restores it with identical UUIDs).
        void Destroy(EditorContext& ctx, Cosmic::Entity e);

        // Re-parent `child` under `newParent` (invalid parent = detach to root).
        void Reparent(EditorContext& ctx, Cosmic::Entity child, Cosmic::Entity newParent);

        // Add / remove a component by reflected type.
        void AddComponent(EditorContext& ctx, Cosmic::Entity e, entt::id_type typeId);
        void RemoveComponent(EditorContext& ctx, Cosmic::Entity e, entt::id_type typeId);

        // Commit a gizmo transform edit already applied live; `before` is the
        // TransformComponent captured at drag-start. Coalesces per drag.
        void CommitTransform(EditorContext& ctx, Cosmic::Entity e,
                             const Cosmic::TransformComponent& before);

        // Set one voxel (world voxel coords) on `e`'s VoxelVolume to `newId`,
        // undoable (V4). Applies immediately; records the old id for undo.
        // Consecutive edits sharing `stroke` coalesce into one undo step (a brush
        // drag = one undo). No-op when the id is unchanged or there is no volume.
        void VoxelEdit(EditorContext& ctx, Cosmic::Entity e,
                       const glm::ivec3& voxel, uint16_t newId, int stroke);

        // Set one tilemap cell (grid coords) on `e`'s Tilemap to `value`,
        // undoable (U4). Same stroke-coalescing contract as VoxelEdit: a paint
        // drag sharing `stroke` is ONE undo step. No-op on unchanged/out-of-map.
        void TileEdit(EditorContext& ctx, Cosmic::Entity e,
                      int x, int y, uint16_t value, int stroke);

        // Apply a batch of tilemap cell writes {cellIndex -> value} as ONE undo
        // step (flood fill / rect fill). Cells already holding the value are
        // skipped; an empty effective batch records nothing.
        void TileEditRun(EditorContext& ctx, Cosmic::Entity e,
                         const std::vector<std::pair<uint32_t, uint16_t>>& writes,
                         const char* label);
    }
}
