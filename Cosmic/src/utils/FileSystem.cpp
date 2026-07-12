// utils/FileSystem.cpp — the VFS mount state + resolution. See FileSystem.h.
//
// The state lives HERE, in the engine DLL, so every module in the process
// shares one active project (the A1 fix — the header-only `static inline`
// members used to give each DLL its own copy; see the header note).

#include "utils/FileSystem.h"

namespace Cosmic
{
	namespace
	{
		// Not thread-safe: the setters must only be called from the main thread,
		// and no worker thread may call Resolve with a project:// path concurrently.
		// If background asset loading is introduced, protect this with a shared_mutex.
		std::string s_ActiveProjectName;
		std::string s_ActiveProjectPath;          // "" => NAME mode
		bool        s_ProjectHasAssetsSubdir = false;
		std::string s_AppIdentity;                // "" => dev/shared user root
	}

	std::string FileSystem::Resolve(const std::string& path)
	{
		if (path.find("engine://") == 0)
		{
			return (std::filesystem::path("assets") / path.substr(9)).generic_string();
		}

		if (path.find("project://") == 0)
		{
			const std::string rel = path.substr(10);

			// PATH mode (S1): a self-contained project folder anywhere on disk.
			if (!s_ActiveProjectPath.empty())
			{
				std::filesystem::path base = s_ActiveProjectPath;
				if (s_ProjectHasAssetsSubdir)
					base /= "assets";
				return (base / rel).generic_string();
			}

			// NAME mode (legacy): assets/projects/<name>/ — matches the CMake
			// POST_BUILD directory structure for in-tree plugin projects.
			return (std::filesystem::path("assets") / "projects" / s_ActiveProjectName / rel).generic_string();
		}

		if (path.find("user://") == 0)
		{
			return (std::filesystem::path(GetUserDataRoot()) / path.substr(7)).generic_string();
		}

		return path; // Fallback for raw paths
	}

	void FileSystem::SetAppIdentity(const std::string& name) { s_AppIdentity = name; }
	const std::string& FileSystem::AppIdentity()             { return s_AppIdentity; }

	const std::string& FileSystem::GetUserDataRoot()
	{
		static const std::string root = []() -> std::string
		{
			namespace fs = std::filesystem;

			// Writability probe in the working directory (== exe dir).
			const bool exeDirWritable = []() -> bool
			{
				const fs::path probe = fs::path(".") / ".cosmic_write_probe";
				std::ofstream test(probe);
				if (!test.is_open())
					return false;
				test.close();
				std::error_code ec;
				fs::remove(probe, ec);
				return true;
			}();

			// Packaged app (identity set): isolate per-app.
			if (!s_AppIdentity.empty())
			{
				std::error_code ec;
				const bool portableFlag = fs::exists(fs::path(".") / "portable.txt", ec);
				if (portableFlag || exeDirWritable)
				{
					fs::path portableRoot = fs::path(".") / "user";
					fs::create_directories(portableRoot, ec);
					return portableRoot.generic_string();
				}

			#pragma warning(push)
			#pragma warning(disable: 4996) // std::getenv is fine here; no CRT state is retained
				const char* localAppData = std::getenv("LOCALAPPDATA");
			#pragma warning(pop)

				fs::path dataRoot = localAppData ? (fs::path(localAppData) / s_AppIdentity)
				                                 : (fs::temp_directory_path() / s_AppIdentity);
				fs::create_directories(dataRoot, ec);
				return dataRoot.generic_string();
			}

			// Dev / shared root (historical behavior, unchanged).
			if (exeDirWritable)
				return std::string(".");

		#pragma warning(push)
		#pragma warning(disable: 4996)
			const char* localAppData = std::getenv("LOCALAPPDATA");
		#pragma warning(pop)

			fs::path dataRoot = localAppData ? (fs::path(localAppData) / "Cosmic")
			                                 : (fs::temp_directory_path() / "Cosmic");
			std::error_code ec;
			fs::create_directories(dataRoot, ec);
			return dataRoot.generic_string();
		}();
		return root;
	}

	void FileSystem::SetActiveProject(const std::string& name)
	{
		s_ActiveProjectName      = name;
		s_ActiveProjectPath.clear();          // drop any absolute PATH mount
		s_ProjectHasAssetsSubdir = false;
	}

	void FileSystem::SetActiveProjectPath(const std::string& absoluteRoot)
	{
		namespace fs = std::filesystem;
		std::error_code ec;
		s_ActiveProjectPath      = fs::path(absoluteRoot).generic_string();
		s_ProjectHasAssetsSubdir = fs::exists(fs::path(absoluteRoot) / "assets", ec);
	}

	const std::string& FileSystem::ActiveProjectPath() { return s_ActiveProjectPath; }
}
