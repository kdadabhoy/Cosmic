#pragma once

// ContentBrowserPanel.h — project:// asset browser.
//
// SKELETON: read-only folder/file listing of the project's asset root with
// breadcrumb navigation. Nothing is clickable beyond navigation.
//   TODO(E10): grid view + type icons + texture thumbnails, drag sources
//              (AssetPath payload / scene drop), create/rename/delete ops,
//              FileWatcher-driven refresh + AssetLibrary hot reload.
//   TODO(E16): OS-file drag-in triggers the assimp import pipeline.

#include "EditorContext.h"

#include <filesystem>

namespace Starforge
{
    class ContentBrowserPanel
    {
    public:
        // Draws the "Content Browser" window. Dock binding happens in StarforgeApp.
        void OnImGuiRender(EditorContext& ctx);

    private:
        std::filesystem::path m_Root;      // resolved project:// asset root
        std::filesystem::path m_Current;   // current directory (under m_Root)
        bool m_Resolved = false;
    };
}
