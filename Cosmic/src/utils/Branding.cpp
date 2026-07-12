// utils/Branding.cpp — drop-a-file branding resolution (K1). See header.

#include "utils/Branding.h"
#include "utils/FileSystem.h"

#include <filesystem>

#ifdef _WIN32
	#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
	#endif
	#ifndef NOMINMAX
	#define NOMINMAX
	#endif
	#include <windows.h>
#endif

namespace Cosmic
{
	namespace
	{
		bool IsFile(const std::filesystem::path& p)
		{
			std::error_code ec;
			return !p.empty() && std::filesystem::is_regular_file(p, ec);
		}
	}

	std::string Branding::ResolveIcon(const IconQuery& q)
	{
		namespace fs = std::filesystem;

		if (!q.ExeDir.empty())
		{
			const fs::path p = fs::path(q.ExeDir) / "branding" / "icon.png";
			if (IsFile(p)) return p.generic_string();
		}
		if (!q.UserRoot.empty())
		{
			const fs::path p = fs::path(q.UserRoot) / "branding" / "icon.png";
			if (IsFile(p)) return p.generic_string();
		}
		if (!q.ManifestIcon.empty() && IsFile(q.ManifestIcon))
			return fs::path(q.ManifestIcon).generic_string();
		if (!q.ProjectRoot.empty())
		{
			const fs::path p = fs::path(q.ProjectRoot) / "icon.png";
			if (IsFile(p)) return p.generic_string();
		}
		return {};
	}

	Branding::IconQuery Branding::QueryForProcess(const std::string& manifestIconVfsOrDisk,
	                                              bool includeProjectIcon)
	{
		IconQuery q;
		q.ExeDir   = ExecutableDir();
		q.UserRoot = FileSystem::GetUserDataRoot();
		if (!manifestIconVfsOrDisk.empty())
			q.ManifestIcon = FileSystem::Resolve(manifestIconVfsOrDisk);
		if (includeProjectIcon)
		{
			// The parent dir of a probe file under project:// — Resolve maps the
			// mount (NAME or PATH mode) for us.
			q.ProjectRoot = std::filesystem::path(
				FileSystem::Resolve("project://icon.png")).parent_path().generic_string();
		}
		return q;
	}

	std::string Branding::ResolveProcessIcon(const std::string& manifestIconVfsOrDisk,
	                                         bool includeProjectIcon)
	{
		return ResolveIcon(QueryForProcess(manifestIconVfsOrDisk, includeProjectIcon));
	}

	std::string Branding::ExecutableDir()
	{
	#ifdef _WIN32
		char exePath[MAX_PATH] = {};
		if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0)
			return std::filesystem::path(exePath).parent_path().generic_string();
	#endif
		return ".";
	}
}
