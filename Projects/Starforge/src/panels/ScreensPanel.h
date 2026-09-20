#pragma once

// panels/ScreensPanel.h — AP-03 (App Platform, contract §5): the Screens panel.
//
// Lists the manifest startup flow's states (one row per screen: name, scene,
// script status, start marker) or, when the project has no flow, "no flow" with a
// Create flow button. Actions on the selected screen: Set as start, Open scene
// (double-click too), Open script / Reveal (SourceLocator), Create / relink script
// (ScreenScaffold), Rename. New Screen: a name + "Create script" -> the scene
// (canvas + ortho camera), the flow state, and optionally the script.
//
// The panel owns no editor state; the shell (StarforgeApp) hands it the project
// facts and three callbacks through `Host` each frame. Every flow edit round-trips
// the .cflow through FlowAsset::Load/Save (byte-stable apart from the edited keys).

#include "../EditorContext.h"
#include "../ScreenScaffold.h"
#include "../SourceLocator.h"

#include <functional>
#include <string>
#include <vector>

namespace Starforge
{
    class ScreensPanel
    {
    public:
        struct Host
        {
            std::string ProjectRoot;     // absolute
            std::string ProjectName;
            std::string ManifestFlow;    // relative, e.g. "flows/Main.cflow" ("" = none)
            std::function<void(const std::string& vfsScene)>  OpenScene;         // "project://scenes/X.cscene"
            std::function<void(const std::string& relFlow)>   SetManifestFlow;   // write project.cproj + adopt
            std::function<void(const std::string& vfsFlow)>   OpenFlowDocument;  // optional: the flow editor
        };

        void OnImGuiRender(EditorContext& ctx, bool* pOpen, const Host& host);

        // ---- the operations (public so the AP-03 self-test drives the real ones) ----
        struct OpResult { bool Ok = false; std::string Message; };
        OpResult CreateFlow(EditorContext& ctx, const Host& host);                                        // flows/Main.cflow + manifest key
        OpResult NewScreen(EditorContext& ctx, const Host& host, const std::string& name, bool createScript);
        OpResult SetAsStart(EditorContext& ctx, const Host& host, const std::string& name);
        OpResult Rename(EditorContext& ctx, const Host& host, const std::string& oldName, const std::string& newName);
        OpResult CreateOrRelinkScript(EditorContext& ctx, const Host& host, const std::string& name);
        bool     OpenScript(const Host& host, const std::string& name);    // SourceLocator::Open(ForScreen)
        bool     RevealScript(const Host& host, const std::string& name);  // SourceLocator::Reveal(ForScreen)

        // The states of the manifest flow as last loaded (empty when no flow).
        const std::vector<Cosmic::FlowState>& States() const { return m_Asset.States; }
        const std::string& StartState() const { return m_Asset.Start; }
        bool HasFlow() const { return m_HasFlow; }
        void Invalidate() { m_Loaded = false; }   // re-read the .cflow next frame

        const std::string& Selected() const { return m_Selected; }
        void Select(const std::string& name) { m_Selected = name; }

    private:
        bool Reload(const Host& host);                 // Load the manifest flow into m_Asset
        bool SaveFlow(const Host& host, std::string* error);
        std::string FlowDiskPath(const Host& host) const;

        Cosmic::FlowAsset m_Asset;
        bool        m_HasFlow  = false;
        bool        m_Loaded   = false;
        std::string m_LoadedFor;                        // root + flow the asset was loaded from
        std::string m_Selected;
        std::string m_Status;                           // last op message (drawn at the bottom)
        bool        m_StatusError = false;
        char        m_NewName[64]   = "Screen";
        bool        m_NewWithScript = true;
        char        m_RenameBuf[64] = "";
    };
}
