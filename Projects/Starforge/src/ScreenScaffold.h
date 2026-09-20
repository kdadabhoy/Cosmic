#pragma once

// ScreenScaffold.h — AP-03 (App Platform, contract §5): the file-level half of
// Starforge ▸ Screens ▸ New Screen / Create script.
//
//   * WriteScreenScene   — scenes/<Name>.cscene with an entity "Canvas" (CanvasComponent
//                          + optional NativeScript{<Name>Screen}) and an entity "Camera"
//                          (orthographic CameraComponent, Primary).
//   * AddFlowState       — a FlowState { Name, "project://scenes/<Name>.cscene" } appended
//                          to a FlowAsset (the caller saves; v1 files stay byte-stable
//                          apart from the addition).
//   * CreateScript       — src/screens/<Name>Screen.h from the stub
//                          (Projects/Starforge/assets/editor/stubs/ScreenScript.h.in,
//                          tokens @SCREEN@ / @PROJECT_NAME@), then the include + the
//                          CS_SCRIPT block inserted between Module.cpp's CS_SCREENS markers.
//                          Missing markers => refused with kNoMarkersMessage and NO file
//                          is touched (the header is written only after the module text
//                          was accepted).
//   * InsertIntoModule   — the pure text edit (unit-testable): include after the last
//                          #include preceding CS_MODULE_BEGIN, CS_SCRIPT before CS_SCREENS_END.

#include <Cosmic.h>
#include "scene/FlowMachine.h"

#include <string>
#include <vector>

namespace Starforge
{
    struct ScreenScaffoldResult
    {
        bool                     Ok = false;
        std::string              Message;   // the error, or a one-line summary
        std::vector<std::string> Written;   // absolute paths created / modified
    };

    class ScreenScaffold
    {
    public:
        static constexpr const char* kNoMarkersMessage = "Module.cpp has no CS_SCREENS markers";
        static constexpr const char* kMarkerBegin      = "CS_SCREENS_BEGIN";
        static constexpr const char* kMarkerEnd        = "CS_SCREENS_END";

        // Where the editor's synced copy of the stub lives (relative to the runtime CWD).
        static std::string DefaultStubPath();

        // A screen name is an identifier: [A-Za-z_][A-Za-z0-9_]*.
        static bool ValidName(const std::string& name);

        // Pure: the stub with tokens replaced.
        static std::string RenderStub(const std::string& stubText, const std::string& screenName,
                                      const std::string& projectName);

        // Pure: edit Module.cpp text in place. false + *error when the markers are
        // missing (text untouched). Idempotent: an existing include / CS_SCRIPT is kept.
        static bool InsertIntoModule(std::string& moduleText, const std::string& screenName, std::string* error);
        static bool ModuleHasScript(const std::string& moduleText, const std::string& screenName);

        static bool WriteScreenScene(const std::string& projectRoot, const std::string& screenName,
                                     bool withScript, std::string* error);

        // Adds (or relinks) the NativeScript{<Name>Screen} on the scene's "Canvas" entity.
        static bool LinkScriptInScene(const std::string& projectRoot, const std::string& screenName, std::string* error);

        static bool AddFlowState(Cosmic::FlowAsset& asset, const std::string& screenName, std::string* error);

        static ScreenScaffoldResult CreateScript(const std::string& projectRoot, const std::string& screenName,
                                                 const std::string& projectName,
                                                 const std::string& stubPath = DefaultStubPath());

        static std::string ScenePath(const std::string& projectRoot, const std::string& screenName);   // absolute
        static std::string ScriptPath(const std::string& projectRoot, const std::string& screenName);  // absolute
        static std::string ModulePath(const std::string& projectRoot);                                 // absolute
    };
}
