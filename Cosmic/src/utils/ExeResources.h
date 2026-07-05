#pragma once

// utils/ExeResources.h
//
// ============================================================================
// Cosmic — executable resource authoring (Phase 16 / S5).
// ============================================================================
//
// A generic engine verb: stamp a Windows PE executable with an application icon.
// The packager (Starforge) uses it so a shipped <Project>.exe shows the project's
// own icon in Explorer/taskbar instead of the engine rocket. No Cosmic-/editor-
// branded names cross this seam — it takes a raw exe path + a raw PNG path.
//
// Win32-only (BeginUpdateResource / UpdateResource). On non-Windows it logs and
// returns false. Operates on the exe path given — callers pass a COPY in dist/,
// never the SDK's live binary: UpdateResource fails on a running image and any
// change invalidates an Authenticode signature (so sign AFTER embedding).
// ============================================================================

#include "core/Core.h"

#include <string>

namespace Cosmic
{
	class COSMIC_API ExeResources
	{
	public:
		// Convert `pngPath` to a multi-size in-memory .ico (16/32/48/256, nearest-box
		// downscale) and write it into `exePath` as RT_GROUP_ICON + RT_ICON. Returns
		// false (and logs) on any failure: missing/unreadable PNG, exe not writable,
		// or non-Windows. The PNG should be square RGBA for the best result.
		static bool SetIcon(const std::string& exePath, const std::string& pngPath);
	};
}
