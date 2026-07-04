#pragma once

// ContentBrowserPanel.h — project:// asset browser (E10).
//
// Grid + breadcrumb view of the project's asset root with texture thumbnails and
// type badges, drag sources that feed Inspector AssetPath slots (ASSET_PATH
// payload), double-click actions (scene→open, texture→preview), a context menu
// (new folder/scene, show-in-explorer, recycle-bin delete), and a FileWatcher
// that hot-reloads changed textures through AssetLibrary::Reload so the viewport
// updates within a poll.

#include "EditorContext.h"

#include <filesystem>
#include <string>

namespace Starforge
{
    class ContentBrowserPanel
    {
    public:
        ~ContentBrowserPanel();
        void OnImGuiRender(EditorContext& ctx);

        // Re-resolve project:// (call when the active project changes).
        void Reset();

    private:
        void EnsureResolved();
        std::string VfsPathOf(const std::filesystem::path& p) const;

        std::filesystem::path m_Root;
        std::filesystem::path m_Current;
        bool                  m_Resolved   = false;
        bool                  m_WatchOn     = false;
        Cosmic::FileWatcher   m_Watcher;

        std::string           m_Preview;        // vfs path of the texture preview popup
        std::string           m_DeleteTarget;   // disk path pending delete confirmation
    };
}
