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
        // so the host window is visible and requests focus on the tab. Returns the
        // (existing or new) editor, or null if `make` produced nothing.
        IAssetEditor* Open(const std::string& vfsPath, const Factory& make, bool* showFlag);

        bool   AnyOpen() const { return !m_Docs.empty(); }
        size_t Count()   const { return m_Docs.size(); }

        // Tick every open document (advance playback, pump previews).
        void OnUpdate(EditorContext& ctx, float ts);

        // Draw the "Editors" window (tab bar of documents). `open` is the View-menu
        // visibility bool — its ✕ hides the whole dock; individual tab ✕ close a
        // single document (with a save prompt when dirty).
        void OnImGuiRender(EditorContext& ctx, bool* open);

        void CloseAll() { m_Docs.clear(); m_FocusPath.clear(); m_PromptClosePath.clear(); }

    private:
        void Remove(const std::string& path);

        std::vector<std::unique_ptr<IAssetEditor>> m_Docs;
        std::string m_FocusPath;        // request SetSelected on the matching tab next render
        std::string m_PromptClosePath;  // a dirty doc awaiting the close prompt ("" = none)
    };
}
