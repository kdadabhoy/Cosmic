#pragma once
// scene/SceneSerializer.h
//
// ============================================================================
// Cosmic scene (de)serialization — JSON via the reflection registry (E2).
// ============================================================================
//
// One generic visitor walks the E1 TypeRegistry: every registered component's
// reflected fields are written/read with ZERO per-component code. Entity
// identity is the top-level "id" (IDComponent); component blocks whose type is
// unknown to this build are preserved verbatim (OpaqueComponentsComponent) and
// re-emitted on save. The same machinery powers .cscene / .cprefab / .cmat.
//
// nlohmann/json is an implementation detail — this header exposes only strings,
// so client DLLs never include the JSON header.
// ============================================================================

#include "core/Core.h"

#include <cstdint>
#include <string>

namespace Cosmic
{
    class Scene;
    class Entity;   // E14 — prefab subtree save/instantiate

    class COSMIC_API SceneSerializer
    {
    public:
        // File I/O. Save writes atomically (temp file + rename). Load does NOT
        // clear the scene first — the editor loads into a fresh Scene. Both
        // return false on I/O or parse failure (and log).
        static bool Save(Scene& scene, const std::string& path);
        static bool Load(Scene& scene, const std::string& path);

        // In-memory variants — Play-mode snapshot/restore (E13) and tests.
        static std::string SaveToString(Scene& scene);
        static bool        LoadFromString(Scene& scene, const std::string& text);

        // Prefab subtree (E14). SavePrefab writes `root` + its descendants as a
        // self-contained `.cprefab` (schema { cosmic_prefab, root, entities }).
        // InstantiatePrefab loads one into `scene` with FRESH UUIDs (so instances
        // coexist), rebuilds the internal hierarchy, stamps the new root with a
        // PrefabComponent{sourcePath}, and returns it (an invalid Entity on
        // failure). The new root has no parent — the caller places it.
        static bool   SavePrefab(Scene& scene, Entity root, const std::string& path);
        static Entity InstantiatePrefab(Scene& scene, const std::string& path);

        // Generic reflected-struct (de)serialization (E17). Serializes ONE
        // registered type's reflected fields to/from a JSON object — the visitor
        // that powers .cscene, applied to a standalone asset like a MaterialAsset
        // (.cmat) or an emitter recipe (.cemitter, E18). `typeId` is the entt type
        // hash (entt::type_hash<T>::value()); `instance` points at a live T.
        static std::string SaveReflectedToString(uint32_t typeId, const void* instance);
        static bool        LoadReflectedFromString(uint32_t typeId, void* instance, const std::string& jsonText);
        static bool        SaveReflectedToFile(uint32_t typeId, const void* instance, const std::string& path);
        static bool        LoadReflectedFromFile(uint32_t typeId, void* instance, const std::string& path);
    };
}
