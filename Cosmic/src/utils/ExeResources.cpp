// utils/ExeResources.cpp — embed an application icon into a PE exe (S5). See header.

#include "utils/ExeResources.h"
#include "core/Log.h"

#include <cstdint>
#include <cstring>
#include <vector>

#ifdef _WIN32
	#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
	#endif
	#ifndef NOMINMAX
	#define NOMINMAX
	#endif
	#include <windows.h>
	#include <stb_image.h>
#endif

namespace Cosmic
{
#ifdef _WIN32
	namespace
	{
		// One decoded icon size + its 32bpp RGBA (top-left origin) pixels.
		struct IconLevel
		{
			int                   size = 0;
			std::vector<uint8_t>  rgba;   // size*size*4, top-left origin
		};

		// Nearest-box downscale of a square RGBA image (top-left origin).
		std::vector<uint8_t> Downscale(const uint8_t* src, int srcW, int srcH, int dst)
		{
			std::vector<uint8_t> out(static_cast<size_t>(dst) * dst * 4);
			for (int y = 0; y < dst; ++y)
			{
				const int sy = (y * srcH) / dst;
				for (int x = 0; x < dst; ++x)
				{
					const int sx = (x * srcW) / dst;
					const uint8_t* s = src + (static_cast<size_t>(sy) * srcW + sx) * 4;
					uint8_t* d = out.data() + (static_cast<size_t>(y) * dst + x) * 4;
					d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
				}
			}
			return out;
		}

		// Build one RT_ICON image blob: BITMAPINFOHEADER (height doubled for the AND
		// mask) + 32bpp BGRA bottom-up XOR bitmap + a 1bpp all-zero AND mask.
		std::vector<uint8_t> BuildIconImage(const IconLevel& lvl)
		{
			const int w = lvl.size, h = lvl.size;
			const size_t xorBytes  = static_cast<size_t>(w) * h * 4;
			const size_t maskStride = ((static_cast<size_t>(w) + 31) / 32) * 4;   // DWORD-aligned rows
			const size_t maskBytes  = maskStride * h;

			std::vector<uint8_t> blob(sizeof(BITMAPINFOHEADER) + xorBytes + maskBytes, 0);

			auto* bih = reinterpret_cast<BITMAPINFOHEADER*>(blob.data());
			bih->biSize        = sizeof(BITMAPINFOHEADER);
			bih->biWidth       = w;
			bih->biHeight      = h * 2;           // XOR + AND mask stacked
			bih->biPlanes      = 1;
			bih->biBitCount    = 32;
			bih->biCompression = BI_RGB;

			uint8_t* xorBits = blob.data() + sizeof(BITMAPINFOHEADER);
			for (int y = 0; y < h; ++y)
			{
				// DIB is bottom-up: file row 0 is the image's bottom row.
				const uint8_t* srcRow = lvl.rgba.data() + static_cast<size_t>(h - 1 - y) * w * 4;
				uint8_t*       dstRow = xorBits + static_cast<size_t>(y) * w * 4;
				for (int x = 0; x < w; ++x)
				{
					const uint8_t* s = srcRow + static_cast<size_t>(x) * 4;   // RGBA
					uint8_t*       d = dstRow + static_cast<size_t>(x) * 4;   // BGRA
					d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3];
				}
			}
			// AND mask stays all-zero: 32bpp alpha handles transparency.
			return blob;
		}

#pragma pack(push, 1)
		struct GrpIconDirEntry
		{
			BYTE  bWidth;
			BYTE  bHeight;
			BYTE  bColorCount;
			BYTE  bReserved;
			WORD  wPlanes;
			WORD  wBitCount;
			DWORD dwBytesInRes;
			WORD  nID;
		};
		struct GrpIconDir
		{
			WORD idReserved;
			WORD idType;
			WORD idCount;
			// GrpIconDirEntry[idCount] follows
		};
#pragma pack(pop)
	}

	bool ExeResources::SetIcon(const std::string& exePath, const std::string& pngPath)
	{
		int w = 0, h = 0, ch = 0;
		uint8_t* src = stbi_load(pngPath.c_str(), &w, &h, &ch, 4);
		if (!src || w <= 0 || h <= 0)
		{
			CS_CORE_ERROR("ExeResources::SetIcon: could not read PNG '{0}'.", pngPath);
			if (src) stbi_image_free(src);
			return false;
		}

		// Standard icon sizes, largest-first (the shell picks the best fit).
		const int sizes[] = { 256, 48, 32, 16 };
		std::vector<IconLevel> levels;
		levels.reserve(4);
		for (int s : sizes)
		{
			IconLevel lvl;
			lvl.size = s;
			lvl.rgba = Downscale(src, w, h, s);
			levels.push_back(std::move(lvl));
		}
		stbi_image_free(src);

		// Assemble the RT_GROUP_ICON directory (WORD id per entry, not a file offset).
		std::vector<uint8_t> group(sizeof(GrpIconDir) + levels.size() * sizeof(GrpIconDirEntry));
		auto* dir = reinterpret_cast<GrpIconDir*>(group.data());
		dir->idReserved = 0;
		dir->idType     = 1;   // 1 = icon
		dir->idCount    = static_cast<WORD>(levels.size());

		std::vector<std::vector<uint8_t>> images(levels.size());
		auto* entry = reinterpret_cast<GrpIconDirEntry*>(group.data() + sizeof(GrpIconDir));
		for (size_t i = 0; i < levels.size(); ++i)
		{
			images[i] = BuildIconImage(levels[i]);
			const int s = levels[i].size;
			entry[i].bWidth      = static_cast<BYTE>(s >= 256 ? 0 : s);   // 0 means 256
			entry[i].bHeight     = static_cast<BYTE>(s >= 256 ? 0 : s);
			entry[i].bColorCount = 0;
			entry[i].bReserved   = 0;
			entry[i].wPlanes     = 1;
			entry[i].wBitCount   = 32;
			entry[i].dwBytesInRes = static_cast<DWORD>(images[i].size());
			entry[i].nID          = static_cast<WORD>(i + 1);
		}

		HANDLE upd = BeginUpdateResourceA(exePath.c_str(), FALSE);
		if (!upd)
		{
			CS_CORE_ERROR("ExeResources::SetIcon: BeginUpdateResource('{0}') failed (err {1}).",
			              exePath, (unsigned)GetLastError());
			return false;
		}

		bool ok = true;
		const WORD lang = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);

		// Each icon image as RT_ICON (ids 1..N).
		for (size_t i = 0; i < images.size() && ok; ++i)
		{
			if (!UpdateResourceA(upd, (LPSTR)RT_ICON, MAKEINTRESOURCEA((WORD)(i + 1)), lang,
			                     images[i].data(), (DWORD)images[i].size()))
			{
				CS_CORE_ERROR("ExeResources::SetIcon: UpdateResource(RT_ICON {0}) failed (err {1}).",
				              i + 1, (unsigned)GetLastError());
				ok = false;
			}
		}

		// The directory as RT_GROUP_ICON id 1 (the shell's default app icon id).
		if (ok && !UpdateResourceA(upd, (LPSTR)RT_GROUP_ICON, MAKEINTRESOURCEA(1), lang,
		                           group.data(), (DWORD)group.size()))
		{
			CS_CORE_ERROR("ExeResources::SetIcon: UpdateResource(RT_GROUP_ICON) failed (err {0}).",
			              (unsigned)GetLastError());
			ok = false;
		}

		if (!EndUpdateResource(upd, ok ? FALSE : TRUE))
		{
			if (ok)   // commit failed
				CS_CORE_ERROR("ExeResources::SetIcon: EndUpdateResource commit failed (err {0}).",
				              (unsigned)GetLastError());
			ok = false;
		}

		if (ok)
			CS_CORE_INFO("ExeResources::SetIcon: embedded icon from '{0}' into '{1}'.", pngPath, exePath);
		return ok;
	}
#else
	bool ExeResources::SetIcon(const std::string& exePath, const std::string& pngPath)
	{
		CS_CORE_WARN("ExeResources::SetIcon: icon embedding is Windows-only (skipped for '{0}').", exePath);
		(void)exePath; (void)pngPath;
		return false;
	}
#endif
}
