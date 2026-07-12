#pragma once

// FileSystem.h
// Last Modified 7/12/2026

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
 * PROCESS-WIDE STATE (Phase 20 / A1 fix): the mount state lives in the ENGINE
 * DLL (FileSystem.cpp) and every module — engine, editor, game DLLs — calls the
 * exported functions, so there is exactly ONE active project per process. The
 * class used to be header-only with `static inline` members, which gave every
 * DLL its own copy: the editor's SetActiveProject never reached engine-compiled
 * code (AssetLibrary, SceneSerializer), so in the dedicated Starforge.exe every
 * engine-side "project://" resolve pointed at the editor's own bundled assets
 * instead of the open project. Application::LoadProjectDLL's set-from-DLL-stem
 * step remains as a harmless default; client setters now override it for real.
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

#include "core/Core.h"

#include <string>
#include <filesystem>
#include <fstream>
#include <cstdlib>

namespace Cosmic
{
	class COSMIC_API FileSystem
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
		static std::string Resolve(const std::string& path);

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
		static void SetAppIdentity(const std::string& name);
		static const std::string& AppIdentity();

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
		static const std::string& GetUserDataRoot();

		////////////////////////////////
		// Project Management
		///////////////////////////////

		// NAME mode. Not thread-safe: must only be called from the main thread before
		// any worker calls Resolve with a project:// path. If background asset loading
		// is introduced, protect this with a shared_mutex.
		static void SetActiveProject(const std::string& name);

		// PATH mode (S1). Mounts a self-contained project folder at `absoluteRoot`.
		// Probes ONCE for an "assets/" subdir: present -> project:// resolves under
		// <root>/assets/; absent -> under <root>/ (the flat layout the Starforge
		// scaffold writes, where scenes/ sits at the project root). Same threading
		// contract as SetActiveProject (main-thread only).
		static void SetActiveProjectPath(const std::string& absoluteRoot);

		// The current absolute PATH-mode root ("" in NAME mode). Lets an app write a
		// sibling folder (build/, .starforge/) next to a project without re-deriving it.
		static const std::string& ActiveProjectPath();
	};
}
