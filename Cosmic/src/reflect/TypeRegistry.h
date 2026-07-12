#pragma once
// reflect/TypeRegistry.h
//
// ============================================================================
// Cosmic reflection — the runtime type registry + builder API (Phase 13 / E1).
// ============================================================================
//
// The registry is a process-wide singleton owned by the ENGINE DLL: call
// Reflect::GetRegistry() from anywhere (engine, editor DLL, game module DLL)
// and get the one instance. Register a component with the fluent builder:
//
//     Reflect::Class<TransformComponent>("Transform", "Core")
//         .Field("Position", &TransformComponent::Position)
//         .Field("Rotation", &TransformComponent::Rotation).Tooltip("Euler degrees")
//         .Field("Scale",    &TransformComponent::Scale);
//
// FieldKind is deduced from the member type; hint calls (.Range/.Step/.Tooltip/
// .Color/.AsAssetPath/.EnumValue/.ReadOnly/...) modify the field just added,
// and return the builder so more .Field() calls chain. The CS_SCRIPT/CS_FIELD
// macros (E11) are thin sugar over this.
//
// GL-free and headless-testable.
// ============================================================================

#include "reflect/TypeDescriptor.h"

#include <string>
#include <type_traits>
#include <unordered_map>

namespace Cosmic::Reflect
{
    // Dependent-false helper for the unsupported-type static_assert.
    template<typename> inline constexpr bool always_false_v = false;

    // Deduce a FieldKind from a C++ member type. Color/AssetPath/EntityRef are
    // opt-in refinements applied by the builder's hint methods, so the raw
    // deduction yields Vec4/String/UInt64-less bases; enums map to Enum.
    template<typename M>
    constexpr FieldKind DeduceKind()
    {
        if constexpr (std::is_same_v<M, bool>)            return FieldKind::Bool;
        else if constexpr (std::is_enum_v<M>)             return FieldKind::Enum;
        else if constexpr (std::is_same_v<M, int32_t>)    return FieldKind::Int32;
        else if constexpr (std::is_same_v<M, uint32_t>)   return FieldKind::UInt32;
        else if constexpr (std::is_same_v<M, uint64_t>)   return FieldKind::EntityRef;
        else if constexpr (std::is_same_v<M, float>)      return FieldKind::Float;
        else if constexpr (std::is_same_v<M, glm::vec2>)  return FieldKind::Vec2;
        else if constexpr (std::is_same_v<M, glm::vec3>)  return FieldKind::Vec3;
        else if constexpr (std::is_same_v<M, glm::vec4>)  return FieldKind::Vec4;
        else if constexpr (std::is_same_v<M, glm::quat>)  return FieldKind::Quat;
        else if constexpr (std::is_same_v<M, std::string>) return FieldKind::String;
        else { static_assert(always_false_v<M>, "Reflect: unsupported field type"); return FieldKind::Float; }
    }

    // ------------------------------------------------------------------------
    // TypeRegistry — owns the descriptors, keyed by entt type hash.
    // ------------------------------------------------------------------------
    class COSMIC_API TypeRegistry
    {
    public:
        TypeDescriptor& GetOrCreate(entt::id_type id, const std::string& name)
        {
            TypeDescriptor& d = m_Types[id];
            d.TypeId = id;
            d.Name   = name;
            m_ByName[name] = id;
            return d;
        }

        // Lookup by entt type hash / by name / by C++ type. Unknown -> nullptr.
        const TypeDescriptor* Find(entt::id_type id) const
        {
            auto it = m_Types.find(id);
            return it == m_Types.end() ? nullptr : &it->second;
        }
        const TypeDescriptor* FindByName(const std::string& name) const
        {
            auto it = m_ByName.find(name);
            return it == m_ByName.end() ? nullptr : Find(it->second);
        }
        template<typename T>
        const TypeDescriptor* Find() const { return Find(entt::type_hash<T>::value()); }

        const std::unordered_map<entt::id_type, TypeDescriptor>& Types() const { return m_Types; }

        // Every registered component the entity currently owns (order is the
        // registry's iteration order; the Inspector sorts by category).
        std::vector<const TypeDescriptor*> ComponentsOf(const entt::registry& reg, entt::entity e) const
        {
            std::vector<const TypeDescriptor*> out;
            for (const auto& [id, desc] : m_Types)
                if (desc.Has && desc.Has(reg, e))
                    out.push_back(&desc);
            return out;
        }

    private:
        std::unordered_map<entt::id_type, TypeDescriptor> m_Types;
        std::unordered_map<std::string, entt::id_type>    m_ByName;
    };

    // The one process-wide registry (defined in TypeRegistry.cpp inside the
    // engine DLL). First call also registers every built-in engine component.
    COSMIC_API TypeRegistry& GetRegistry();

    // Idempotently register all engine-side components (called by GetRegistry
    // on first use; exposed for tests / explicit ordering).
    COSMIC_API void RegisterEngineTypes(TypeRegistry& registry);

    // ------------------------------------------------------------------------
    // ClassBuilder — fluent registration for one component type T.
    // ------------------------------------------------------------------------
    template<typename T>
    class ClassBuilder
    {
    public:
        explicit ClassBuilder(TypeDescriptor* desc) : m_Desc(desc) {}

        ClassBuilder& Category(const std::string& c) { m_Desc->Category = c; return *this; }

        template<typename M>
        ClassBuilder& Field(const std::string& name, M T::* member)
        {
            FieldDescriptor f;
            f.Name = name;
            f.Kind = DeduceKind<M>();
            f.Read = [member](const void* comp) -> FieldValue
            {
                const M& v = static_cast<const T*>(comp)->*member;
                if constexpr (std::is_enum_v<M>) return FieldValue{ static_cast<int32_t>(v) };
                else                             return FieldValue{ v };
            };
            f.Write = [member](void* comp, const FieldValue& fv)
            {
                M& dst = static_cast<T*>(comp)->*member;
                if constexpr (std::is_enum_v<M>) dst = static_cast<M>(std::get<int32_t>(fv));
                else                             dst = std::get<M>(fv);
            };
            m_Desc->Fields.push_back(std::move(f));
            m_Last = &m_Desc->Fields.back();
            return *this;
        }

        // ---- hint refinements (operate on the field most recently added) ----
        ClassBuilder& Range(float mn, float mx) { L().Hints.HasRange = true; L().Hints.Min = mn; L().Hints.Max = mx; return *this; }
        ClassBuilder& Step(float s)             { L().Hints.Step = s; return *this; }
        ClassBuilder& Tooltip(const std::string& t) { L().Hints.Tooltip = t; return *this; }
        // Doc is the reflection-metadata-v2 (T1) name for the per-field help text;
        // it shares storage with Tooltip so existing .Tooltip(...) registrations
        // and the PropertyRows consumer keep working unchanged.
        ClassBuilder& Doc(const std::string& d) { L().Hints.Tooltip = d; return *this; }
        // Physical-unit annotations (T1). One unit per field.
        ClassBuilder& Units(FieldUnits u)       { L().Hints.Units = u; return *this; }
        ClassBuilder& Degrees()                 { L().Hints.Units = FieldUnits::Degrees; return *this; }
        ClassBuilder& Meters()                  { L().Hints.Units = FieldUnits::Meters; return *this; }
        ClassBuilder& Seconds()                 { L().Hints.Units = FieldUnits::Seconds; return *this; }
        ClassBuilder& Color()                   { if (L().Kind == FieldKind::Vec4) L().Kind = FieldKind::Color; return *this; }
        ClassBuilder& AsAssetPath(const std::string& assetType) { L().Kind = FieldKind::AssetPath; L().Hints.AssetType = assetType; return *this; }
        ClassBuilder& AsEntityRef()             { L().Kind = FieldKind::EntityRef; return *this; }
        ClassBuilder& EnumValue(const std::string& n, int32_t v) { L().Hints.EnumEntries.push_back({ n, v }); return *this; }
        ClassBuilder& ReadOnly()                { L().Flags |= Field_ReadOnly; return *this; }
        ClassBuilder& HideInInspector()         { L().Flags |= Field_HideInInspector; return *this; }
        ClassBuilder& NoSerialize()             { L().Flags |= Field_NoSerialize; return *this; }
        ClassBuilder& OmitIfTrue()              { L().Flags |= Field_OmitIfTrue; return *this; }

    private:
        FieldDescriptor& L() { return *m_Last; }

        TypeDescriptor*  m_Desc = nullptr;
        FieldDescriptor* m_Last = nullptr; // reassigned after every Field(); only
                                           // touched by hints that follow it
    };

    // Begin registering component type T into a SPECIFIC registry. Used by
    // RegisterEngineTypes during the singleton's own construction, where going
    // back through GetRegistry() would re-enter an under-construction static.
    template<typename T>
    ClassBuilder<T> ClassIn(TypeRegistry& registry, const std::string& name, const std::string& category = "General")
    {
        const entt::id_type id = entt::type_hash<T>::value();
        TypeDescriptor& d = registry.GetOrCreate(id, name);
        d.Category = category;

        d.Add = [](entt::registry& r, entt::entity e) -> void*
        {
            if constexpr (std::is_empty_v<T>) { r.get_or_emplace<T>(e); return nullptr; }
            else                              { return &r.get_or_emplace<T>(e); }
        };
        d.Has = [](const entt::registry& r, entt::entity e) -> bool
        {
            return r.all_of<T>(e);
        };
        d.Remove = [](entt::registry& r, entt::entity e)
        {
            if (r.all_of<T>(e)) r.remove<T>(e);
        };
        d.Get = [](entt::registry& r, entt::entity e) -> void*
        {
            if constexpr (std::is_empty_v<T>) { return nullptr; }
            else                              { return r.all_of<T>(e) ? &r.get<T>(e) : nullptr; }
        };
        d.Copy = [](entt::registry& r, entt::entity e, const void* src)
        {
            if constexpr (std::is_empty_v<T>) { r.get_or_emplace<T>(e); }
            else { T& dst = r.get_or_emplace<T>(e); dst = *static_cast<const T*>(src); }
        };

        return ClassBuilder<T>(&d);
    }

    // Begin registering component type T in the process-wide registry (the
    // common case: engine/editor/game-module registration outside init).
    template<typename T>
    ClassBuilder<T> Class(const std::string& name, const std::string& category = "General")
    {
        return ClassIn<T>(GetRegistry(), name, category);
    }
}
