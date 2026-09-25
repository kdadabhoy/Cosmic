// test_launcher_scan.cpp — UX-03 (UX & Shipping) LH01 U: the Cosmic Launcher's
// project scan never lists a test fixture (KI-77).
//
//   * LauncherLayer::ScanForProjects(dirs) over a temp dir holding a copy of a real
//     plugin (Starforge.dll) and a copy of a real fixture (AP01ServiceFixture.dll)
//     returns exactly {"Starforge"} — the fixture exports CreatePluginLayer AND the
//     CS_TEST_FIXTURE marker, and the marker wins;
//   * every *Fixture.dll the build puts beside the test exe exports CosmicTestFixture
//     (a GetProcAddress probe, the same one the scan uses), and the scan over the
//     real runtime dir lists none of them and not WO07NoExport.dll (no entry point).
//
// A missing source DLL is a FAILURE, never a skip: these cases only mean something
// against the real build outputs.

#include <doctest.h>

#include "layers/LauncherLayer.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace fs = std::filesystem;

namespace
{
    fs::path LH01ExeDir()
    {
        wchar_t exePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        return fs::path(exePath).parent_path();
    }

    // The probe the scan uses: map the image without running DllMain or resolving
    // imports, look the export up by name.
    bool ExportsSymbol(const fs::path& dll, const char* symbol)
    {
        HMODULE h = LoadLibraryExW(dll.wstring().c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
        if (!h) return false;
        const bool has = GetProcAddress(h, symbol) != nullptr;
        FreeLibrary(h);
        return has;
    }

    std::string Join(const std::vector<std::string>& v)
    {
        std::string s;
        for (const auto& x : v) s += (s.empty() ? "" : ",") + x;
        return s;
    }

    // The nine tests/*Fixture.cpp DLL targets (tests/CMakeLists.txt).
    const char* const kFixtures[] = {
        "WO05HostFixture", "WO06HostFixture", "WO07LifetimeFixture", "WO07TeardownFixture",
        "WO07ModuleFixture", "WO07PlotFixture", "WO07UiCyclesFixture", "AP01ServiceFixture",
        "WO10ClockFixture",
    };
}

TEST_SUITE("UX-03 LH01 launcher scan")
{
    TEST_CASE("UX-03 LH01 U: a dir holding a copy of Starforge.dll and of AP01ServiceFixture.dll scans to exactly {Starforge}")
    {
        const fs::path exe = LH01ExeDir();
        const fs::path plugin  = exe / "Starforge.dll";
        const fs::path fixture = exe / "AP01ServiceFixture.dll";
        REQUIRE_MESSAGE(fs::exists(plugin),  "missing source DLL: " << plugin.string());
        REQUIRE_MESSAGE(fs::exists(fixture), "missing source DLL: " << fixture.string());
        // Both are real plugins by the old rule — the fixture is only hidden by its marker.
        REQUIRE(ExportsSymbol(plugin, "CreatePluginLayer"));
        REQUIRE(ExportsSymbol(fixture, "CreatePluginLayer"));
        CHECK_FALSE(ExportsSymbol(plugin, "CosmicTestFixture"));

        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / "cosmic-ux03-lh01" /
                             std::to_string(GetCurrentProcessId());
        fs::remove_all(dir, ec);
        REQUIRE(fs::create_directories(dir, ec));
        REQUIRE(fs::copy_file(plugin, dir / "Starforge.dll", ec));
        REQUIRE(fs::copy_file(fixture, dir / "AP01ServiceFixture.dll", ec));

        const std::vector<std::string> found = Cosmic::LauncherLayer::ScanForProjects({ dir });
        INFO("scan of the temp dir: [" << Join(found) << "]");
        CHECK(found == std::vector<std::string>{ "Starforge" });

        // The member's shape: <dir>/projects first (absent here), then <dir>.
        const std::vector<std::string> shaped =
            Cosmic::LauncherLayer::ScanForProjects({ dir / "projects", dir });
        CHECK(shaped == std::vector<std::string>{ "Starforge" });

        fs::remove_all(dir, ec);
    }

    TEST_CASE("UX-03 LH01 U: every *Fixture.dll beside the exe exports CosmicTestFixture; the runtime-dir scan lists no fixture and not WO07NoExport")
    {
        const fs::path exe = LH01ExeDir();

        // The nine fixture targets are all present (a missing one is a failure).
        for (const char* name : kFixtures)
            CHECK_MESSAGE(fs::exists(exe / (std::string(name) + ".dll")), "missing fixture DLL: " << name);

        // Every *Fixture.dll actually in the dir carries the marker.
        std::error_code ec;
        int probed = 0;
        for (const auto& e : fs::directory_iterator(exe, ec))
        {
            const fs::path p = e.path();
            if (p.extension() != ".dll") continue;
            const std::string stem = p.stem().string();
            if (stem.size() < 7 || stem.compare(stem.size() - 7, 7, "Fixture") != 0) continue;
            ++probed;
            CHECK_MESSAGE(ExportsSymbol(p, "CosmicTestFixture"), stem << ".dll does not export CosmicTestFixture");
        }
        CHECK(probed >= (int)(sizeof(kFixtures) / sizeof(kFixtures[0])));

        // WO07NoExport: present, exports no entry point, stays hidden.
        const fs::path noExport = exe / "WO07NoExport.dll";
        REQUIRE_MESSAGE(fs::exists(noExport), "missing source DLL: " << noExport.string());
        CHECK_FALSE(ExportsSymbol(noExport, "CreatePluginLayer"));

        // The real runtime dir, scanned the way the launcher does.
        const std::vector<std::string> found = Cosmic::LauncherLayer::ScanForProjects({ exe / "projects", exe });
        INFO("scan of the runtime dir: [" << Join(found) << "]");
        CHECK(std::find(found.begin(), found.end(), "Starforge") != found.end());
        CHECK(std::find(found.begin(), found.end(), "WO07NoExport") == found.end());
        for (const std::string& n : found)
            CHECK_MESSAGE(!(n.size() >= 7 && n.compare(n.size() - 7, 7, "Fixture") == 0), "fixture listed: " << n);
        CHECK(std::is_sorted(found.begin(), found.end()));
    }
}
