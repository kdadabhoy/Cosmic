// AssetTypes.cpp — the asset-type registry + default-asset writers (T5).
// See AssetTypes.h.

#include "AssetTypes.h"

#include "assets/AssetLibrary.h"
#include "graphics/MaterialAsset.h"
#include "scene/Components.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/SceneSerializer.h"
#include "scene/FlowMachine.h"
#include "scene/StoryGraph.h"
#include "core/Log.h"

#include <entt/entt.hpp>

#include <unordered_map>

namespace Starforge
{
    namespace
    {
        // The registry. Built once; every entry is a stable reference-target.
        const std::unordered_map<std::string, AssetTypeInfo>& Registry()
        {
            static const std::unordered_map<std::string, AssetTypeInfo> table = []
            {
                std::unordered_map<std::string, AssetTypeInfo> t;
                t[".cscene"]   = { ICON_LC_CLAPPERBOARD, IM_COL32( 66, 133, 244, 255), "Scene",     AssetOpen::Scene };
                t[".cprefab"]  = { ICON_LC_BOXES,        IM_COL32( 38, 166, 154, 255), "Prefab",    AssetOpen::Prefab };
                t[".cmat"]     = { ICON_LC_CIRCLE,       IM_COL32(255, 138,  61, 255), "Material",  AssetOpen::Material };
                t[".cflow"]    = { ICON_LC_WORKFLOW,     IM_COL32(240, 200,  60, 255), "Flow",      AssetOpen::None, AssetOpen::FlowEditor };
                t[".cstory"]   = { ICON_LC_MESSAGES_SQUARE, IM_COL32(150, 120, 230, 255), "Story",  AssetOpen::None, AssetOpen::StoryEditor };
                t[".cmeta"]    = { ICON_LC_SETTINGS,     IM_COL32(130, 130, 135, 255), "Meta",      AssetOpen::None };

                // W7 — the 3D asset families. A row here is what makes a file
                // legible AND droppable in the Content Browser, so a 2D build
                // that cannot load these formats must not advertise them: the
                // browser falls through to its generic-file tile instead.
                // (AssetOpen keeps all its enumerators — the Model /
                // AnimationEditor values are simply never produced here.)

                // Images / textures.
                const AssetTypeInfo image = { ICON_LC_IMAGE, IM_COL32(180, 120, 230, 255), "Texture", AssetOpen::Texture };
                for (const char* e : { ".png", ".jpg", ".jpeg", ".tga", ".bmp" })
                    t[e] = image;

                // HDR environment maps (the IBL source — 3D only).

                // Audio.
                const AssetTypeInfo audio = { ICON_LC_FILE_AUDIO, IM_COL32(230, 110, 140, 255), "Audio", AssetOpen::None };
                for (const char* e : { ".wav", ".mp3", ".flac", ".ogg" })
                    t[e] = audio;

                return t;
            }();
            return table;
        }

        // Default emitter (a loadable .cemitter recipe preset). Particles are
        // excluded from the 2D build (plan decision 5), so the .cemitter row is
        // gone from the registry above and this writer goes with it.

        bool WriteDefaultPrefab(const std::string& path)
        {
            // A minimal single-entity prefab (authoring a real one is done from the
            // hierarchy; this gives the create menu a loadable starting point).
            Cosmic::Ref<Cosmic::Scene> scene = Cosmic::Scene::Create();
            Cosmic::Entity root = scene->CreateEntity("Root");
            return Cosmic::SceneSerializer::SavePrefab(*scene, root, path);
        }
    }

    const AssetTypeInfo& AssetTypeForExt(const std::string& extLower)
    {
        static const AssetTypeInfo fallback{};
        const auto& reg = Registry();
        auto it = reg.find(extLower);
        return it == reg.end() ? fallback : it->second;
    }

    const AssetTypeInfo& FolderTypeInfo()
    {
        static const AssetTypeInfo folder{ ICON_LC_FOLDER, IM_COL32(224, 190, 110, 255), "Folder", AssetOpen::None };
        return folder;
    }

    const std::vector<CreatableType>& CreatableTypes()
    {
        // W7 — the Content Browser's "Create" menu. Emitter (particles) and
        // Palette (voxel blocks) are 3D-only, so the 2D build cannot offer to
        // create a file it has no way to author or load.
        static const std::vector<CreatableType> types = {
            { "Material", ".cmat",    ICON_LC_CIRCLE },
            { "Flow",     ".cflow",   ICON_LC_WORKFLOW },
            { "Story",    ".cstory",  ICON_LC_MESSAGES_SQUARE },
            { "Scene",    ".cscene",  ICON_LC_CLAPPERBOARD },
            { "Prefab",   ".cprefab", ICON_LC_BOXES },
        };
        return types;
    }

    bool CreateDefaultAsset(const std::string& extLower, const std::string& resolvedDiskPath)
    {
        if (extLower == ".cscene")
        {
            Cosmic::Ref<Cosmic::Scene> empty = Cosmic::Scene::Create();
            return Cosmic::SceneSerializer::Save(*empty, resolvedDiskPath);
        }
        if (extLower == ".cprefab")
            return WriteDefaultPrefab(resolvedDiskPath);
        if (extLower == ".cmat")
            return Cosmic::AssetLibrary::SaveMaterialAsset(Cosmic::MaterialAsset{}, resolvedDiskPath);
        if (extLower == ".cflow")
        {
            // One "Start" state so the default flow validates + is editable.
            Cosmic::FlowAsset flow;
            flow.Start = "Start";
            Cosmic::FlowState s;
            s.Name = "Start";
            flow.States.push_back(std::move(s));
            return flow.Save(resolvedDiskPath);
        }
        if (extLower == ".cstory")
        {
            // One "Start" node with a single "@end" option so the default story
            // validates, opens in the Story Graph editor, and previews.
            Cosmic::StoryGraph story;
            story.Start = "Start";
            Cosmic::StoryNode n;
            n.Name    = "Start";
            n.Speaker = "Narrator";
            n.Text    = "Once upon a time…";
            Cosmic::StoryOption opt;
            opt.Text = "The end";
            opt.Next = "@end";
            n.Options.push_back(std::move(opt));
            story.Nodes.push_back(std::move(n));
            return story.Save(resolvedDiskPath);
        }
        return false;   // not a creatable type
    }
}
