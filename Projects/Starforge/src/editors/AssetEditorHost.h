#pragma once

// editors/AssetEditorHost.h
//
// ============================================================================
// Starforge — tabbed asset-editor document host (Phase 24 / M1, gap §8.1).
// ============================================================================
//
// Owns the open IAssetEditor documents and draws them as a tab bar inside the
// dockable "Editors" window. Responsibilities:
//   * multi-document tab bar with per-tab dirty dot;
//   * one editor instance per asset path (Open re-focuses an already-open doc);
//   * close-with-save prompt (a dirty tab's ✕ raises Save / Discard / Cancel);
//   * per-frame OnUpdate for every open doc (playback keeps running unfocused).
//
// The host never knows a document's concrete type — callers pass a factory, so
// the shell wires the Content Browser's "Open in Animation Editor" request to
// an AnimationEditor factory without this file depending on it.
// ============================================================================

#include "IAssetEditor.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Starforge
{
    struct EditorContext;

    class AssetEditorHost
    {
    public:
        using Factory = std::function<std::unique_ptr<IAssetEditor>()>;

        // Open the asset at `vfsPath` — re-focusing the tab if one is already open
        // for that path, else building a new document via `make`. Raises *showFlag
        // so the host window is visible and requests focus on the window + the tab
        // for the next frame. Returns the (existing or new) editor, or null if `make`
        // produced nothing.
        IAssetEditor* Open(const std::string& vfsPath, const Factory& make, bool* showFlag);

        bool   AnyOpen() const { return !m_Docs.empty(); }
        size_t Count()   const { return m_Docs.size(); }

        // The open document for `vfsPath` (null when none). No focus / visibility effect.
        IAssetEditor* Find(const std::string& vfsPath) const;

        // Close the document for `vfsPath` without a prompt (what a clean tab's ✕ does).
        // Returns false when no such document is open.
        bool Close(const std::string& vfsPath);

        // The document's stable tab id (0 = not open). Keys the tab label and PushID.
        uint32_t TabId(const std::string& vfsPath) const;

        // Tick every open document (advance playback, pump previews).
        void OnUpdate(EditorContext& ctx, float ts);

        // Draw the "Editors" window (tab bar of documents). `open` is the View-menu
        // visibility bool — its ✕ hides the whole dock; individual tab ✕ close a
        // single document (with a save prompt when dirty).
        void OnImGuiRender(EditorContext& ctx, bool* open);

        void CloseAll() { m_Docs.clear(); m_FocusPath.clear(); m_PromptClosePath.clear(); m_WantFocus = false; }

    private:
        struct Doc
        {
            std::unique_ptr<IAssetEditor> Editor;
            uint32_t                      Id = 0;   // stable tab id (per-host counter)
        };

        void Remove(const std::string& path);

        std::vector<Doc> m_Docs;
        uint32_t    m_NextId = 1;
        bool        m_WantFocus = false;  // SetNextWindowFocus on the next render (Open added / re-focused)
        std::string m_FocusPath;        // request SetSelected on the matching tab next render
        std::string m_PromptClosePath;  // a dirty doc awaiting the close prompt ("" = none)
    };
}
