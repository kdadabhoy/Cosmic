// X01PackageSelfTest.cpp — WO-10 (2D stability): package an EXTERNAL project
// through the REAL editor path (StarforgeApp::PackageProject -> BeginPackage ->
// BuildRunner (cmake configure + Release build of the project OUTSIDE the SDK
// tree, against COSMIC_SDK_DIR) -> Packager::Stage -> Packager::Finalize).
//
// Armed by COSMIC_X01_PACKAGE=<result.json> and COSMIC_X01_PROJECT=<absolute
// external project root> (a copy of Projects/AnalysisSample outside the SDK
// source tree). The harness opens the project (OpenProjectPath), calls the same
// PackageProject() the File > Package menu calls, waits for the build runner and
// the staging to finish, verifies the staged payload (renamed exe, Cosmic.dll,
// the project DLL, the project content under assets/projects/<name>/, boot.cfg)
// and writes a JSON result + the editor console (every cmake line) for the
// runner wrapper, which then RUNS the staged package from a different working
// directory. Nothing here packages by itself — the packager is the existing one.
#include "StarforgeApp.h"

#include <Cosmic.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace Starforge
{
    namespace
    {
        std::string Esc(const std::string& s)
        {
            std::string o; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; if (c == '\n') { o += "\\n"; continue; } o += c; } return o;
        }
    }

    struct StarforgeApp::X01PackageSelfTest
    {
        enum Phase { Boot, Open, Package, Building, Verify, Finish, Done };
        Phase phase = Boot;
        int frames = 0, phaseFrames = 0;
        std::string resultPath, projectRoot, projectName, distDir;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point buildStart;
        double buildSeconds = 0.0;
        std::vector<std::string> payload;   // staged files verified
        int failures = 0;
        std::vector<std::string> log, checks;
        void note(const char* fmt, ...)
        {
            char buf[768]; va_list ap; va_start(ap, fmt); _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap); va_end(ap);
            log.emplace_back(buf); std::printf("[X01-PKG] %s\n", buf); std::fflush(stdout);
        }
        void fail(const char* fmt, ...)
        {
            char buf[768]; va_list ap; va_start(ap, fmt); _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap); va_end(ap);
            ++failures; checks.emplace_back(buf);
            log.emplace_back(std::string("FAIL ") + buf); std::printf("[X01-PKG] FAIL %s\n", buf); std::fflush(stdout);
        }
        void go(Phase p) { phase = p; phaseFrames = 0; }
    };

    void StarforgeApp::X01SelfTestInit()
    {
        const char* rp = std::getenv("COSMIC_X01_PACKAGE");
        if (!rp || !*rp) return;
        m_X01 = new X01PackageSelfTest();
        auto& t = *m_X01;
        t.resultPath = rp;
        if (const char* root = std::getenv("COSMIC_X01_PROJECT")) t.projectRoot = root;
        SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
        m_OpenFirstRun = false;
        Cosmic::Application::Get().GetWindow().SetVSync(false);
        t.note("armed: result=%s project=%s sdk=%s", rp, t.projectRoot.c_str(), SdkDir().c_str());
    }

    void StarforgeApp::X01SelfTestShutdown() { delete m_X01; m_X01 = nullptr; }

    void StarforgeApp::X01SelfTestTick()
    {
        if (!m_X01) return;
        auto& t = *m_X01;
        using P = X01PackageSelfTest;
        ++t.frames; ++t.phaseFrames;
        const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t.start).count();
        if (t.phase != P::Done && t.phase != P::Finish && total > 900.0) { t.fail("exceeded 900 s"); t.go(P::Finish); }

        switch (t.phase)
        {
        case P::Boot:
            if (t.phaseFrames >= 3) t.go(P::Open);
            return;
        case P::Open:
        {
            std::error_code ec;
            if (t.projectRoot.empty() || !fs::exists(fs::path(t.projectRoot) / "project.cproj", ec)) { t.fail("COSMIC_X01_PROJECT does not point at a project: '%s'", t.projectRoot.c_str()); t.go(P::Finish); return; }
            if (!OpenProjectPath(t.projectRoot)) { t.fail("OpenProjectPath failed"); t.go(P::Finish); return; }
            if (!m_Ctx.ProjectOpen) { t.fail("project not open after OpenProjectPath"); t.go(P::Finish); return; }
            if (m_Ctx.ProjectPath.empty()) t.fail("project opened as in-tree, expected external");
            t.projectName = m_Ctx.ProjectName;
            t.note("opened '%s' @ %s (external=%s)", t.projectName.c_str(), m_Ctx.ProjectPath.c_str(), m_Ctx.ProjectPath.empty() ? "no" : "yes");
            t.go(P::Package);
            return;
        }
        case P::Package:
            m_PkgOpt.ReleaseBuild = true;   // the File > Package default: Release build first, then stage
            m_PkgOpt.MakeZip = false; m_PkgOpt.MakeInstaller = false;
            t.buildStart = std::chrono::steady_clock::now();
            PackageProject();
            if (!m_PkgAwaitingBuild && !m_Builder.IsBuilding()) { t.fail("PackageProject did not start the Release build"); t.go(P::Finish); return; }
            t.note("PackageProject(): building '%s' Release then staging (BuildRunner)", t.projectName.c_str());
            t.go(P::Building);
            return;
        case P::Building:
            if (m_Builder.IsBuilding() || m_PkgAwaitingBuild) return;   // the build pump (OnUpdate) drives it
            t.buildSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t.buildStart).count();
            t.note("build + stage finished in %.1f s; dist='%s'", t.buildSeconds, m_LastDistDir.c_str());
            t.go(P::Verify);
            return;
        case P::Verify:
        {
            std::error_code ec;
            t.distDir = m_LastDistDir;
            if (t.distDir.empty()) { t.fail("no dist dir recorded — the Release build or the staging failed (see the console)"); t.go(P::Finish); return; }
            const fs::path dist = t.distDir;
            auto need = [&](const fs::path& rel)
            {
                const fs::path p = dist / rel;
                if (!fs::exists(p, ec)) t.fail("staged payload missing: %s", rel.generic_string().c_str());
                else t.payload.push_back(rel.generic_string());
            };
            need(t.projectName + ".exe");
            need("Cosmic.dll");
            need(t.projectName + ".dll");
            need("boot.cfg");
            need(fs::path("assets") / "projects" / t.projectName / "project.cproj");
            // The project's own content follows it into the payload: every regular file under the
            // SOURCE project's data/ (AnalysisSample ships data/trajectory.csv; PendulumLab, or any
            // other project driven through this harness, may have none — AP-Q1 made this generic).
            if (fs::is_directory(fs::path(t.projectRoot) / "data", ec))
                for (const auto& f : fs::directory_iterator(fs::path(t.projectRoot) / "data", ec))
                    if (f.is_regular_file(ec)) need(fs::path("assets") / "projects" / t.projectName / "data" / f.path().filename());
            need(fs::path("assets") / "shaders");
            // Nothing developer-only or source in the payload.
            for (const char* bad : { "src", "build", "CMakeLists.txt", ".git" })
                if (fs::exists(dist / "assets" / "projects" / t.projectName / bad, ec)) t.fail("payload carries '%s'", bad);
            if (fs::exists(dist / "CosmicTests.exe", ec) || fs::exists(dist / "CosmicRenderTests.exe", ec)) t.fail("payload carries a test exe");
            // boot.cfg names the project.
            {
                std::ifstream b(dist / "boot.cfg"); std::string line, name;
                while (std::getline(b, line)) { const size_t a = line.find_first_not_of(" \t\r\n"); if (a == std::string::npos || line[a] == '#') continue; name = line.substr(a); break; }
                while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
                if (name != t.projectName) t.fail("boot.cfg names '%s'", name.c_str());
            }
            // The project DLL came from the EXTERNAL build tree (outside the SDK source).
            const fs::path extDll = fs::path(t.projectRoot) / "build" / "Release" / (t.projectName + ".dll");
            if (!fs::exists(extDll, ec)) t.fail("external build output missing: %s", extDll.generic_string().c_str());
            else if (fs::file_size(extDll, ec) != fs::file_size(dist / (t.projectName + ".dll"), ec)) t.fail("staged DLL size differs from the external build output");
            t.go(P::Finish);
            return;
        }
        case P::Finish:
        {
            const bool pass = t.failures == 0 && !t.distDir.empty();
#if defined(NDEBUG)
            const char* cfg = "Release";
#else
            const char* cfg = "Debug";
#endif
            {
                std::ofstream c(fs::path(t.resultPath).parent_path() / "x01-editor-console.txt", std::ios::trunc);
                for (const auto& l : m_Ctx.ConsoleLines) c << l.Text << "\n";
            }
            std::ofstream f(t.resultPath, std::ios::trunc);
            if (f)
            {
                f << "{\n  \"work_order\": \"WO-10\",\n  \"case\": \"X01 package (editor path)\",\n  \"config\": \"" << cfg << "\",\n";
                f << "  \"path\": \"OpenProjectPath -> StarforgeApp::PackageProject -> BeginPackage -> BuildRunner (cmake configure + build Release, external tree) -> Packager::Stage -> Packager::Finalize\",\n";
                f << "  \"project\": \"" << Esc(t.projectName) << "\",\n  \"project_root\": \"" << Esc(t.projectRoot) << "\",\n  \"sdk\": \"" << Esc(SdkDir()) << "\",\n";
                f << "  \"dist\": \"" << Esc(t.distDir) << "\",\n  \"exe\": \"" << Esc((fs::path(t.distDir) / (t.projectName + ".exe")).generic_string()) << "\",\n";
                f << "  \"build_seconds\": " << t.buildSeconds << ",\n  \"total_seconds\": " << std::chrono::duration<double>(std::chrono::steady_clock::now() - t.start).count() << ",\n";
                f << "  \"failed_checks\": " << t.failures << ",\n  \"verdict\": \"" << (pass ? "PASS" : "FAIL") << "\",\n  \"payload\": [\n";
                for (size_t i = 0; i < t.payload.size(); ++i) f << "    \"" << Esc(t.payload[i]) << "\"" << (i + 1 < t.payload.size() ? "," : "") << "\n";
                f << "  ],\n  \"checks_failed\": [\n";
                for (size_t i = 0; i < t.checks.size(); ++i) f << "    \"" << Esc(t.checks[i]) << "\"" << (i + 1 < t.checks.size() ? "," : "") << "\n";
                f << "  ],\n  \"log\": [\n";
                for (size_t i = 0; i < t.log.size(); ++i) f << "    \"" << Esc(t.log[i]) << "\"" << (i + 1 < t.log.size() ? "," : "") << "\n";
                f << "  ]\n}\n";
            }
            std::printf("X01_PACKAGE_RESULT=%s config=%s failedChecks=%d dist=%s\n", pass ? "PASS" : "FAIL", cfg, t.failures, t.distDir.c_str());
            std::fflush(stdout);
            t.go(P::Done);
            if (pass) Cosmic::Application::Get().Close();
            else      std::quick_exit(1);
            return;
        }
        case P::Done:
        default:
            return;
        }
    }
}
