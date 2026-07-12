#pragma once

// AssetTypes.h — the single registry mapping an asset file EXTENSION to its
// editor identity (T5 / gap §4.2): a Lucide glyph, an accent color, a display
// name, a double-click open action, and (for creatable types) a default-asset
// writer. The Content Browser tiles + create menu + open routing, and the
// Hierarchy row icons (T14), all read from this ONE table so a new asset kind is
// added in exactly one place. Later phases append rows (.cstory, .cnav, skeleton
// /clips) — keep this the single source of truth.

#include "ui/IconsLucide.h"

#include <imgui.h>

#include <string>
#include <vector>

namespace Starforge
{
    // What a double-click on the tile does. The panel maps these onto its
    // existing cross-panel requests (open routing stays unchanged for the three
    // that already had behavior: Scene, Prefab, Texture).
    enum class AssetOpen { None, Scene, Prefab, Texture, Model, Material };

    struct AssetTypeInfo
    {
        const char* Glyph = ICON_LC_FILE;
        ImU32       Color = IM_COL32(150, 150, 155, 255);
        const char* Name  = "File";
        AssetOpen   Open  = AssetOpen::None;
    };

    // Identity for a lower-case extension WITH the dot (e.g. ".cmat"). Unknown
    // extensions get a neutral fallback (never null).
    const AssetTypeInfo& AssetTypeForExt(const std::string& extLower);

    // The folder pseudo-type (tiles + tree + hierarchy children counts).
    const AssetTypeInfo& FolderTypeInfo();

    // The "New…" menu entries (label + extension + glyph). Folder is handled by
    // the panel itself (a directory, not a file write).
    struct CreatableType
    {
        const char* Label;
        const char* Ext;    // ".cmat", ".cscene", …
        const char* Glyph;
    };
    const std::vector<CreatableType>& CreatableTypes();

    // Write a default, loadable asset of `extLower` to the absolute disk path
    // `resolvedDiskPath`. Returns false if the type is not creatable or the write
    // fails. (Heavy engine dependencies live in AssetTypes.cpp so this header
    // stays light for the many panels that only need the glyph/color.)
    bool CreateDefaultAsset(const std::string& extLower, const std::string& resolvedDiskPath);
}
