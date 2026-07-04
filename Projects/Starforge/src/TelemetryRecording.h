#pragma once

// TelemetryRecording.h
//
// ============================================================================
// Starforge — recorded-field marks (E20).
// ============================================================================
//
// A "recorded channel" is a reflected numeric field the user marked for
// telemetry capture (Inspector right-click, or the panel's "Add from
// selection"). Marks are keyed by the entity's stable UUID so they survive the
// Play snapshot rebuild (the runtime scene is a fresh copy; UUIDs are
// preserved, entt handles are not). The set lives on the EditorContext hub; the
// TelemetryPanel resolves it against the runtime scene each Play.
//
// This header holds only plain data + pure helpers; the free functions that
// touch the EditorContext are implemented in the .cpp (which owns the include).
// ============================================================================

#include <Cosmic.h>

#include <cstdint>
#include <string>

namespace Starforge
{
    struct EditorContext;   // fwd — defined in EditorContext.h

    namespace Telemetry
    {
        // One recorded scalar channel. Comp is the vector/quat sub-component
        // (-1 for a scalar field). Reflected fields only — script-pushed channels
        // are discovered at runtime and never live in this set.
        struct RecordedChannel
        {
            uint64_t      Entity = 0;   // IDComponent UUID (stable across Play)
            entt::id_type TypeId = 0;   // reflected component type hash
            std::string   Field;        // reflected field name
            int           Comp   = -1;  // vec/quat sub-index (-1 == scalar)
        };

        // -------------------------------------------------------------------
        // Pure helpers (no EditorContext dependency)
        // -------------------------------------------------------------------

        // Numeric kinds we can plot. String / AssetPath / EntityRef are excluded.
        bool IsRecordable(Cosmic::Reflect::FieldKind k);

        // How many scalar channels a field expands to (1, or 2/3/4 for vec/quat).
        int ComponentCount(Cosmic::Reflect::FieldKind k);

        // "x"/"y"/"z"/"w" for a multi-component field, "" for a scalar.
        const char* AxisSuffix(Cosmic::Reflect::FieldKind k, int comp);

        // -------------------------------------------------------------------
        // EditorContext-backed operations (implemented in the .cpp)
        // -------------------------------------------------------------------

        // Is ANY component of (uuid, typeId, field) currently marked?
        bool IsRecorded(const EditorContext& ctx, uint64_t uuid,
                        entt::id_type typeId, const std::string& field);

        // Toggle a whole field: enabling adds one channel per component, disabling
        // removes them all.
        void ToggleRecorded(EditorContext& ctx, uint64_t uuid, entt::id_type typeId,
                            const Cosmic::Reflect::FieldDescriptor& field);

        // Mark every recordable field of an entity (the panel's "Add from
        // selection"). No-op for an invalid entity.
        void RecordAllFields(EditorContext& ctx, Cosmic::Entity entity);
    }
}
