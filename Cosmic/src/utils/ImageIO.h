#pragma once

// utils/ImageIO.h
//
// A tiny generic engine verb for writing raw pixels to an image file — used by
// screenshot / thumbnail capture (Phase 16 / S7). Keeps stb_image_write inside the
// engine DLL so apps don't need the vendored header on their include path.

#include "core/Core.h"

#include <cstdint>
#include <string>

namespace Cosmic
{
	class COSMIC_API ImageIO
	{
	public:
		// Write `channels`-channel 8-bit pixels (row-major, top-left origin) to a PNG
		// at `path`. Returns false and logs on failure.
		static bool WritePNG(const std::string& path, int width, int height,
		                     int channels, const uint8_t* data);
	};
}
