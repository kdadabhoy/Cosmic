#pragma once
// utils/FileWatcher.h
//
// ============================================================================
// Cosmic file watcher — directory change notifications (Phase 13 / E10).
// ============================================================================
//
// Watches a directory subtree and reports create/modify/delete/rename events so
// a tool can refresh a view or hot-reload an asset. A background thread runs the
// platform watch (ReadDirectoryChangesW on Windows) and pushes events into a
// mutex-guarded queue; the owner drains it from the main thread with Poll().
//
//     FileWatcher watcher;
//     watcher.Watch(FileSystem::Resolve("project://"));   // recursive
//     ...
//     for (const FileChange& c : watcher.Poll())          // main thread, per frame
//         RefreshOrReload(c);
//
// Engine-generic: no editor/ImGui/asset knowledge. Windows-only today; on other
// platforms Watch() is a no-op (IsWatching() == false) so callers still compile.
// The Windows API detail is hidden behind a pimpl so this header stays clean.
// ============================================================================

#include "core/Core.h"

#include <string>
#include <vector>

namespace Cosmic
{
    enum class FileChangeKind
    {
        Added,      // file/dir created
        Modified,   // contents/attributes changed
        Removed,    // file/dir deleted
        Renamed     // renamed (Path is the NEW name)
    };

    struct FileChange
    {
        FileChangeKind Kind = FileChangeKind::Modified;
        std::string    Path;      // path relative to the watched root, '/'-separated
        std::string    OldPath;   // previous name for Renamed (empty otherwise)
    };

    class COSMIC_API FileWatcher
    {
    public:
        FileWatcher();
        ~FileWatcher();

        FileWatcher(const FileWatcher&)            = delete;
        FileWatcher& operator=(const FileWatcher&) = delete;

        // Begin watching `directory` (recursively by default). Replaces any
        // existing watch. Returns false if the directory can't be opened or the
        // platform has no watcher (then IsWatching() stays false).
        bool Watch(const std::string& directory, bool recursive = true);

        // Stop the watch and join the worker thread. Safe to call repeatedly.
        void Stop();

        bool IsWatching() const;

        // Drain all pending changes (call from the main thread). Empty when idle.
        std::vector<FileChange> Poll();

    private:
        struct Impl;
        Scope<Impl> m_Impl;
    };
}
