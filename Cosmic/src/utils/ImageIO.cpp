// utils/ImageIO.cpp — raw pixels <-> image files (S7 write, K1 read/resize). See header.

#include "utils/ImageIO.h"
#include "core/Log.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>

namespace Cosmic
{
	bool ImageIO::WritePNG(const std::string& path, int width, int height,
	                       int channels, const uint8_t* data)
	{
		if (!data || width <= 0 || height <= 0 || channels < 1 || channels > 4)
		{
			CS_CORE_ERROR("ImageIO::WritePNG: invalid arguments for '{0}'.", path);
			return false;
		}
		const int rc = stbi_write_png(path.c_str(), width, height, channels, data, width * channels);
		if (rc == 0)
			CS_CORE_ERROR("ImageIO::WritePNG: failed to write '{0}'.", path);
		return rc != 0;
	}

	bool ImageIO::ReadPixels(const std::string& path, int& width, int& height,
	                         std::vector<uint8_t>& rgba)
	{
		// The GL texture loader sets stb's flip-on-load globally (UV origin);
		// icons/branding need the file's own top-left orientation, so force it
		// off for this decode. (stb's flag is per-thread in recent versions and
		// global in older ones — either way, texture loads re-assert their own.)
		stbi_set_flip_vertically_on_load(0);

		int w = 0, h = 0, ch = 0;
		uint8_t* pixels = stbi_load(path.c_str(), &w, &h, &ch, 4);
		if (!pixels || w <= 0 || h <= 0)
		{
			if (pixels) stbi_image_free(pixels);
			CS_CORE_WARN("ImageIO::ReadPixels: could not decode '{0}'.", path);
			return false;
		}

		width  = w;
		height = h;
		rgba.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
		stbi_image_free(pixels);
		return true;
	}

	void ImageIO::ResizeRgba(const uint8_t* src, int srcW, int srcH,
	                         uint8_t* dst, int dstW, int dstH)
	{
		if (!src || !dst || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
			return;

		// Same size: straight copy.
		if (srcW == dstW && srcH == dstH)
		{
			std::copy(src, src + static_cast<size_t>(srcW) * srcH * 4, dst);
			return;
		}

		const bool shrinking = dstW <= srcW && dstH <= srcH;
		for (int y = 0; y < dstH; ++y)
		{
			for (int x = 0; x < dstW; ++x)
			{
				uint8_t* d = dst + (static_cast<size_t>(y) * dstW + x) * 4;
				if (shrinking)
				{
					// Box average over this destination pixel's source cell.
					const int x0 = (x * srcW) / dstW;
					const int y0 = (y * srcH) / dstH;
					const int x1 = std::max(x0 + 1, ((x + 1) * srcW) / dstW);
					const int y1 = std::max(y0 + 1, ((y + 1) * srcH) / dstH);
					uint32_t acc[4] = { 0, 0, 0, 0 };
					for (int sy = y0; sy < y1; ++sy)
						for (int sx = x0; sx < x1; ++sx)
						{
							const uint8_t* s = src + (static_cast<size_t>(sy) * srcW + sx) * 4;
							acc[0] += s[0]; acc[1] += s[1]; acc[2] += s[2]; acc[3] += s[3];
						}
					const uint32_t n = static_cast<uint32_t>((x1 - x0) * (y1 - y0));
					d[0] = static_cast<uint8_t>(acc[0] / n);
					d[1] = static_cast<uint8_t>(acc[1] / n);
					d[2] = static_cast<uint8_t>(acc[2] / n);
					d[3] = static_cast<uint8_t>(acc[3] / n);
				}
				else
				{
					// Bilinear sample at the destination pixel's source center.
					const float fx = (x + 0.5f) * srcW / dstW - 0.5f;
					const float fy = (y + 0.5f) * srcH / dstH - 0.5f;
					const int   x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, srcW - 1);
					const int   y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, srcH - 1);
					const int   x1 = std::min(x0 + 1, srcW - 1);
					const int   y1 = std::min(y0 + 1, srcH - 1);
					const float tx = std::clamp(fx - x0, 0.0f, 1.0f);
					const float ty = std::clamp(fy - y0, 0.0f, 1.0f);
					const uint8_t* s00 = src + (static_cast<size_t>(y0) * srcW + x0) * 4;
					const uint8_t* s10 = src + (static_cast<size_t>(y0) * srcW + x1) * 4;
					const uint8_t* s01 = src + (static_cast<size_t>(y1) * srcW + x0) * 4;
					const uint8_t* s11 = src + (static_cast<size_t>(y1) * srcW + x1) * 4;
					for (int c = 0; c < 4; ++c)
					{
						const float top = s00[c] + (s10[c] - s00[c]) * tx;
						const float bot = s01[c] + (s11[c] - s01[c]) * tx;
						d[c] = static_cast<uint8_t>(std::lround(top + (bot - top) * ty));
					}
				}
			}
		}
	}
}
