#pragma once

// utils/ImageIO.h
//
// Tiny generic engine verbs for image files — writing raw pixels (screenshot /
// thumbnail capture, Phase 16 / S7) and decoding image files into raw RGBA
// (runtime branding icons, Phase 22 / K1). Keeps stb_image / stb_image_write
// inside the engine DLL so apps don't need the vendored headers on their
// include path.

#include "core/Core.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Cosmic
{
	class COSMIC_API ImageIO
	{
	public:
		// Write `channels`-channel 8-bit pixels (row-major, top-left origin) to a PNG
		// at `path`. Returns false and logs on failure.
		static bool WritePNG(const std::string& path, int width, int height,
		                     int channels, const uint8_t* data);

		// Decode an image file (any stb-supported format: PNG/JPG/BMP/TGA/…) into
		// 8-bit RGBA, row-major, TOP-LEFT origin (the OS icon convention — this
		// explicitly forces stb's flip-on-load OFF, which the GL texture loader
		// leaves globally ON). Returns false and logs on failure; `rgba` is sized
		// width*height*4 on success.
		static bool ReadPixels(const std::string& path, int& width, int& height,
		                       std::vector<uint8_t>& rgba);

		// Resample an RGBA8 image (top-left origin) to dstW x dstH: box-average
		// when shrinking (each destination pixel averages its source cell) and
		// bilinear when enlarging. `dst` must hold dstW*dstH*4 bytes. Pure CPU —
		// headless-testable.
		static void ResizeRgba(const uint8_t* src, int srcW, int srcH,
		                       uint8_t* dst, int dstW, int dstH);
	};
}
