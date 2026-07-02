#include <Cosmic.h>
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <exception>
#include <string>

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
		else
		{
			std::cerr << "CosmicApp: unrecognized argument '" << arg << "' (supported: --project <NameOrDll>)" << std::endl;
		}
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