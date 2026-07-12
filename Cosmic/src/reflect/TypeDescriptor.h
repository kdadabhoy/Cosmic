#pragma once
// reflect/TypeDescriptor.h
//
// ============================================================================
// Cosmic reflection — runtime type + field descriptors (Phase 13 / E1).
// ============================================================================
//
// A TypeDescriptor is the engine's runtime knowledge of a component type: its
// serialization/display name, category, an ordered list of reflected fields,
// and a set of type-erased thunks that add/query/remove/copy the component on
// an entt registry WITHOUT the caller knowing the C++ type at the call site.
//
// One definition drives the Inspector (E8), the SceneSerializer (E2), the undo
// CommandStack (E7), and the telemetry panel (E20). The registry that owns
// these descriptors lives in the engine DLL (see TypeRegistry.h) so every DLL
// in the process shares one instance.
//
// This header is GL-free and headless-testable.
// ============================================================================

#include "core/Core.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace Cosmic::Reflect
{
    // ------------------------------------------------------------------------
    // FieldKind — the closed set of reflectable field types. The C++ member's
    // type is deduced to one of these at registration; UI + serialization
    // branch on it. Color is a glm::vec4 flagged for a colour picker; AssetPath
    // is a std::string flagged with an asset-type tag; EntityRef is a 64-bit
    // UUID (see core/UUID.h, E2); Enum is an int32 with a name<->value table.
    // ------------------------------------------------------------------------
    enum class FieldKind
    {
        Bool, Int32, UInt32, Float,
        Vec2, Vec3, Vec4, Quat,
        Color, String, AssetPath, EntityRef, Enum
    };

    // Per-field flags (bit mask).
    enum FieldFlags : uint32_t
    {
        Field_None            = 0,
        Field_ReadOnly        = 1u << 0,  // inspector shows it disabled
        Field_HideInInspector = 1u << 1,  // still serialized, just not shown
        Field_NoSerialize     = 1u << 2,  // runtime-only, skipped by the serializer
        // Bool convenience (T12/T13): omit from serialization when true, so a
        // default-true flag (Enabled / Active) leaves unchanged scenes
        // byte-identical and only writes the key once it is toggled to false.
        Field_OmitIfTrue      = 1u << 3
    };

    struct EnumEntry
    {
        std::string Name;
        int32_t     Value = 0;
    };

    // Physical unit a numeric field is expressed in (reflection metadata v2,
    // Phase 23 / T1). Purely a UI/doc-gen hint — the Inspector annotates the row
    // (° for Degrees, m / s), sliders honour it, and doc-gen can print it. A
    // field carries exactly one unit, so this is an enum, not a bit mask. None is
    // the default => today's unitless behaviour, byte-identical.
    enum class FieldUnits : uint8_t { None = 0, Degrees, Meters, Seconds };

    // Optional UI/serialization hints attached to a field. Every member defaults
    // to "absent", so a field registered without any hint call behaves exactly as
    // it did before this struct grew (T1 compat requirement). Hints are NEVER
    // serialized — they are registration-time metadata only.
    struct FieldHints
    {
        bool  HasRange = false;
        float Min = 0.0f, Max = 0.0f;
        float Step = 0.0f;                 // 0 => widget default
        // Human-readable documentation for the field (gap analysis §14.1 calls
        // this "Doc"). Rendered as the row's ⓘ/hover text (T10). Set via either
        // .Doc(...) (the spec name) or the older .Tooltip(...) alias.
        std::string Tooltip;
        std::string AssetType;             // AssetPath: "mesh" / "texture" / "material" / ...
        FieldUnits  Units = FieldUnits::None; // numeric fields: ° / m / s annotation
        std::vector<EnumEntry> EnumEntries; // Enum: name <-> value table
    };

    // ------------------------------------------------------------------------
    // FieldValue — a boxed reflected value. The variant alternatives map 1:1
    // onto the storable field types; Enum is boxed as int32_t, EntityRef as
    // uint64_t, Color/AssetPath reuse vec4/string. Serializer and undo move
    // values through this type so no per-field C++ code is needed anywhere.
    // ------------------------------------------------------------------------
    using FieldValue = std::variant<
        bool, int32_t, uint32_t, uint64_t, float,
        glm::vec2, glm::vec3, glm::vec4, glm::quat,
        std::string>;

    // ------------------------------------------------------------------------
    // FieldDescriptor — one reflected member of a component.
    // ------------------------------------------------------------------------
    struct FieldDescriptor
    {
        std::string Name;
        FieldKind   Kind  = FieldKind::Float;
        uint32_t    Flags = Field_None;
        FieldHints  Hints;

        // Type-erased read/write. `component` points at the component instance
        // (the raw pointer returned by TypeDescriptor::Get). Read boxes the
        // member into a FieldValue; Write unboxes and assigns.
        std::function<FieldValue(const void* component)>          Read;
        std::function<void(void* component, const FieldValue& v)> Write;

        FieldValue Get(const void* component) const { return Read(component); }
        void       Set(void* component, const FieldValue& v) const { Write(component, v); }

        bool HasFlag(FieldFlags f) const { return (Flags & f) != 0; }
    };

    // ------------------------------------------------------------------------
    // TypeDescriptor — one reflected component type.
    // ------------------------------------------------------------------------
    struct TypeDescriptor
    {
        entt::id_type TypeId = 0;   // == entt::type_hash<T>::value() (storage key)
        std::string   Name;         // serialization + display name ("Transform")
        std::string   Category;     // inspector grouping ("Core", "Rendering", ...)
        std::vector<FieldDescriptor> Fields;

        // entt glue — captured at registration, so callers stay type-agnostic.
        // Add   : get-or-create the component, returns its address (nullptr for
        //         empty/tag types, which carry no data).
        // Has   : presence test.
        // Remove: remove if present (no-op otherwise).
        // Get   : address of the component, or nullptr if absent.
        // Copy  : get-or-create on dst and copy-assign from a source instance.
        std::function<void*(entt::registry&, entt::entity)>              Add;
        std::function<bool(const entt::registry&, entt::entity)>         Has;
        std::function<void(entt::registry&, entt::entity)>              Remove;
        std::function<void*(entt::registry&, entt::entity)>              Get;
        std::function<void(entt::registry&, entt::entity, const void*)> Copy;

        const FieldDescriptor* FindField(const std::string& name) const
        {
            for (const auto& f : Fields)
                if (f.Name == name)
                    return &f;
            return nullptr;
        }
    };
}
