#pragma once
// SubTexture2D.h

#include "core/Core.h"
#include "graphics/Texture.h"
#include <glm/glm.hpp>

namespace Cosmic
{
    class COSMIC_API SubTexture2D
    {
    public:
        SubTexture2D(const Ref<Texture2D>& texture, const glm::vec2& min, const glm::vec2& max);

        const Ref<Texture2D>& GetTexture() const { return m_Texture; }
        const glm::vec2* GetTexCoords() const { return m_TexCoords; }

        /**
         * @brief Extracts a discrete tile bounding box from a parent texture atlas grid layout.
         * @param coords The grid coordinate indices representing column/row blocks (e.g., column 2, row 0).
         * @param cellSize The resolution scale of an individual tile unit in absolute pixels.
         * @param spriteSize Optional multiplier for sprites that stretch over multi-tile blocks (defaults to 1x1).
         */
        static Ref<SubTexture2D> CreateFromCoords(
            const Ref<Texture2D>& texture,
            const glm::vec2& coords,
            const glm::vec2& cellSize,
            const glm::vec2& spriteSize = { 1.0f, 1.0f }
        );

    private:
        Ref<Texture2D> m_Texture;
        glm::vec2 m_TexCoords[4];
    };
}