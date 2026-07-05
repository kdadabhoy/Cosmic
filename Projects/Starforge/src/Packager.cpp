// Packager.cpp — standalone-app staging + shipping (S5/S2). See header.

#include "Packager.h"

#include <Cosmic.h>   // FileSystem, ExeResources, Log

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

namespace fs = std::filesystem;

namespace Starforge
{
    namespace
    {
        // Content sub-dirs/files we never ship: source, build trees, VCS, editor
        // scratch. Everything else under a project root is runtime content.
        bool SkipContentEntry(const std::string& name)
        {
            static const std::set<std::string> kSkip = {
                "build", ".git", ".starforge", "src", "CMakeLists.txt", ".vs", ".gitignore"
            };
            return kSkip.count(name) != 0;
        }

        void CopyProjectContent(const fs::path& srcRoot, const fs::path& dstRoot)
        {
            std::error_code ec;
            if (!fs::exists(srcRoot, ec)) return;
            fs::create_directories(dstRoot, ec);
            for (const auto& entry : fs::directory_iterator(srcRoot, ec))
            {
                if (SkipContentEntry(entry.path().filename().string()))
                    continue;
                fs::copy(entry.path(), dstRoot / entry.path().filename(),
                         fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            }
        }
    }

    bool Packager::Stage(EditorContext& ctx, const PackageInputs& in)
    {
        std::error_code ec;
        const fs::path runtime = in.RuntimeSourceDir;
        const fs::path outRoot = in.OutDistDir;

        const fs::path exeSrc    = runtime / "CosmicApp.exe";
        const fs::path engineDll = runtime / "Cosmic.dll";
        if (!fs::exists(exeSrc, ec) || !fs::exists(engineDll, ec))
        {
            ctx.Log("[Package] CosmicApp.exe / Cosmic.dll not in '" + in.RuntimeSourceDir +
                    "' — build that config first.", LogSeverity::Error);
            return false;
        }
        // The editor only ever emits hot-reload DLLs (<name>_hotN.dll); a base
        // <name>.dll comes from a non-hot (e.g. Release) build. When the base is
        // absent, fall back to the newest hot DLL in the same dir — its exports are
        // identical, so renaming it to <name>.dll produces a runnable app.
        std::string projDll = in.ProjectDllPath;
        bool hasProjectDll = !projDll.empty() && fs::exists(projDll, ec);
        if (!hasProjectDll && !in.ProjectDllPath.empty())
        {
            const fs::path dir = fs::path(in.ProjectDllPath).parent_path();
            const std::string prefix = in.ProjectName + "_hot";
            std::string newest; fs::file_time_type newestT{};
            for (const auto& e : fs::directory_iterator(dir, ec))
            {
                if (e.path().extension() != ".dll") continue;
                if (e.path().stem().string().rfind(prefix, 0) != 0) continue;
                std::error_code tec; const auto t = fs::last_write_time(e.path(), tec);
                if (newest.empty() || t > newestT) { newest = e.path().generic_string(); newestT = t; }
            }
            if (!newest.empty())
            {
                projDll = newest; hasProjectDll = true;
                ctx.Log("[Package] Using hot-built module " + fs::path(newest).filename().string() +
                        " as " + in.ProjectName + ".dll.");
            }
        }
        if (!hasProjectDll)
            ctx.Log("[Package] No " + in.ProjectName + ".dll — build the project for a runnable app.",
                    LogSeverity::Warn);

        // Fresh output dir.
        fs::remove_all(outRoot, ec);
        fs::create_directories(outRoot, ec);

        // Renamed exe + engine + project DLLs.
        fs::copy_file(exeSrc,    outRoot / in.ExeName,     fs::copy_options::overwrite_existing, ec);
        fs::copy_file(engineDll, outRoot / "Cosmic.dll",   fs::copy_options::overwrite_existing, ec);
        if (hasProjectDll)
            fs::copy_file(projDll, outRoot / (in.ProjectName + ".dll"),
                          fs::copy_options::overwrite_existing, ec);

        // Assets: engine assets (skip the whole projects/ dir) + ONLY this project's
        // content, normalised to the in-tree layout the packaged PlayerLayer expects
        // (assets/projects/<name>/, NAME-mode). External projects relocate for free.
        const fs::path assetsSrc = runtime / "assets";
        const fs::path assetsDst = outRoot / "assets";
        if (fs::exists(assetsSrc, ec))
        {
            fs::create_directories(assetsDst / "projects", ec);
            for (const auto& entry : fs::directory_iterator(assetsSrc, ec))
            {
                if (entry.path().filename() == "projects")
                    continue;   // per-project handled below
                fs::copy(entry.path(), assetsDst / entry.path().filename(),
                         fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            }
        }
        CopyProjectContent(in.ProjectContentDir, assetsDst / "projects" / in.ProjectName);

        // boot.cfg — Main.cpp launches this project (and sets the S6 app identity)
        // when run with no --project flag.
        {
            std::ofstream boot(outRoot / "boot.cfg", std::ios::trunc);
            boot << "# Cosmic packaged app — launched with no --project flag; also sets\n"
                 << "# the per-app user:// data identity (S6).\n"
                 << in.ProjectName << "\n";
        }

        ctx.Log("[Package] Staged '" + in.ProjectName + "' -> " + fs::absolute(outRoot, ec).generic_string());
        return true;
    }

    bool Packager::Finalize(EditorContext& ctx, const PackageInputs& in, const PackageOptions& opt)
    {
        std::error_code ec;
        const fs::path distExe = fs::path(in.OutDistDir) / in.ExeName;
        if (!fs::exists(distExe, ec))
        {
            ctx.Log("[Package] Staged exe missing — cannot finalize.", LogSeverity::Error);
            return false;
        }

        // 1) Icon embed (must precede signing — UpdateResource invalidates a signature).
        if (!in.IconPng.empty())
        {
            if (fs::exists(in.IconPng, ec))
            {
                if (Cosmic::ExeResources::SetIcon(distExe.generic_string(), in.IconPng))
                    ctx.Log("[Package] Embedded icon: " + in.IconPng);
                else
                    ctx.Log("[Package] Icon embed failed — see the log.", LogSeverity::Warn);
            }
            else
            {
                ctx.Log("[Package] Icon '" + in.IconPng + "' not found — shipping the engine icon.",
                        LogSeverity::Warn);
            }
        }

        // 2) Code signing (hook point — skipped with a log when no cert is configured).
        if (!opt.SignCertPath.empty())
            SignExe(ctx, distExe.generic_string(), opt.SignCertPath);
        else
            ctx.Log("[Package] Code signing skipped (no cert configured).");

        // 3) Installer script (+ compile if Inno is on PATH).
        if (opt.MakeInstaller)
        {
            if (WriteInstallerScript(ctx, in))
                RunInnoIfAvailable(ctx, (fs::path(in.SdkDir) / "dist" / (in.ProjectName + ".iss")).generic_string());
        }

        // 4) Zip (last, so it captures the embedded icon).
        if (opt.MakeZip)
            ZipDist(ctx, in);

        return true;
    }

    bool Packager::WriteInstallerScript(EditorContext& ctx, const PackageInputs& in)
    {
        std::error_code ec;
        const fs::path issPath = fs::path(in.SdkDir) / "dist" / (in.ProjectName + ".iss");
        std::ofstream f(issPath, std::ios::trunc);
        if (!f)
        {
            ctx.Log("[Package] Could not write installer script '" + issPath.generic_string() + "'.",
                    LogSeverity::Error);
            return false;
        }

        // Self-contained per-app Inno script (no /D defines needed). Boots straight
        // into the app via boot.cfg, so shortcuts carry no --project flag. Per-user
        // install => no UAC; user data lands in %LOCALAPPDATA%/<AppName> via S6.
        f << "; Generated by Starforge (S5) for " << in.ProjectName << ".\n"
          << "; Compile with:  iscc \"" << issPath.generic_string() << "\"\n\n"
          << "[Setup]\n"
          << "AppId=" << in.ProjectName << ".CosmicApp\n"
          << "AppName=" << in.ProjectName << "\n"
          << "AppVersion=" << in.Version << "\n"
          << "AppPublisher=" << in.Publisher << "\n"
          << "DefaultDirName={autopf}\\" << in.ProjectName << "\n"
          << "DefaultGroupName=" << in.ProjectName << "\n"
          << "PrivilegesRequired=lowest\n"
          << "OutputDir=.\n"
          << "OutputBaseFilename=" << in.ProjectName << "-Setup-" << in.Version << "\n"
          << "Compression=lzma2\n"
          << "SolidCompression=yes\n"
          << "DisableProgramGroupPage=yes\n"
          << "UninstallDisplayIcon={app}\\" << in.ExeName << "\n"
          << "ArchitecturesAllowed=x64compatible\n"
          << "ArchitecturesInstallIn64BitMode=x64compatible\n\n"
          << "[Tasks]\n"
          << "Name: \"desktopicon\"; Description: \"Create a &desktop shortcut\"; GroupDescription: \"Additional icons:\"\n\n"
          << "[Files]\n"
          << "Source: \"" << in.ProjectName << "\\*\"; DestDir: \"{app}\"; Flags: recursesubdirs createallsubdirs ignoreversion\n\n"
          << "[Icons]\n"
          << "Name: \"{autoprograms}\\" << in.ProjectName << "\"; Filename: \"{app}\\" << in.ExeName << "\"; WorkingDir: \"{app}\"\n"
          << "Name: \"{autodesktop}\\" << in.ProjectName << "\"; Filename: \"{app}\\" << in.ExeName << "\"; WorkingDir: \"{app}\"; Tasks: desktopicon\n\n"
          << "[Registry]\n"
          << "; --replay file association (optional): open .cham recordings with the app.\n"
          << "Root: HKCU; Subkey: \"Software\\Classes\\.cham\"; ValueType: string; ValueData: \"" << in.ProjectName << ".Replay\"; Flags: uninsdeletevalue\n"
          << "Root: HKCU; Subkey: \"Software\\Classes\\" << in.ProjectName << ".Replay\\shell\\open\\command\"; ValueType: string; ValueData: \"\"\"{app}\\" << in.ExeName << "\"\" --replay \"\"%1\"\"\"; Flags: uninsdeletekey\n\n"
          << "[Run]\n"
          << "Filename: \"{app}\\" << in.ExeName << "\"; Description: \"Launch " << in.ProjectName << "\"; Flags: nowait postinstall skipifsilent\n";

        ctx.Log("[Package] Installer script: " + issPath.generic_string());
        return true;
    }

    void Packager::RunInnoIfAvailable(EditorContext& ctx, const std::string& issPath)
    {
        // iscc on PATH? `where` exits 0 when found.
        if (std::system("where iscc >nul 2>nul") == 0)
        {
            ctx.Log("[Package] Compiling installer with Inno Setup…");
            const int rc = std::system(("iscc \"" + issPath + "\"").c_str());
            ctx.Log(rc == 0 ? "[Package] Installer built." : "[Package] iscc failed — see console.",
                    rc == 0 ? LogSeverity::Info : LogSeverity::Error);
        }
        else
        {
            ctx.Log("[Package] Inno Setup (iscc.exe) not on PATH — script written; compile it "
                    "manually. See docs/installer-guide.md.", LogSeverity::Warn);
        }
    }

    void Packager::ZipDist(EditorContext& ctx, const PackageInputs& in)
    {
        std::error_code ec;
        const fs::path zip = fs::path(in.SdkDir) / "dist" / (in.ProjectName + "-" + in.Version + ".zip");
        fs::remove(zip, ec);
        // PowerShell Compress-Archive — no new dependency.
        const std::string cmd =
            "powershell -NoProfile -Command \"Compress-Archive -Path '" +
            (fs::path(in.OutDistDir) / "*").generic_string() + "' -DestinationPath '" +
            zip.generic_string() + "' -Force\"";
        const int rc = std::system(cmd.c_str());
        if (rc == 0)
            ctx.Log("[Package] Zipped -> " + zip.generic_string());
        else
            ctx.Log("[Package] Zip failed (Compress-Archive exit " + std::to_string(rc) + ").",
                    LogSeverity::Error);
    }

    void Packager::SignExe(EditorContext& ctx, const std::string& exePath, const std::string& cert)
    {
        std::error_code ec;
        if (!fs::exists(cert, ec))
        {
            ctx.Log("[Package] Signing cert '" + cert + "' not found — skipped.", LogSeverity::Warn);
            return;
        }
        if (std::system("where signtool >nul 2>nul") != 0)
        {
            ctx.Log("[Package] signtool.exe not on PATH — cannot sign.", LogSeverity::Warn);
            return;
        }
        ctx.Log("[Package] Signing " + exePath + " …");
        const std::string cmd = "signtool sign /fd SHA256 /f \"" + cert + "\" \"" + exePath + "\"";
        const int rc = std::system(cmd.c_str());
        ctx.Log(rc == 0 ? "[Package] Signed." : "[Package] signtool failed — see console.",
                rc == 0 ? LogSeverity::Info : LogSeverity::Error);
    }
}
