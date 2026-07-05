#pragma once

// FileSystem.h
// Last Modified 7/5/2026

/**
 * General Description:
 *
 * The FileSystem class is a static utility service that manages path resolution
 * within the Cosmic Engine. It provides a virtual file system (VFS) abstraction
 * using protocol prefixes ("engine://", "project://" and "user://") to decouple
 * asset references from absolute disk locations.
 *
 * This ensures that assets can be relocated or bundled without breaking
 * internal code references, and allows the engine to switch between different
 * active projects seamlessly by remapping the "project://" protocol at runtime.
 *
 * project:// has TWO mount modes (Phase 16 / S1):
 *   - NAME mode   (SetActiveProject):     assets/projects/<name>/…  — the legacy
 *                                         in-tree layout shipped plugin apps use.
 *   - PATH mode   (SetActiveProjectPath): <absoluteRoot>/[assets/]…  — a
 *                                         self-contained project folder anywhere
 *                                         on disk (Starforge external projects).
 * The last setter wins; SetActiveProject clears any absolute mount so a legacy
 * project always resolves the legacy way.
 *
 *
 * Public Function Prototypes (Pre and Post Conditions):
 *
 * 1. static std::string Resolve(const std::string& path)
 * Pre:  None.
 * Post: Returns a platform-specific relative/absolute path. If the input uses a
 * protocol prefix, it is expanded to the correct directory; otherwise, it returns
 * the original path.
 *
 * 2. static void SetActiveProject(const std::string& name)
 * Pre:  'name' corresponds to a valid folder within the assets/projects/ directory.
 * Post: Updates the internal project mapping to NAME mode, affecting all
 * subsequent "project://" resolution calls; clears any absolute PATH mount.
 *
 * 3. static void SetActiveProjectPath(const std::string& absoluteRoot)
 * Pre:  'absoluteRoot' is an absolute path to a self-contained project folder.
 * Post: Switches project:// resolution to PATH mode rooted at absoluteRoot,
 * appending an "assets/" subdir only when one exists (probed once here).
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
		 * - "project://" -> Maps to the active project (NAME or PATH mode; read-only content).
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

		////////////////////////////////
		// User Data Root
		///////////////////////////////

		/**
		 * SetAppIdentity — names the running app so per-app user data is isolated
		 * (Phase 16 / S6). Packaged boots (a boot.cfg next to the exe named the
		 * startup project) call this from Runtime/Main.cpp BEFORE anything resolves
		 * "user://", so two different shipped apps never share prefs/logs/takes.
		 * Dev boots (the Launcher, `--project`) leave the identity empty and keep
		 * the current shared root. Must be set before the first GetUserDataRoot().
		 */
		static void SetAppIdentity(const std::string& name) { s_AppIdentity = name; }
		static const std::string& AppIdentity() { return s_AppIdentity; }

		/**
		 * GetUserDataRoot
		 * * The writable root that "user://" maps to. Decided once at first use:
		 *
		 * - DEV / SHARED (no app identity): the historical behavior. If the working
		 *   directory is writable (dev tree / unzipped folder — Main.cpp sets the CWD
		 *   to the exe dir), user data stays next to the app ("."); otherwise it goes
		 *   to %LOCALAPPDATA%/Cosmic/. This keeps SF_Telem/ViperSim recordings exactly
		 *   where they were.
		 *
		 * - PACKAGED (app identity set, S6): a portable.txt next to the exe OR a
		 *   writable exe dir puts user data in "<exe>/user/" (portable mode); a
		 *   read-only exe dir (installed under Program Files) puts it in
		 *   %LOCALAPPDATA%/<AppName>/. Falls back to the system temp dir if
		 *   LOCALAPPDATA is unset.
		 */
		static const std::string& GetUserDataRoot()
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

		////////////////////////////////
		// Project Management
		///////////////////////////////

		// NAME mode. Not thread-safe: must only be called from the main thread before
		// any worker calls Resolve with a project:// path. If background asset loading
		// is introduced, protect this with a shared_mutex.
		static void SetActiveProject(const std::string& name)
		{
			s_ActiveProjectName    = name;
			s_ActiveProjectPath.clear();          // drop any absolute PATH mount
			s_ProjectHasAssetsSubdir = false;
		}

		// PATH mode (S1). Mounts a self-contained project folder at `absoluteRoot`.
		// Probes ONCE for an "assets/" subdir: present -> project:// resolves under
		// <root>/assets/; absent -> under <root>/ (the flat layout the Starforge
		// scaffold writes, where scenes/ sits at the project root). Same threading
		// contract as SetActiveProject (main-thread only).
		static void SetActiveProjectPath(const std::string& absoluteRoot)
		{
			namespace fs = std::filesystem;
			std::error_code ec;
			s_ActiveProjectPath      = fs::path(absoluteRoot).generic_string();
			s_ProjectHasAssetsSubdir = fs::exists(fs::path(absoluteRoot) / "assets", ec);
		}

		// The current absolute PATH-mode root ("" in NAME mode). Lets an app write a
		// sibling folder (build/, .starforge/) next to a project without re-deriving it.
		static const std::string& ActiveProjectPath() { return s_ActiveProjectPath; }

	private:
		////////////////////////////////
		// Internal State
		///////////////////////////////

		// Not thread-safe: the setters must only be called from the main thread,
		// and no worker thread may call Resolve with a project:// path concurrently.
		// If background asset loading is introduced, protect this with a shared_mutex.
		static inline std::string		s_ActiveProjectName		= "";
		static inline std::string		s_ActiveProjectPath		= "";     // "" => NAME mode
		static inline bool				s_ProjectHasAssetsSubdir = false;
		static inline std::string		s_AppIdentity			= "";     // "" => dev/shared user root
	};
}
