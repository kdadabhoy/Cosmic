#include <Cosmic.h>
#include <windows.h>
#include <filesystem>

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

	auto app = new Cosmic::Application();
	app->Run();
	delete app;
	return 0;
}