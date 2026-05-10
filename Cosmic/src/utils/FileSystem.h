#pragma once
#include <string>
#include <filesystem>

namespace Cosmic
{
	class FileSystem
	{
	public:
		// Converts "assets/Dino.png" -> "projects/DinoProject/assets/Dino.png"
		// based on which project is currently active.

		static std::string Resolve(const std::string& path)
		{
			// Logic: If path starts with "engine://", look in engine assets
			// If it starts with "project://", look in the active project's subfolder

			if (path.find("engine://") == 0)
			{
				return "assets/" + path.substr(9);
			}
			if (path.find("project://") == 0)
			{
				return "assets/" + s_ActiveProjectName + "/" + path.substr(10);
			}

			return path; // Fallback
		}

		static void SetActiveProject(const std::string& name) { s_ActiveProjectName = name; }

	private:
		static inline std::string s_ActiveProjectName = "";
	};
}