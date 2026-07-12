#pragma once

// utils/Branding.h
//
// ============================================================================
// Drop-a-file branding — the app-icon resolution convention (Phase 22 / K1,
// roadmap decision #11 2026-07-11).
// ============================================================================
//
// One convention, three consumers (Starforge, Launcher, PlayerLayer/packaged
// apps): the window/taskbar icon (and any in-app logo drawn from the same
// file) resolves from `branding/icon.png` with per-user and per-project
// overrides. FIRST HIT WINS:
//
//   1. <exe dir>/branding/icon.png      — the app's shipped brand
//   2. user://branding/icon.png         — per-user override (user:// is already
//                                         per-app once SetAppIdentity ran)
//   3. <manifest icon>                  — the project.cproj `icon` key (S5),
//                                         resolved by the caller
//   4. project://icon.png               — the S5 project-icon convention
//   5. (none)                           — "" -> keep the platform default icon
//
// Replacing the resolved image ON DISK re-brands a running app with zero code
// edits — hosts re-resolve + re-apply (Starforge watches the file, K1c). The
// Packager's exe-embed (S5) stays the Explorer/pinned-shortcut icon source;
// this convention covers the LIVE window + taskbar.
//
// ResolveIcon(IconQuery) is pure filesystem-probing over explicit roots —
// headless-testable (tests/test_branding.cpp). QueryForProcess fills a query
// from the running process (exe dir, user:// root, active project mount).
// ============================================================================

#include "core/Core.h"

#include <string>

namespace Cosmic
{
	class COSMIC_API Branding
	{
	public:
		// Explicit probe roots — every field optional ("" = skip that candidate).
		struct IconQuery
		{
			std::string ExeDir;        // probes <ExeDir>/branding/icon.png
			std::string UserRoot;      // probes <UserRoot>/branding/icon.png
			std::string ManifestIcon;  // probes this exact file (caller-resolved)
			std::string ProjectRoot;   // probes <ProjectRoot>/icon.png
		};

		// The first candidate that exists as a regular file, in the documented
		// order. "" = none found (keep the platform default icon).
		static std::string ResolveIcon(const IconQuery& query);

		// Build a query for the RUNNING process: exe dir + user:// root always;
		// `manifestIconVfsOrDisk` (e.g. "project://" + manifest.Icon) and the
		// project://icon.png probe only when the caller opts in (hosts that have
		// a mounted project — the PlayerLayer and a Starforge Run/Play session).
		static IconQuery QueryForProcess(const std::string& manifestIconVfsOrDisk = "",
		                                 bool includeProjectIcon = false);

		// Sugar: ResolveIcon(QueryForProcess(...)).
		static std::string ResolveProcessIcon(const std::string& manifestIconVfsOrDisk = "",
		                                      bool includeProjectIcon = false);

		// Absolute directory of the running executable ("." fallback off-Windows).
		static std::string ExecutableDir();
	};
}
