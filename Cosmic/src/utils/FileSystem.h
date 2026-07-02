#pragma once

// FileSystem.h
// Last Modified 5/14/2026

/**
 * General Description:
 * 
 * The FileSystem class is a static utility service that manages path resolution
 * within the Cosmic Engine. It provides a virtual file system (VFS) abstraction
 * using protocol prefixes ("engine://" and "project://") to decouple asset
 * references from absolute disk locations.
 * 
 * This ensures that assets can be relocated or bundled without breaking
 * internal code references, and allows the engine to switch between different
 * active projects seamlessly by remapping the "project://" protocol at runtime.
 * 
 * 
 * Public Function Prototypes (Pre and Post Conditions):
 * 
 * 1. static std::string Resolve(const std::string& path)
 * Pre:  None.
 * Post: Returns a platform-specific relative path. If the input uses a protocol
 * prefix, it is expanded to the correct directory; otherwise, it returns
 * the original path.
 * 
 * 2. static void SetActiveProject(const std::string& name)
 * Pre:  'name' corresponds to a valid folder within the assets/ directory.
 * Post: Updates the internal project mapping, affecting all subsequent
 * "project://" resolution calls.
 */

#include <string>
#include <filesystem>
#include <fstream>
#include <cstdlib>

namespace Cosmic
{
	class FileSystem
	{
	public:
		////////////////////////////////
		// Path Resolution
		///////////////////////////////

		/**
		 * Resolve
		 * * THE VIRTUAL BRIDGE: Translates virtual engine paths into physical
		 * paths on the disk.
		 * * Protocols:
		 * - "engine://"  -> Maps to the base engine asset directory (read-only content).
		 * - "project://" -> Maps to a sub-folder named after the active project (read-only content).
		 * - "user://"    -> Maps to the WRITABLE user-data root (logs, recordings,
		 *                   imgui.ini, settings). See GetUserDataRoot() for the
		 *                   portable-vs-installed mapping. ALWAYS route writes here —
		 *                   an installed app's exe dir (Program Files) is read-only.
		 */
		static std::string Resolve(const std::string& path)
		{
			if (path.find("engine://") == 0)
			{
				return (std::filesystem::path("assets") / path.substr(9)).generic_string();
			}

			if (path.find("project://") == 0)
			{
				// Added "projects/" to match the CMake POST_BUILD directory structure perfectly
				return (std::filesystem::path("assets") / "projects" / s_ActiveProjectName / path.substr(10)).generic_string();
			}

			if (path.find("user://") == 0)
			{
				return (std::filesystem::path(GetUserDataRoot()) / path.substr(7)).generic_string();
			}

			return path; // Fallback for raw paths
		}

		////////////////////////////////
		// User Data Root
		///////////////////////////////

		/**
		 * GetUserDataRoot
		 * * The writable root that "user://" maps to. Decided once at first use:
		 *
		 * - PORTABLE MODE: if the working directory is writable (dev tree, unzipped
		 *   folder — Runtime/Main.cpp sets the CWD to the exe dir), user data stays
		 *   next to the app: "user://logs" resolves to "logs" exactly as before.
		 *
		 * - INSTALLED MODE: if the exe dir is NOT writable (Program Files under a
		 *   standard user), user data goes to %LOCALAPPDATA%/Cosmic/ — created on
		 *   demand. Falls back to the system temp dir if LOCALAPPDATA is unset.
		 */
		static const std::string& GetUserDataRoot()
		{
			static const std::string root = []() -> std::string
			{
				namespace fs = std::filesystem;

				// Writability probe in the working directory (== exe dir).
				const fs::path probe = fs::path(".") / ".cosmic_write_probe";
				{
					std::ofstream test(probe);
					if (test.is_open())
					{
						test.close();
						std::error_code ec;
						fs::remove(probe, ec);
						return std::string(".");
					}
				}

				// Installed / read-only location: per-user local app data.
			#pragma warning(push)
			#pragma warning(disable: 4996) // std::getenv is fine here; no CRT state is retained
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

		////////////////////////////////
		// Project Management
		///////////////////////////////

		// Not thread-safe: must only be called from the main thread before any worker calls Resolve with a project:// path.
		// If background asset loading is introduced, protect this with a shared_mutex.
		static void						SetActiveProject(const std::string& name)			{ s_ActiveProjectName = name; }

	private:
		////////////////////////////////
		// Internal State
		///////////////////////////////

		// Not thread-safe: SetActiveProject must only be called from the main thread,
		// and no worker thread may call Resolve with a project:// path concurrently.
		// If background asset loading is introduced, protect this with a shared_mutex.
		static inline std::string		s_ActiveProjectName		= "";
	};
}