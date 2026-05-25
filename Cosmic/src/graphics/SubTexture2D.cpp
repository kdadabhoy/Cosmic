#include "graphics/SubTexture2D.h"

namespace Cosmic
{
	SubTexture2D::SubTexture2D(const Ref<Texture2D>& texture, const glm::vec2& min, const glm::vec2& max)
		: m_Texture(texture)
	{
		// Populates vertex map coordinates in traditional counter-clockwise order:
		// Bottom-Left -> Bottom-Right -> Top-Right -> Top-Left
		m_TexCoords[0] = min;
		m_TexCoords[1] = { max.x, min.y };
		m_TexCoords[2] = max;
		m_TexCoords[3] = { min.x, max.y };
	}

	Ref<SubTexture2D> SubTexture2D::CreateFromCoords(
		const Ref<Texture2D>& texture,
		const glm::vec2& coords,
		const glm::vec2& cellSize,
		const glm::vec2& spriteSize)
	{
		float textureWidth = static_cast<float>(texture->GetWidth());
		float textureHeight = static_cast<float>(texture->GetHeight());

		// Map pixel coordinates to the normalized float ranges [0.0, 1.0] expected by the GPU
		glm::vec2 min = {
			(coords.x * cellSize.x) / textureWidth,
			(coords.y * cellSize.y) / textureHeight
		};

		glm::vec2 max = {
			((coords.x + spriteSize.x) * cellSize.x) / textureWidth,
			((coords.y + spriteSize.y) * cellSize.y) / textureHeight
		};

		return std::make_shared<SubTexture2D>(texture, min, max);
	}
}