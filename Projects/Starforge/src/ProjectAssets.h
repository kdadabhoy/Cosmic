#pragma once

// ProjectAssets.h — project-wide asset reference maintenance (T6 / gap §4.3).
//
// When an asset file is renamed or moved, the string VFS paths stored in scenes,
// prefabs, and materials still point at the OLD location. RetargetPath sweeps
// every .cscene / .cprefab / .cmat under the project root and rewrites exact
// matches of the old VFS path to the new one. It is schema-safe (parses each
// file as JSON and replaces only string VALUES equal to the old path — never a
// blind text substitution) and GL-free, so it runs entirely off the file text
// without loading a live scene.

#include <string>
#include <vector>

namespace Starforge::ProjectAssets
{
    // The project:// files that reference `oldVfs` (would be touched by a
    // retarget). No writes — used to populate the confirmation dialog.
    std::vector<std::string> FindReferences(const std::string& oldVfs);

    // Rewrite every exact string-value occurrence of `oldVfs` to `newVfs` across
    // all .cscene/.cprefab/.cmat in the project. Returns the project:// paths of
    // the files that changed. A no-op (empty result) when nothing references it.
    std::vector<std::string> RetargetPath(const std::string& oldVfs, const std::string& newVfs);
}
