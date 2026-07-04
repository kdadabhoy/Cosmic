// BuildRunner.cpp — background cmake driver (E12). See BuildRunner.h.

#include "BuildRunner.h"
#include "EditorContext.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace Starforge
{
    BuildRunner::~BuildRunner()
    {
        if (m_Thread.joinable())
            m_Thread.join();
    }

    std::string BuildRunner::FindCMake()
    {
        // 1) VS-bundled cmake (matches the roadmap's non-interactive recipe).
        const char* bases[] = {
            "C:/Program Files/Microsoft Visual Studio",
            "C:/Program Files (x86)/Microsoft Visual Studio",
        };
        std::error_code ec;
        for (const char* base : bases)
        {
            if (!fs::exists(base, ec)) continue;
            for (const auto& ed : fs::directory_iterator(base, ec))   // 18, 2022, ...
            {
                if (!ed.is_directory(ec)) continue;
                for (const auto& sku : fs::directory_iterator(ed.path(), ec))  // Community, ...
                {
                    fs::path p = sku.path() /
                        "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe";
                    if (fs::exists(p, ec))
                        return p.string();
                }
            }
        }
        // 2) Rely on PATH.
        return "cmake";
    }

    void BuildRunner::Start(const std::string& projectDir, const std::string& sdkDir,
                            const std::string& hotSuffix)
    {
        if (IsBuilding())
            return;
        if (m_Thread.joinable())
            m_Thread.join();   // reap a finished-but-unjoined worker

        {
            std::lock_guard<std::mutex> lk(m_LinesMx);
            m_Lines.clear();
        }
        m_Finished.store(false);
        m_Result.store(false);
        m_ResultDelivered = false;
        m_Status.store(Status::Building);

        m_Thread = std::thread(&BuildRunner::Run, this, projectDir, sdkDir, hotSuffix);
    }

    void BuildRunner::Run(std::string projectDir, std::string sdkDir, std::string hotSuffix)
    {
        const std::string cmake    = FindCMake();
        const std::string buildDir = projectDir + "/build";
        const std::string logFile  = (fs::temp_directory_path() / "starforge_build.log").string();

        auto push = [&](const std::string& s)
        {
            std::lock_guard<std::mutex> lk(m_LinesMx);
            m_Lines.push_back(s);
        };
        auto drainLog = [&]()
        {
            std::ifstream in(logFile);
            std::string line;
            std::lock_guard<std::mutex> lk(m_LinesMx);
            while (std::getline(in, line))
                m_Lines.push_back(line);
        };
        // std::system runs `cmd /c <str>`; wrap the whole thing in an extra pair of
        // quotes so cmd doesn't strip the quotes around the (spaced) cmake path.
        auto runCmd = [&](const std::string& cmd) -> int
        {
            return std::system(("\"" + cmd + "\"").c_str());
        };

        push("[build] configuring " + projectDir);
        const std::string cfg =
            "\"" + cmake + "\" -S \"" + projectDir + "\" -B \"" + buildDir +
            "\" -A x64 -DCOSMIC_SDK_DIR=\"" + sdkDir + "\" -DGAME_HOT_SUFFIX=" + hotSuffix +
            " > \"" + logFile + "\" 2>&1";
        int rc = runCmd(cfg);
        drainLog();
        if (rc != 0)
        {
            push("[build] configure FAILED (exit " + std::to_string(rc) + ")");
            m_Result.store(false);
            m_Finished.store(true);
            return;
        }

        push("[build] compiling (Debug)");
        const std::string bld =
            "\"" + cmake + "\" --build \"" + buildDir + "\" --config Debug --parallel"
            " > \"" + logFile + "\" 2>&1";
        rc = runCmd(bld);
        drainLog();

        m_Result.store(rc == 0);
        push(rc == 0 ? "[build] SUCCESS" : "[build] FAILED (exit " + std::to_string(rc) + ")");
        m_Finished.store(true);
    }

    void BuildRunner::Poll(EditorContext& ctx, const std::function<void(bool)>& onDone)
    {
        // Drain captured output into the console.
        {
            std::lock_guard<std::mutex> lk(m_LinesMx);
            for (auto& l : m_Lines)
                ctx.Log(l, l.find("FAILED") != std::string::npos ? LogSeverity::Error
                                                                  : LogSeverity::Info);
            m_Lines.clear();
        }

        if (m_Finished.load() && !m_ResultDelivered)
        {
            const bool ok = m_Result.load();
            m_Status.store(ok ? Status::Success : Status::Failed);
            m_ResultDelivered = true;
            if (m_Thread.joinable())
                m_Thread.join();
            onDone(ok);
        }
    }
}
