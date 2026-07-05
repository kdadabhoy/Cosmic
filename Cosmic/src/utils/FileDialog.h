#pragma once

// utils/FileDialog.h
//
// ============================================================================
// Cosmic — native file dialogs (Phase 14 / H6).
// ============================================================================
//
// A generic engine verb for "pick a file / folder" so no app hand-types paths.
// The Win32 IFileDialog COM implementation is pimpl'd in the .cpp (no <windows.h>
// in this header — the FileWatcher precedent). Modal on the main thread: call ONLY
// from a UI-event context (a button handler), never from a render callback.
//
// Paths returned are ABSOLUTE. InitialDir may be a VFS path (e.g. "project://models")
// — it is resolved through FileSystem::Resolve. The CALLER decides whether to copy a
// picked file into project:// (Import does; asset-slot "…" buttons translate a file
// already under the project root into its project:// form and offer copy-in otherwise).
// ============================================================================

#include "core/Core.h"

#include <optional>
#include <string>
#include <vector>

namespace Cosmic
{
	// One filter row, e.g. { "3D models", "*.obj;*.fbx;*.stl" }.
	struct FileFilter
	{
		std::string Name;
		std::string Spec;   // semicolon-separated wildcard patterns
	};

	struct FileDialogDesc
	{
		std::string             Title = "Open";
		std::vector<FileFilter> Filters;          // empty => "All files (*.*)"
		std::string             InitialDir;        // VFS or absolute path; "" => system default
		std::string             DefaultExtension;  // e.g. "cscene" (no dot) — Save only
	};

	class COSMIC_API FileDialog
	{
	public:
		// Open an existing file. Returns the absolute path, or nullopt on cancel.
		static std::optional<std::string> Open(const FileDialogDesc& desc);

		// Choose a save path (offers overwrite confirmation). nullopt on cancel.
		static std::optional<std::string> Save(const FileDialogDesc& desc);

		// Pick a folder. nullopt on cancel.
		static std::optional<std::string> PickFolder(const std::string& title = "Select Folder",
		                                             const std::string& initialDir = "");
	};
}
