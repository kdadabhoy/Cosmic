// utils/FileWatcher.cpp — see FileWatcher.h.
//
// Windows implementation: a worker thread parks on ReadDirectoryChangesW with an
// OVERLAPPED event; a second manual-reset event unblocks it for shutdown. All
// captured changes are converted to FileChange and pushed under a mutex; Poll()
// swaps the queue out on the main thread.

#include "utils/FileWatcher.h"
#include "core/Log.h"

#include <mutex>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <thread>
#include <atomic>
#include <string>

namespace Cosmic
{
    struct FileWatcher::Impl
    {
        std::thread             Worker;
        std::atomic<bool>       Running{ false };
        HANDLE                  DirHandle  = INVALID_HANDLE_VALUE;
        HANDLE                  StopEvent  = nullptr;   // manual-reset: signals shutdown
        bool                    Recursive  = true;

        std::mutex              QueueMutex;
        std::vector<FileChange> Queue;

        void Run()
        {
            // Overlapped I/O so the wait can be interrupted by StopEvent.
            OVERLAPPED overlapped = {};
            overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!overlapped.hEvent)
                return;

            // ~64 KB of FILE_NOTIFY_INFORMATION records per read.
            std::vector<BYTE> buffer(64 * 1024);

            const DWORD filter =
                FILE_NOTIFY_CHANGE_FILE_NAME  | FILE_NOTIFY_CHANGE_DIR_NAME |
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;

            while (Running.load(std::memory_order_acquire))
            {
                ResetEvent(overlapped.hEvent);
                DWORD bytes = 0;
                if (!ReadDirectoryChangesW(DirHandle, buffer.data(),
                                           static_cast<DWORD>(buffer.size()),
                                           Recursive ? TRUE : FALSE, filter,
                                           &bytes, &overlapped, nullptr))
                    break;

                HANDLE waits[2] = { overlapped.hEvent, StopEvent };
                const DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (w != WAIT_OBJECT_0)
                {
                    // StopEvent (or an error): cancel the pending read and exit.
                    CancelIo(DirHandle);
                    break;
                }

                DWORD transferred = 0;
                if (!GetOverlappedResult(DirHandle, &overlapped, &transferred, FALSE) ||
                    transferred == 0)
                    continue;   // spurious / buffer overflow — skip this batch

                DrainBuffer(buffer.data());
            }

            CloseHandle(overlapped.hEvent);
        }

        void DrainBuffer(const BYTE* base)
        {
            const BYTE* ptr = base;
            std::string pendingRenameOld;

            std::lock_guard<std::mutex> lock(QueueMutex);
            for (;;)
            {
                const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(ptr);

                const std::wstring wname(info->FileName,
                                         info->FileNameLength / sizeof(WCHAR));
                std::string name = ToUtf8(wname);
                for (char& c : name) if (c == '\\') c = '/';

                switch (info->Action)
                {
                    case FILE_ACTION_ADDED:
                        Queue.push_back({ FileChangeKind::Added, name, {} });
                        break;
                    case FILE_ACTION_REMOVED:
                        Queue.push_back({ FileChangeKind::Removed, name, {} });
                        break;
                    case FILE_ACTION_MODIFIED:
                        Queue.push_back({ FileChangeKind::Modified, name, {} });
                        break;
                    case FILE_ACTION_RENAMED_OLD_NAME:
                        pendingRenameOld = name;
                        break;
                    case FILE_ACTION_RENAMED_NEW_NAME:
                        Queue.push_back({ FileChangeKind::Renamed, name, pendingRenameOld });
                        pendingRenameOld.clear();
                        break;
                    default: break;
                }

                if (info->NextEntryOffset == 0)
                    break;
                ptr += info->NextEntryOffset;
            }
        }

        static std::string ToUtf8(const std::wstring& w)
        {
            if (w.empty())
                return {};
            const int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(),
                                                 nullptr, 0, nullptr, nullptr);
            std::string out(static_cast<size_t>(len), '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(),
                                out.data(), len, nullptr, nullptr);
            return out;
        }
    };

    FileWatcher::FileWatcher() : m_Impl(CreateScope<Impl>()) {}

    FileWatcher::~FileWatcher() { Stop(); }

    bool FileWatcher::Watch(const std::string& directory, bool recursive)
    {
        Stop();

        const int wlen = MultiByteToWideChar(CP_UTF8, 0, directory.data(),
                                             (int)directory.size(), nullptr, 0);
        std::wstring wdir(static_cast<size_t>(wlen), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, directory.data(), (int)directory.size(),
                            wdir.data(), wlen);

        HANDLE dir = CreateFileW(wdir.c_str(), FILE_LIST_DIRECTORY,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                 nullptr);
        if (dir == INVALID_HANDLE_VALUE)
        {
            CS_CORE_WARN("FileWatcher: cannot open directory '{}' (error {}).",
                         directory, GetLastError());
            return false;
        }

        m_Impl->DirHandle = dir;
        m_Impl->Recursive = recursive;
        m_Impl->StopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        m_Impl->Running.store(true, std::memory_order_release);
        m_Impl->Worker = std::thread([this] { m_Impl->Run(); });
        return true;
    }

    void FileWatcher::Stop()
    {
        if (!m_Impl->Running.exchange(false))
        {
            // Not running, but clean up any dangling handles from a failed Watch.
            if (m_Impl->StopEvent) { CloseHandle(m_Impl->StopEvent); m_Impl->StopEvent = nullptr; }
            if (m_Impl->DirHandle != INVALID_HANDLE_VALUE)
            { CloseHandle(m_Impl->DirHandle); m_Impl->DirHandle = INVALID_HANDLE_VALUE; }
            return;
        }

        if (m_Impl->StopEvent)
            SetEvent(m_Impl->StopEvent);
        if (m_Impl->Worker.joinable())
            m_Impl->Worker.join();

        if (m_Impl->StopEvent) { CloseHandle(m_Impl->StopEvent); m_Impl->StopEvent = nullptr; }
        if (m_Impl->DirHandle != INVALID_HANDLE_VALUE)
        { CloseHandle(m_Impl->DirHandle); m_Impl->DirHandle = INVALID_HANDLE_VALUE; }

        std::lock_guard<std::mutex> lock(m_Impl->QueueMutex);
        m_Impl->Queue.clear();
    }

    bool FileWatcher::IsWatching() const
    {
        return m_Impl->Running.load(std::memory_order_acquire);
    }

    std::vector<FileChange> FileWatcher::Poll()
    {
        std::vector<FileChange> out;
        std::lock_guard<std::mutex> lock(m_Impl->QueueMutex);
        out.swap(m_Impl->Queue);
        return out;
    }
}

#else  // non-Windows: no-op stub so callers still compile

namespace Cosmic
{
    struct FileWatcher::Impl {};
    FileWatcher::FileWatcher() : m_Impl(CreateScope<Impl>()) {}
    FileWatcher::~FileWatcher() {}
    bool FileWatcher::Watch(const std::string&, bool) { return false; }
    void FileWatcher::Stop() {}
    bool FileWatcher::IsWatching() const { return false; }
    std::vector<FileChange> FileWatcher::Poll() { return {}; }
}

#endif
