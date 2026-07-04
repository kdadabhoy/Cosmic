#pragma once

// Prefabs.h
//
// ============================================================================
// Starforge — prefab authoring helpers (E14).
// ============================================================================
//
// Thin app-side glue over the engine's SceneSerializer prefab I/O
// (SavePrefab / InstantiatePrefab). Prefabs are self-contained `.cprefab` files
// under project://prefabs/. v1 has NO per-field override tracking and NO live
// propagation to open instances — an instance is a plain copy that remembers its
// source (PrefabComponent), so Apply overwrites the asset from the instance and
// Revert re-instantiates in place. These ops mark the scene dirty but are NOT on
// the undo stack in v1 (documented, like the content browser's rename/delete).
// ============================================================================

#include "EditorContext.h"

#include <Cosmic.h>

#include <filesystem>
#include <string>

namespace Starforge::Prefabs
{
    // Sanitize a tag into a filename stem (keep it simple + predictable).
    inline std::string SafeStem(std::string s)
    {
        for (char& c : s)
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                c == '"' || c == '<' || c == '>' || c == '|')
                c = '_';
        if (s.empty()) s = "Prefab";
        return s;
    }

    // Save `root` + its subtree to project://prefabs/<Tag>.cprefab.
    inline void SaveAs(EditorContext& ctx, Cosmic::Entity root)
    {
        if (!ctx.Scene || !root) return;
        const std::string tag = root.HasComponent<Cosmic::TagComponent>()
            ? root.GetComponent<Cosmic::TagComponent>().Tag : std::string("Prefab");
        const std::string vfs = "project://prefabs/" + SafeStem(tag) + ".cprefab";

        std::error_code ec;
        std::filesystem::create_directories(Cosmic::FileSystem::Resolve("project://prefabs"), ec);
        if (Cosmic::SceneSerializer::SavePrefab(*ctx.Scene, root, Cosmic::FileSystem::Resolve(vfs)))
            ctx.Log("[Prefab] Saved '" + vfs + "'.");
        else
            ctx.Log("[Prefab] Save failed: '" + vfs + "'.", LogSeverity::Error);
    }

    // Instantiate a prefab into the scene (fresh UUIDs), select the root, dirty.
    // The stored SourcePath is kept as the VFS path (relocatable), overriding the
    // resolved disk path the engine stamps.
    inline Cosmic::Entity Instantiate(EditorContext& ctx, const std::string& vfsPath)
    {
        if (!ctx.Scene) return {};
        Cosmic::Entity root =
            Cosmic::SceneSerializer::InstantiatePrefab(*ctx.Scene, Cosmic::FileSystem::Resolve(vfsPath));
        if (root)
        {
            root.GetOrAddComponent<Cosmic::PrefabComponent>().SourcePath = vfsPath;
            ctx.SelectOnly(root);
            ctx.MarkDirty();
            ctx.Log("[Prefab] Instantiated '" + vfsPath + "'.");
        }
        else
        {
            ctx.Log("[Prefab] Instantiate failed: '" + vfsPath + "'.", LogSeverity::Error);
        }
        return root;
    }

    // Overwrite the source asset from this instance's current subtree.
    inline void Apply(EditorContext& ctx, Cosmic::Entity root)
    {
        if (!ctx.Scene || !root || !root.HasComponent<Cosmic::PrefabComponent>()) return;
        const std::string vfs = root.GetComponent<Cosmic::PrefabComponent>().SourcePath;
        if (Cosmic::SceneSerializer::SavePrefab(*ctx.Scene, root, Cosmic::FileSystem::Resolve(vfs)))
            ctx.Log("[Prefab] Applied instance to '" + vfs + "'.");
        else
            ctx.Log("[Prefab] Apply failed: '" + vfs + "'.", LogSeverity::Error);
    }

    // Re-instantiate the source in place (root transform preserved), replacing the
    // current instance subtree.
    inline void Revert(EditorContext& ctx, Cosmic::Entity root)
    {
        if (!ctx.Scene || !root || !root.HasComponent<Cosmic::PrefabComponent>()) return;
        const std::string vfs = root.GetComponent<Cosmic::PrefabComponent>().SourcePath;

        Cosmic::TransformComponent keep;
        if (root.HasComponent<Cosmic::TransformComponent>())
            keep = root.GetComponent<Cosmic::TransformComponent>();

        ctx.Scene->DestroyEntity(root, /*destroyChildren=*/true);
        Cosmic::Entity fresh = Instantiate(ctx, vfs);
        if (fresh && fresh.HasComponent<Cosmic::TransformComponent>())
            fresh.GetComponent<Cosmic::TransformComponent>() = keep;
    }
}
