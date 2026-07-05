// utils/FileDialog.cpp — Win32 IFileDialog implementation of the FileDialog verb (H6).
// See FileDialog.h. All Win32/COM lives here so the header stays platform-clean.

#include "utils/FileDialog.h"
#include "utils/FileSystem.h"
#include "core/Log.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>   // IFileDialog / IFileOpenDialog / IFileSaveDialog

#include <string>
#include <vector>

#pragma comment(lib, "Ole32.lib")    // CoInitializeEx / CoCreateInstance / CoTaskMemFree
#pragma comment(lib, "Shell32.lib")  // SHCreateItemFromParsingName

namespace Cosmic
{
	namespace
	{
		// UTF-8 <-> UTF-16 (the Win32 shell API is wide).
		std::wstring Widen(const std::string& s)
		{
			if (s.empty()) return {};
			const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
			std::wstring w(n, L'\0');
			MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
			return w;
		}

		std::string Narrow(const wchar_t* w)
		{
			if (!w || !*w) return {};
			const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
			std::string s(n > 0 ? n - 1 : 0, '\0');
			if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
			return s;
		}

		// RAII CoInitialize — tolerant of an already-initialized apartment (the OS
		// dialogs used elsewhere may have done it) per the H6 gotcha.
		struct ComInit
		{
			bool Owned = false;
			ComInit()
			{
				const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
				Owned = SUCCEEDED(hr);   // RPC_E_CHANGED_MODE => already inited differently; do NOT uninit
			}
			~ComInit() { if (Owned) CoUninitialize(); }
		};

		// Resolve a VFS-or-absolute initial dir to an absolute filesystem path.
		std::wstring ResolveInitialDir(const std::string& dir)
		{
			if (dir.empty()) return {};
			std::string resolved = dir;
			if (dir.find("://") != std::string::npos)
				resolved = FileSystem::Resolve(dir);
			return Widen(resolved);
		}

		void ApplyCommon(IFileDialog* dialog, const FileDialogDesc& desc,
		                 std::vector<COMDLG_FILTERSPEC>& specs,
		                 std::vector<std::wstring>& storage)
		{
			if (!desc.Title.empty())
				dialog->SetTitle(Widen(desc.Title).c_str());

			// Filters — two wide strings per row kept alive in `storage`.
			if (!desc.Filters.empty())
			{
				storage.reserve(desc.Filters.size() * 2);
				for (const auto& f : desc.Filters)
				{
					storage.push_back(Widen(f.Name));
					storage.push_back(Widen(f.Spec));
				}
				specs.reserve(desc.Filters.size());
				for (size_t i = 0; i < desc.Filters.size(); ++i)
					specs.push_back({ storage[i * 2].c_str(), storage[i * 2 + 1].c_str() });
				dialog->SetFileTypes((UINT)specs.size(), specs.data());
			}

			if (!desc.DefaultExtension.empty())
				dialog->SetDefaultExtension(Widen(desc.DefaultExtension).c_str());

			// Start folder.
			const std::wstring initial = ResolveInitialDir(desc.InitialDir);
			if (!initial.empty())
			{
				IShellItem* folder = nullptr;
				if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&folder))))
				{
					dialog->SetFolder(folder);
					folder->Release();
				}
			}
		}

		// Run a configured dialog + read back the chosen path.
		std::optional<std::string> RunDialog(IFileDialog* dialog)
		{
			// Parent to the active app window so the modal can't hide behind it.
			if (FAILED(dialog->Show(GetActiveWindow())))
				return std::nullopt;   // cancelled or error

			IShellItem* item = nullptr;
			if (FAILED(dialog->GetResult(&item)) || !item)
				return std::nullopt;

			PWSTR path = nullptr;
			std::optional<std::string> out;
			if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
			{
				out = Narrow(path);
				CoTaskMemFree(path);
			}
			item->Release();
			return out;
		}
	}

	std::optional<std::string> FileDialog::Open(const FileDialogDesc& desc)
	{
		ComInit com;
		IFileOpenDialog* dialog = nullptr;
		if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
		                            IID_PPV_ARGS(&dialog))) || !dialog)
		{
			CS_CORE_ERROR("FileDialog::Open: could not create IFileOpenDialog.");
			return std::nullopt;
		}
		std::vector<COMDLG_FILTERSPEC> specs;
		std::vector<std::wstring>      storage;
		ApplyCommon(dialog, desc, specs, storage);
		auto out = RunDialog(dialog);
		dialog->Release();
		return out;
	}

	std::optional<std::string> FileDialog::Save(const FileDialogDesc& desc)
	{
		ComInit com;
		IFileSaveDialog* dialog = nullptr;
		if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
		                            IID_PPV_ARGS(&dialog))) || !dialog)
		{
			CS_CORE_ERROR("FileDialog::Save: could not create IFileSaveDialog.");
			return std::nullopt;
		}
		std::vector<COMDLG_FILTERSPEC> specs;
		std::vector<std::wstring>      storage;
		ApplyCommon(dialog, desc, specs, storage);
		auto out = RunDialog(dialog);
		dialog->Release();
		return out;
	}

	std::optional<std::string> FileDialog::PickFolder(const std::string& title, const std::string& initialDir)
	{
		ComInit com;
		IFileOpenDialog* dialog = nullptr;
		if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
		                            IID_PPV_ARGS(&dialog))) || !dialog)
			return std::nullopt;

		DWORD opts = 0;
		dialog->GetOptions(&opts);
		dialog->SetOptions(opts | FOS_PICKFOLDERS);

		FileDialogDesc desc;
		desc.Title      = title;
		desc.InitialDir = initialDir;
		std::vector<COMDLG_FILTERSPEC> specs;
		std::vector<std::wstring>      storage;
		ApplyCommon(dialog, desc, specs, storage);

		auto out = RunDialog(dialog);
		dialog->Release();
		return out;
	}
}

#else   // non-Windows: no native dialog yet — log + return nullopt (callers fall back).

namespace Cosmic
{
	std::optional<std::string> FileDialog::Open(const FileDialogDesc&)   { CS_CORE_WARN("FileDialog: not implemented on this platform."); return std::nullopt; }
	std::optional<std::string> FileDialog::Save(const FileDialogDesc&)   { CS_CORE_WARN("FileDialog: not implemented on this platform."); return std::nullopt; }
	std::optional<std::string> FileDialog::PickFolder(const std::string&, const std::string&) { return std::nullopt; }
}

#endif
