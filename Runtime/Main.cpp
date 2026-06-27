#include <Cosmic.h>
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <exception>

// Basic Bootloader / Entry Point

int main()
{
	// Force the working directory to the exe's own directory so all relative
	// paths (assets/, logs/, exports/) resolve correctly regardless of how
	// the exe was launched — double-click, terminal, or shortcut.
	char exePath[MAX_PATH];
	GetModuleFileNameA(nullptr, exePath, MAX_PATH);
	std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
	SetCurrentDirectoryA(exeDir.string().c_str());

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
		app = new Cosmic::Application();
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