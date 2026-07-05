#pragma once

// BuildRunner.h
//
// ============================================================================
// Starforge — background game-module build driver (E12).
// ============================================================================
//
// Drives cmake (configure + build) for a scaffolded project's game module on a
// worker thread, capturing output for the Console panel. The build emits
// <project><hotSuffix>.dll into the SDK runtime dir so a currently-loaded module
// never blocks the new build (the hot-reload DLL-lock trick, §3.3). Not
// re-entrant — a Start while building is ignored.
//
// Environment-coupled by nature (needs a VS toolchain + the SDK source tree), so
// the end-to-end cycle is the user's on-machine acceptance step; this class owns
// the orchestration + output capture.
// ============================================================================

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Starforge
{
    struct EditorContext;

    // One labelled shell step (a cmake configure or build). RunSteps redirects each
    // step's stdout+stderr to a temp log and streams it into the Console.
    struct BuildStep
    {
        std::string Label;     // shown in the Console before the step runs
        std::string Command;   // full command WITHOUT redirection (RunSteps adds it)
    };

    class BuildRunner
    {
    public:
        enum class Status { Idle, Building, Success, Failed };

        BuildRunner() = default;
        ~BuildRunner();

        BuildRunner(const BuildRunner&)            = delete;
        BuildRunner& operator=(const BuildRunner&) = delete;

        // Kick off a background configure+build of the project at `projectDir` (the
        // folder holding CMakeLists.txt). `hotSuffix` (e.g. "_hot3") makes the output
        // DLL unique. `config` selects Debug/Release; `gameOutputDir` (S1), when set,
        // routes the module DLL to "<gameOutputDir>/<config>/" (an external project's
        // own build tree) instead of the SDK runtime dir. No-op while already building.
        void Start(const std::string& projectDir, const std::string& sdkDir,
                   const std::string& hotSuffix,
                   const std::string& config = "Debug",
                   const std::string& gameOutputDir = "");

        // Run an arbitrary sequence of build steps in the background (S5 packaging
        // pipeline: SDK Release + project Release). Stops on the first failure.
        void StartSteps(std::vector<BuildStep> steps);

        // Main-thread pump: drains captured output into the console and, once the
        // worker finishes, invokes onDone(success) exactly once and joins. Call
        // every frame.
        void Poll(EditorContext& ctx, const std::function<void(bool)>& onDone);

        Status GetStatus() const { return m_Status.load(); }
        bool   IsBuilding() const { return m_Status.load() == Status::Building; }

        // The config hot-reload builds (module DLLs load into the running editor).
        static constexpr const char* kHotConfig = "Debug";

        // Locate cmake.exe (VS-bundled, else PATH). "" only if nothing is found.
        static std::string FindCMake();

    private:
        void RunSteps(std::vector<BuildStep> steps);

        std::thread              m_Thread;
        std::atomic<Status>      m_Status{ Status::Idle };
        std::atomic<bool>        m_Finished{ false };
        std::atomic<bool>        m_Result{ false };
        std::mutex               m_LinesMx;
        std::vector<std::string> m_Lines;         // captured output, drained by Poll
        bool                     m_ResultDelivered = false;
    };
}
