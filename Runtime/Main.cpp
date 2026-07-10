#include <Cosmic.h>
#include <windows.h>
#include <shobjidl.h>   // SetCurrentProcessExplicitAppUserModelID
#include <filesystem>
#include <fstream>
#include <iostream>
#include <exception>
#include <string>
#include <cstdlib>
#include <cctype>

// Basic Bootloader / Entry Point
//
// Command line:
//   CosmicApp.exe                       -> boots into the Launcher (default)
//   CosmicApp.exe --project <NameOrDll> -> boots straight into that project,
//                                          skipping the Launcher. Accepts
//                                          "SF_Telem", "SF_Telem.dll", or an
//                                          absolute path. A missing DLL logs an
//                                          error and falls back to the Launcher.
//                                          (--project=Name is also accepted.)

int main(int argc, char** argv)
{
	// Force the working directory to the exe's own directory so all relative
	// paths (assets/, logs/, exports/) resolve correctly regardless of how
	// the exe was launched — double-click, terminal, or shortcut.
	char exePath[MAX_PATH];
	GetModuleFileNameA(nullptr, exePath, MAX_PATH);
	std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
	SetCurrentDirectoryA(exeDir.string().c_str());

	// Parse command-line flags (deliberately dependency-free).
	std::string startupProject;
	std::string replayFile;   // --replay <file> (installer file association, S5)
	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		if (arg == "--project" && i + 1 < argc)
		{
			startupProject = argv[++i];
		}
		else if (arg.rfind("--project=", 0) == 0)
		{
			startupProject = arg.substr(10);
		}
		else if (arg == "--replay" && i + 1 < argc)
		{
			// A shipped app registers this association in its installer so a
			// double-clicked recording opens the app. The path is exposed to the
			// running app via the COSMIC_REPLAY_FILE env var; apps that support
			// replay read it on boot (others ignore it — never an error).
			replayFile = argv[++i];
		}
		else
		{
			std::cerr << "CosmicApp: unrecognized argument '" << arg << "' (supported: --project <NameOrDll>, --replay <file>)" << std::endl;
		}
	}
	if (!replayFile.empty())
		_putenv_s("COSMIC_REPLAY_FILE", replayFile.c_str());

	// Packaged-dist default (E19): with no --project, a "boot.cfg" next to the exe
	// names the startup project. The Starforge packager writes one so a shipped app
	// runs straight into its scene on double-click (no Launcher). First non-empty,
	// non-'#' line is the project name; absent/empty -> the Launcher, as before.
	bool fromBootCfg = false;
	if (startupProject.empty())
	{
		std::ifstream boot(exeDir / "boot.cfg");
		std::string line;
		while (std::getline(boot, line))
		{
			const size_t a = line.find_first_not_of(" \t\r\n");
			if (a == std::string::npos) continue;
			const size_t b = line.find_last_not_of(" \t\r\n");
			const std::string trimmed = line.substr(a, b - a + 1);
			if (trimmed.empty() || trimmed[0] == '#') continue;
			startupProject = trimmed;
			fromBootCfg = true;
			break;
		}
	}

	// Dedicated single-app hosts (e.g. the dev-tree Starforge.exe target) bake a
	// default project in at compile time. Lowest priority on purpose: an explicit
	// --project and a boot.cfg next to the exe both override it, so the packaged
	// mechanics are unchanged. This exists so an app can be a first-class exe on
	// the desktop (own name/icon/taskbar identity) without the Launcher.
#ifdef COSMIC_STARTUP_PROJECT
	if (startupProject.empty())
		startupProject = COSMIC_STARTUP_PROJECT;
#endif

	// Per-app user-data isolation (S6): a packaged boot (identity from boot.cfg)
	// routes "user://" to %LOCALAPPDATA%/<AppName>/ (installed) or "<exe>/user/"
	// (portable), so two shipped apps never share prefs/logs/takes. Dev boots
	// (Launcher, --project) leave the identity empty and keep the shared root.
	// Must be set BEFORE anything resolves "user://".
	if (fromBootCfg && !startupProject.empty())
		Cosmic::FileSystem::SetAppIdentity(startupProject);

	// Windows shell identity (AppUserModelID): each Cosmic app gets its own
	// taskbar identity (grouping/pinning) instead of every boot stacking as one
	// anonymous host exe. Derived from the boot decision above — packaged apps
	// use their app name, dev boots the project name, the bare Launcher its own.
	// Must be set before any window exists. AUMID rules: < 128 chars, no spaces —
	// sanitized to ASCII [A-Za-z0-9._-], so the naive widen below is lossless.
	{
		std::string name = startupProject.empty()
			? std::string("Launcher")
			: std::filesystem::path(startupProject).stem().string();
		std::string id = "Cosmic." + name;
		for (char& c : id)
			if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-'))
				c = '-';
		if (id.size() > 127)
			id.resize(127);
		const std::wstring wid(id.begin(), id.end());
		SetCurrentProcessExplicitAppUserModelID(wid.c_str());
	}

	// The app is heap-allocated so we can guarantee graceful destruction even if
	// Run() throws. ~Application() -> Shutdown() releases GPU resources WHILE THE
	// OPENGL CONTEXT IS STILL ALIVE. Letting an exception escape main() instead would
	// std::terminate without that cleanup, leaving the static Renderer2D textures to be
	// deleted at process-exit with no current context — an access violation in
	// opengl32.dll on close. (The GL resource destructors are also context-guarded as a
	// final safety net, but graceful shutdown is the correct path.)
	Cosmic::Application* app = nullptr;
	try
	{
		// The startup project must be a constructor argument: Application's
		// constructor runs Initialize(), which decides Launcher-vs-project boot.
		app = new Cosmic::Application(startupProject);
		// Where does this app's writable data live? (H7 — "logs say where they live".)
		CS_CORE_INFO("user:// root -> {}", Cosmic::FileSystem::GetUserDataRoot());
		app->Run();
		delete app;
		return 0;
	}
	catch (const std::exception& e)
	{
		CS_CORE_CRITICAL("Fatal: unhandled exception escaped main(): {0}", e.what());
		std::cerr << "Fatal: unhandled exception escaped main(): " << e.what() << std::endl;
	}
	catch (...)
	{
		CS_CORE_CRITICAL("Fatal: unknown unhandled exception escaped main().");
		std::cerr << "Fatal: unknown unhandled exception escaped main()." << std::endl;
	}

	// Graceful shutdown after a caught exception. delete on nullptr (if construction
	// threw) is a safe no-op; otherwise this runs the full teardown with the context live.
	delete app;
	return 1;
}