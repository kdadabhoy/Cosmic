#pragma once

// Packager.h — standalone-app staging + shipping pipeline (Phase 16 / S5, S2).
//
// Turns a project's build outputs into a finished product under dist/<Project>/:
// renamed exe + engine/project DLLs + assets + boot.cfg (E19 stage), then an
// embedded icon, an optional zip, an optional Inno installer script (+ compile if
// iscc is on PATH), and a code-signing hook. Starforge self-packages through the
// SAME path (S2) — the editor is just a project named "Starforge".
//
// Stage/Finalize are synchronous file ops (fast); the slow Release build is driven
// by StarforgeApp's BuildRunner, which calls Stage+Finalize on success. No engine-
// or editor-branded assumptions cross this seam beyond the file layout Main.cpp/
// the LauncherLayer scan already expect (exe-dir DLLs + boot.cfg).

#include "EditorContext.h"

#include <string>

namespace Starforge
{
    struct PackageOptions
    {
        bool        ReleaseBuild  = true;    // build Release first (else package current config)
        bool        MakeZip       = false;
        bool        MakeInstaller = false;
        std::string SignCertPath;            // "" => signing skipped (logged)
    };

    struct PackageInputs
    {
        std::string ProjectName;        // "MyRover" (also the boot.cfg project + dll stem)
        std::string SdkDir;             // absolute SDK root (dist/ + installer/ live here)
        std::string RuntimeSourceDir;   // holds CosmicApp.exe + Cosmic.dll + engine assets/
        std::string ProjectDllPath;     // absolute path to <ProjectName>.dll ("" => none yet)
        std::string ProjectContentDir;  // absolute dir whose scenes/models/… ship as project content
        std::string OutDistDir;         // <SdkDir>/dist/<ProjectName>
        std::string ExeName;            // "<ProjectName>.exe"
        std::string IconPng;            // absolute path to the app icon PNG ("" => none)
        std::string Version = "0.9.0";
        std::string Publisher = "Kaden Dadabhoy";
    };

    class Packager
    {
    public:
        // Copy the renamed exe + DLLs + engine assets + this project's content +
        // boot.cfg into OutDistDir (fresh). Returns false + logs on failure.
        static bool Stage(EditorContext& ctx, const PackageInputs& in);

        // Post-stage: embed the icon (if any), sign (if a cert is configured), then
        // zip and/or generate+compile an installer per `opt`. Returns false on a
        // hard failure; soft steps (missing iscc) log and continue.
        static bool Finalize(EditorContext& ctx, const PackageInputs& in, const PackageOptions& opt);

    private:
        static bool WriteInstallerScript(EditorContext& ctx, const PackageInputs& in);
        static void RunInnoIfAvailable(EditorContext& ctx, const std::string& issPath);
        static void ZipDist(EditorContext& ctx, const PackageInputs& in);
        static void SignExe(EditorContext& ctx, const std::string& exePath, const std::string& cert);
    };
}
