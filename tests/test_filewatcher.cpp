// test_filewatcher.cpp — directory change notifications (Phase 13 / E10).
// Uses a real temp directory (no GL). The detection assertions are Windows-only
// (the watcher is a no-op stub elsewhere); lifecycle checks run everywhere.

#include <doctest.h>

#include "utils/FileWatcher.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using namespace Cosmic;

namespace
{
    // Poll up to ~2s for the queue to produce any change (ReadDirectoryChangesW
    // usually fires within tens of ms; the generous cap only guards a slow box).
    std::vector<FileChange> WaitForChanges(FileWatcher& w)
    {
        for (int i = 0; i < 200; ++i)
        {
            auto changes = w.Poll();
            if (!changes.empty())
                return changes;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return {};
    }

    bool MentionsFile(const std::vector<FileChange>& changes, const std::string& name)
    {
        for (const auto& c : changes)
            if (c.Path.find(name) != std::string::npos ||
                c.OldPath.find(name) != std::string::npos)
                return true;
        return false;
    }
}

TEST_CASE("E10: watching a missing directory fails cleanly")
{
    FileWatcher w;
    CHECK_FALSE(w.Watch("this/path/does/not/exist/hopefully"));
    CHECK_FALSE(w.IsWatching());
    CHECK(w.Poll().empty());
}

TEST_CASE("E10: Stop is idempotent and safe on an unstarted watcher")
{
    FileWatcher w;
    w.Stop();
    w.Stop();
    CHECK_FALSE(w.IsWatching());
}

#ifdef _WIN32
TEST_CASE("E10: a new file in the watched tree is reported")
{
    const fs::path dir = fs::temp_directory_path() / "cosmic_fw_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    REQUIRE(fs::exists(dir));

    FileWatcher w;
    REQUIRE(w.Watch(dir.generic_string()));
    CHECK(w.IsWatching());

    // Give the worker a moment to arm the first ReadDirectoryChangesW call.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const std::string name = "hello.txt";
    {
        std::ofstream f(dir / name);
        f << "content";
    }

    const auto changes = WaitForChanges(w);
    CHECK_FALSE(changes.empty());
    CHECK(MentionsFile(changes, name));

    w.Stop();
    CHECK_FALSE(w.IsWatching());
    fs::remove_all(dir, ec);
}
#endif
