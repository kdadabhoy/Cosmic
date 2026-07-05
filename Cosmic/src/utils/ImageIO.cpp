// utils/ImageIO.cpp — raw pixels -> PNG (S7). See header.

#include "utils/ImageIO.h"
#include "core/Log.h"

#include <stb_image_write.h>

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
}
