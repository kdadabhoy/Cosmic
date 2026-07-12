#pragma once

// ContentBrowserPanel.h — project:// asset browser (E10 → T4 two-pane v2).
//
// Two-pane view of the project's asset root: a lazy-expanded folder TREE on the
// left, a tile GRID on the right (texture thumbnails + PreviewRig mesh/material
// thumbnails + type badges). Drag sources feed Inspector AssetPath slots
// (ASSET_PATH payload); double-click opens scenes / instantiates prefabs /
// previews textures; a context menu creates + deletes; a FileWatcher hot-reloads
// changed textures. T4 adds: back/forward navigation history (+ mouse-4/5), a
// recursive case-insensitive search, and a tile-size slider — tree width and
// tile size persist in EditorPrefs.

#include "EditorContext.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Starforge
{
    namespace Prefs { struct EditorSettings; }   // EditorPrefs.h (persistence)

    class ContentBrowserPanel
    {
    public:
        ~ContentBrowserPanel();
        void OnImGuiRender(EditorContext& ctx, bool* pOpen = nullptr);

        // Re-resolve project:// (call when the active project changes).
        void Reset();

        // Navigate to an asset's folder and select it (T11 — Inspector slot reveal).
        void RevealAsset(const std::string& vfs);

        // Persist / restore the panel's layout prefs (tree width, tile size,
        // preview-pane visibility).
        void LoadPrefs(const Prefs::EditorSettings& s);
        void SavePrefs(Prefs::EditorSettings& s) const;

    private:
        void        EnsureResolved();
        std::string VfsPathOf(const std::filesystem::path& p) const;

        // --- Navigation history (T4) ---
        void NavigateTo(const std::filesystem::path& dir);   // push, truncating forward
        void GoBack();
        void GoForward();
        bool CanGoBack() const    { return m_HistoryPos > 0; }
        bool CanGoForward() const { return m_HistoryPos + 1 < (int)m_History.size(); }

        // --- Sub-panels (T4) ---
        void DrawFolderTree(const std::filesystem::path& dir, EditorContext& ctx);
        void DrawGrid(EditorContext& ctx);
        void DrawEntryTile(const std::filesystem::directory_entry& entry,
                           EditorContext& ctx, float cell, bool isDir);
        bool HasSubdirectories(const std::filesystem::path& dir) const;

        // --- Rename / move + reference retarget (T6) ---
        void BeginRename(const std::filesystem::path& disk);
        // Move/rename oldDisk -> newDisk. When it references any scene/prefab/mat,
        // defer to the confirm dialog (m_Pending); otherwise relocate immediately.
        void TryRelocate(EditorContext& ctx, const std::filesystem::path& oldDisk,
                         const std::filesystem::path& newDisk);
        // Accept an ASSET_PATH tile drop onto `destDir` (move into the folder).
        void AcceptMoveDrop(EditorContext& ctx, const std::filesystem::path& destDir);

        // --- Preview + metadata pane (T7) ---
        void Select(const std::filesystem::path& disk);   // set the previewed asset
        void DrawPreviewPane(EditorContext& ctx, float height);

        // --- Import (T8) — models route to the E16 modal; other assets copy in ---
        void ImportFile(EditorContext& ctx, const std::filesystem::path& srcDisk);

        std::filesystem::path m_Root;
        std::filesystem::path m_Current;
        bool                  m_Resolved   = false;
        bool                  m_WatchOn     = false;
        Cosmic::FileWatcher   m_Watcher;

        std::string           m_Preview;        // vfs path of the texture preview popup
        std::string           m_DeleteTarget;   // disk path pending delete confirmation

        // --- T4 state ---
        std::vector<std::filesystem::path> m_History;      // navigation stack
        int                                m_HistoryPos = -1;   // cursor into m_History
        char                               m_Search[128] = { 0 };
        float                              m_TreeWidth = 220.0f; // left pane px (persisted)
        float                              m_TileSize  = 84.0f;  // grid cell px (persisted)

        // --- T6 rename/move state ---
        std::string m_RenameEditPath;              // disk path being inline-renamed ("" = none)
        char        m_RenameBuf[256] = { 0 };
        bool        m_RenameFocus = false;         // grab keyboard focus on first edit frame

        struct PendingRetarget                     // a relocation awaiting confirmation
        {
            bool        Active = false;
            std::string OldDisk, NewDisk, OldVfs, NewVfs;
            std::vector<std::string> Refs;         // files that reference OldVfs
        } m_Pending;

        // --- T7 preview pane state ---
        bool                m_ShowPreview = true;         // bottom pane visible (persisted)
        std::string         m_SelectedDisk;               // selected asset (disk path; "" = none)
        PreviewRig          m_PreviewRig;                 // interactive mesh/material turntable
        Cosmic::Ref<Cosmic::Sound> m_PreviewSound;        // decoded audio for the selected sound
        Cosmic::SoundHandle m_PreviewVoice = Cosmic::InvalidSoundHandle;
        std::string         m_WaveformFor;                // vfs the envelope was decoded for
        std::vector<float>  m_Waveform;                   // peak-decimated audio envelope
    };
}
