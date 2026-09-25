#pragma once

// SourceLocator.h — AP-03 (App Platform, contract §7 / D-LINKS): "open the source
// behind anything with logic".
//
// Resolves a script class, an app service, a hosted panel, a DataBus channel (via
// its producer), a signal string or a screen to a file (+ line) under the project's
// src/ tree, and opens / reveals it through the OS. The registry-backed lookups
// (ForService, ForPanel) come from the CS_SERVICE / CS_PANEL call sites AP-01
// records (__FILE__/__LINE__); the rest is a text scan of src/**/*.h,*.cpp.
//
// UX-02 (contract §2 "what a button does"): ForSignal also returns the project's startup
// flow transitions on the signal (Kind Flow, after the src/ hits), and ProjectScenes is
// the one scenes/** lister behind Screens ▸ Scenes and File ▸ Open Scene.
//
// Test seam (E08): when the env var COSMIC_AP03_RECORD_SHELL is set, Open/Reveal
// RECORD the invocation (kind + absolute path + line) to a file and to an in-memory
// list instead of calling ShellExecuteW / explorer.exe. The value "1" records to
// "ap03-shell-invocations.txt" in the CWD; any other value is the file path.

#include <Cosmic.h>

#include <string>
#include <vector>

namespace Cosmic { class PanelRegistry; class DataBus; }

namespace Starforge
{
    // UX-02 (contract §2 "what a button does"): a hit is either a source file
    // (src/**, the AP-03 kinds) or a transition of the project's startup flow.
    enum class SourceHitKind { Source, Flow };

    struct SourceHit
    {
        std::string Path;      // absolute disk path; empty => unresolved (Flow: the .cflow)
        int         Line = 0;  // 1-based when known, 0 = unknown (Flow: always 0)
        std::string Reason;    // why unresolved (tooltip), or how it was found

        // ---- Flow hits (Kind == Flow) — one per transition whose On == the signal ----
        SourceHitKind Kind = SourceHitKind::Source;
        std::string State;               // the transition's owner state
        std::string Target;              // as written: a state, "@quit", "@pop"; "push <State>" for a push
        std::string Signal;              // the transition's On
        std::string FlowVfs;             // "project://<startup_flow>" — the document to open
        int         StateIndex = -1;     // FlowAsset::States index (FlowEditor::HarnessSelect)
        int         TransitionIndex = -1;// that state's Transitions index

        bool Resolved() const { return !Path.empty(); }
        bool IsFlow() const   { return Kind == SourceHitKind::Flow; }
        // The read-only line the Inspector / viewport menu show: "Flow: <State> —<signal>→ <Target>".
        std::string FlowLine() const { return "Flow: " + State + " \xE2\x80\x94" + Signal + "\xE2\x86\x92 " + Target; }
    };

    class SourceLocator
    {
    public:
        explicit SourceLocator(std::string projectRoot);

        const std::string& Root() const { return m_Root; }

        SourceHit ForScriptClass(const std::string& className) const;   // scan src/**/*.h,*.cpp for "class <Name>" (first match)
        SourceHit ForService(const std::string& serviceName) const;     // ModuleRegistry::FindService()->File/Line, else scan like a class
        SourceHit ForPanel(const std::string& panelName, const Cosmic::PanelRegistry& panels) const;   // PanelRegistry::SourceOf
        SourceHit ForChannel(const std::string& channel, const Cosmic::DataBus& bus) const;            // Producer(channel) -> ForService
        // Every src/** file containing the quoted signal string, THEN (UX-02) one Flow hit per
        // transition of the startup flow (project.cproj startup_flow) whose On equals it.
        std::vector<SourceHit> ForSignal(const std::string& signal) const;
        std::vector<SourceHit> FlowHitsForSignal(const std::string& signal) const;   // the Flow half alone (cheap: two small reads)
        std::string            StartupFlowRel() const;   // project.cproj startup_flow ("" = none), "project://" stripped

        // UX-02 (contract §2 "Scenes list") — every scenes/**/*.cscene under `projectRoot`
        // as "project://scenes/<rel>" (recursive; *.bak and other extensions never listed),
        // sorted case-insensitively by path. The ONE lister behind Screens ▸ Scenes and
        // File ▸ Open Scene.
        static std::vector<std::string> ProjectScenes(const std::string& projectRoot);
        SourceHit ForScreen(const std::string& screenName) const;            // src/screens/<Name>Screen.h if it exists, else ForScriptClass

        static bool Open(const SourceHit& hit);      // ShellExecuteW "open" on the file (the OS default editor); false when unresolved
        static bool Reveal(const SourceHit& hit);    // explorer.exe /select,"<path>"

        // ---- the E08 recording seam ----
        struct ShellRecord { std::string Kind; std::string Path; int Line = 0; };   // Kind = "open" | "reveal"
        static bool Recording();                                    // COSMIC_AP03_RECORD_SHELL set
        static const std::vector<ShellRecord>& Recorded();          // this process, in order
        static void ClearRecorded();

        // A file:line, made absolute + generic (helper shared by the panels).
        static std::string Normalize(const std::string& path);

    private:
        std::vector<std::string> SourceFiles() const;   // src/**/*.h,*.cpp (sorted)
        std::string m_Root;
    };
}
