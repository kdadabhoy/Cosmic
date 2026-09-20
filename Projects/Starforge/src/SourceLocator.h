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
    struct SourceHit
    {
        std::string Path;      // absolute disk path; empty => unresolved
        int         Line = 0;  // 1-based when known, 0 = unknown
        std::string Reason;    // why unresolved (tooltip), or how it was found

        bool Resolved() const { return !Path.empty(); }
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
        std::vector<SourceHit> ForSignal(const std::string& signal) const;   // every src/** file containing the quoted signal string
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
