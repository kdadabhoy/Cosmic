// SourceLocator.cpp — see SourceLocator.h (AP-03, contract §7, E08).

#include "SourceLocator.h"

#include "data/DataBus.h"
#include "scripting/AppService.h"
#include "scripting/ModuleRegistry.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace Starforge
{
    namespace
    {
        std::vector<SourceLocator::ShellRecord>& RecordList()
        {
            static std::vector<SourceLocator::ShellRecord> s_List;
            return s_List;
        }

        std::string RecordEnv()
        {
#pragma warning(push)
#pragma warning(disable: 4996)
            const char* v = std::getenv("COSMIC_AP03_RECORD_SHELL");
#pragma warning(pop)
            return (v && *v) ? std::string(v) : std::string();
        }

        std::string RecordFile()
        {
            const std::string v = RecordEnv();
            if (v == "1" || v == "true") return "ap03-shell-invocations.txt";
            return v;
        }

        bool Record(const char* kind, const SourceHit& hit)
        {
            SourceLocator::ShellRecord r;
            r.Kind = kind; r.Path = SourceLocator::Normalize(hit.Path); r.Line = hit.Line;
            RecordList().push_back(r);
            std::ofstream f(RecordFile(), std::ios::app);
            if (f) f << kind << "\t" << r.Path << "\t" << r.Line << "\n";
            return true;
        }

        std::string ReadAll(const fs::path& p)
        {
            std::ifstream in(p, std::ios::binary);
            if (!in) return {};
            std::stringstream ss; ss << in.rdbuf();
            return ss.str();
        }

        int LineOf(const std::string& text, size_t pos)
        {
            int line = 1;
            for (size_t i = 0; i < pos && i < text.size(); ++i) if (text[i] == '\n') ++line;
            return line;
        }

        bool IsIdent(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

        // "class <Name>" / "struct <Name>" followed by a non-identifier char (not a forward
        // declaration: the first match that is not immediately terminated by ';').
        size_t FindClassDecl(const std::string& text, const std::string& name)
        {
            for (const char* kw : { "class ", "struct " })
            {
                size_t pos = 0;
                const std::string needle = std::string(kw) + name;
                while ((pos = text.find(needle, pos)) != std::string::npos)
                {
                    const size_t end = pos + needle.size();
                    const bool wordEnd = end >= text.size() || !IsIdent(text[end]);
                    const bool wordStart = pos == 0 || !IsIdent(text[pos - 1]);
                    if (wordEnd && wordStart)
                    {
                        // skip "class X;" forward declarations
                        size_t k = end;
                        while (k < text.size() && (text[k] == ' ' || text[k] == '\t')) ++k;
                        if (k < text.size() && text[k] == ';') { pos = end; continue; }
                        return pos;
                    }
                    pos = end;
                }
            }
            return std::string::npos;
        }
    }

    // =========================================================================
    SourceLocator::SourceLocator(std::string projectRoot) : m_Root(std::move(projectRoot)) {}

    std::string SourceLocator::Normalize(const std::string& path)
    {
        if (path.empty()) return {};
        std::error_code ec;
        fs::path p = fs::absolute(fs::path(path), ec);
        p = p.lexically_normal();
        return p.generic_string();
    }

    std::vector<std::string> SourceLocator::SourceFiles() const
    {
        std::vector<std::string> out;
        std::error_code ec;
        const fs::path src = fs::path(m_Root) / "src";
        if (!fs::exists(src, ec)) return out;
        for (auto it = fs::recursive_directory_iterator(src, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            const std::string ext = it->path().extension().string();
            if (ext == ".h" || ext == ".cpp" || ext == ".hpp" || ext == ".inl")
                out.push_back(it->path().generic_string());
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    SourceHit SourceLocator::ForScriptClass(const std::string& className) const
    {
        SourceHit hit;
        if (className.empty()) { hit.Reason = "no class name"; return hit; }
        for (const std::string& f : SourceFiles())
        {
            const std::string text = ReadAll(f);
            const size_t pos = FindClassDecl(text, className);
            if (pos != std::string::npos)
            {
                hit.Path = Normalize(f); hit.Line = LineOf(text, pos);
                hit.Reason = "class declaration in src/";
                return hit;
            }
        }
        hit.Reason = "no 'class " + className + "' under " + (fs::path(m_Root) / "src").generic_string();
        return hit;
    }

    SourceHit SourceLocator::ForService(const std::string& serviceName) const
    {
        SourceHit hit;
        if (serviceName.empty()) { hit.Reason = "no service name"; return hit; }
        if (const Cosmic::ServiceDescriptor* d = Cosmic::ModuleRegistry::Get().FindService(serviceName))
        {
            if (!d->File.empty())
            {
                std::error_code ec;
                if (fs::exists(d->File, ec))
                {
                    hit.Path = Normalize(d->File); hit.Line = d->Line;
                    hit.Reason = "CS_SERVICE registration site";
                    return hit;
                }
            }
        }
        SourceHit scan = ForScriptClass(serviceName);
        if (scan.Resolved()) { scan.Reason = "class declaration in src/ (service not registered)"; return scan; }
        hit.Reason = "service '" + serviceName + "' is not registered (run Play once) and no class declaration was found";
        return hit;
    }

    SourceHit SourceLocator::ForPanel(const std::string& panelName, const Cosmic::PanelRegistry& panels) const
    {
        SourceHit hit;
        if (panelName.empty()) { hit.Reason = "no panel name"; return hit; }
        if (!panels.Has(panelName))
        {
            hit.Reason = "panel '" + panelName + "' is not registered — run Play once to resolve";
            return hit;
        }
        const Cosmic::PanelRegistry::Source src = panels.SourceOf(panelName);
        if (src.File.empty()) { hit.Reason = "panel '" + panelName + "' registered without a source location"; return hit; }
        hit.Path = Normalize(src.File); hit.Line = src.Line; hit.Reason = "CS_PANEL registration site";
        return hit;
    }

    SourceHit SourceLocator::ForChannel(const std::string& channel, const Cosmic::DataBus& bus) const
    {
        SourceHit hit;
        if (channel.empty()) { hit.Reason = "no channel"; return hit; }
        if (!bus.Has(channel)) { hit.Reason = "channel '" + channel + "' has no value on the bus yet"; return hit; }
        const std::string producer = bus.Producer(channel);
        if (producer.empty()) { hit.Reason = "channel '" + channel + "' has no recorded producer (written outside a service tick)"; return hit; }
        SourceHit s = ForService(producer);
        if (s.Resolved()) s.Reason = "producer '" + producer + "': " + s.Reason;
        else              s.Reason = "producer '" + producer + "': " + s.Reason;
        return s;
    }

    std::vector<SourceHit> SourceLocator::ForSignal(const std::string& signal) const
    {
        std::vector<SourceHit> out;
        if (signal.empty()) return out;
        const std::string quoted = "\"" + signal + "\"";
        for (const std::string& f : SourceFiles())
        {
            const std::string text = ReadAll(f);
            const size_t pos = text.find(quoted);
            if (pos == std::string::npos) continue;
            SourceHit h; h.Path = Normalize(f); h.Line = LineOf(text, pos); h.Reason = "contains " + quoted;
            out.push_back(h);
        }
        return out;
    }

    SourceHit SourceLocator::ForScreen(const std::string& screenName) const
    {
        SourceHit hit;
        if (screenName.empty()) { hit.Reason = "no screen name"; return hit; }
        std::error_code ec;
        const fs::path conv = fs::path(m_Root) / "src" / "screens" / (screenName + "Screen.h");
        if (fs::exists(conv, ec))
        {
            hit.Path = Normalize(conv.generic_string());
            const std::string text = ReadAll(conv);
            const size_t pos = FindClassDecl(text, screenName + "Screen");
            hit.Line = pos == std::string::npos ? 1 : LineOf(text, pos);
            hit.Reason = "src/screens/<Name>Screen.h";
            return hit;
        }
        SourceHit scan = ForScriptClass(screenName + "Screen");
        if (scan.Resolved()) return scan;
        hit.Reason = "no script for screen '" + screenName + "' (Create script writes src/screens/" + screenName + "Screen.h)";
        return hit;
    }

    // =========================================================================
    bool SourceLocator::Recording() { return !RecordEnv().empty(); }
    const std::vector<SourceLocator::ShellRecord>& SourceLocator::Recorded() { return RecordList(); }
    void SourceLocator::ClearRecorded() { RecordList().clear(); }

    bool SourceLocator::Open(const SourceHit& hit)
    {
        if (!hit.Resolved()) return false;
        if (Recording()) return Record("open", hit);
#ifdef _WIN32
        const std::wstring w = fs::path(hit.Path).wstring();
        const HINSTANCE r = ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<intptr_t>(r) > 32;
#else
        return false;
#endif
    }

    bool SourceLocator::Reveal(const SourceHit& hit)
    {
        if (!hit.Resolved()) return false;
        if (Recording()) return Record("reveal", hit);
#ifdef _WIN32
        const std::wstring arg = L"/select,\"" + fs::path(hit.Path).make_preferred().wstring() + L"\"";
        const HINSTANCE r = ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<intptr_t>(r) > 32;
#else
        return false;
#endif
    }
}
