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
		 * - "engine://"  -> Maps to the base engine asset directory.
		 * - "project://" -> Maps to a sub-folder named after the active project.
		 */
		static std::string Resolve(const std::string& path)
		{
			if (path.find("engine://") == 0)
			{
				return "assets/" + path.substr(9);
			}

			if (path.find("project://") == 0)
			{
				return "assets/" + s_ActiveProjectName + "/" + path.substr(10);
			}

			return path; // Fallback for raw paths
		}

		////////////////////////////////
		// Project Management
		///////////////////////////////

		static void						SetActiveProject(const std::string& name)			{ s_ActiveProjectName = name; }

	private:
		////////////////////////////////
		// Internal State
		///////////////////////////////

		static inline std::string		s_ActiveProjectName		= "";
	};
}