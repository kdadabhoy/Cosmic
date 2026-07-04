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

#include <string>

namespace Cosmic
{
    class Scene;

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
    };
}
