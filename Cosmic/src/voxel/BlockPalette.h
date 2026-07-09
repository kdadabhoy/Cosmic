#pragma once
// voxel/BlockPalette.h
//
// ============================================================================
// Cosmic voxel worlds — the per-volume block-type table (Phase 18 / V1).
// ============================================================================
//
// A BlockPalette maps a compact voxel id (uint16, 0 = Air) to a BlockType: a
// display name, gameplay flags (solid / transparent / emissive), a per-FACE
// atlas tile index (top / side / bottom), and a base colour. The VoxelVolume
// stores only ids; the palette turns an id into everything the mesher and the
// renderer need — which faces occlude their neighbours (solid & opaque), which
// atlas tile each face samples, and (when no atlas image is supplied) the colour
// a procedural atlas paints per tile.
//
// Id 0 is ALWAYS Air (empty space): non-solid, no faces, no collision. A fresh
// palette reserves it. Block ids are dense (1, 2, 3, ...) so the id doubles as
// the default atlas tile index.
//
// This header is GL-free, allocation-light, and headless-testable. The `.cpal`
// JSON (de)serialization lives in BlockPalette.cpp (needs nlohmann, engine-only).
// A palette is NOT run through the generic reflected-struct serializer: the
// reflection registry has no array-of-struct field kind, so the palette owns a
// small purpose-built JSON schema instead.
// ============================================================================

#include "core/Core.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace Cosmic
{
    // One block type. Faces reference atlas tiles by index; the atlas is an
    // AtlasTilesX x AtlasTilesY grid (row-major, top-left origin). Colour is the
    // procedural-atlas fill + a generic tint the renderer can fall back to.
    struct BlockType
    {
        std::string Name = "Block";

        bool Solid       = true;    // occludes neighbour faces + has collision
        bool Transparent = false;   // opaque solids cull neighbour faces; a
                                    // transparent solid (glass — parked) does not
        bool Emissive    = false;   // reserved for the parked voxel-light unlock

        // Per-face atlas tile index (into the AtlasTilesX*AtlasTilesY grid).
        uint16_t TileTop    = 0;
        uint16_t TileSide   = 0;
        uint16_t TileBottom = 0;

        glm::vec3 Color{ 0.8f, 0.8f, 0.8f };   // procedural-atlas fill / tint

        BlockType() = default;
        BlockType(const std::string& name, const glm::vec3& color)
            : Name(name), Color(color) {}
    };

    class COSMIC_API BlockPalette
    {
    public:
        BlockPalette() { Reset(); }

        /** @brief Factory to match the engine's Ref<> allocation convention. */
        template<typename... Args>
        static Ref<BlockPalette> Create(Args&&... args)
        {
            return std::make_shared<BlockPalette>(std::forward<Args>(args)...);
        }

        /** @brief Clear to just Air (id 0). AtlasTiles left at 1x1 until (re)filled. */
        void Reset()
        {
            m_Blocks.clear();
            BlockType air("Air", { 0.0f, 0.0f, 0.0f });
            air.Solid = false;
            m_Blocks.push_back(air);          // id 0 == Air
            m_AtlasTilesX = 1;
            m_AtlasTilesY = 1;
        }

        /** @brief Append a solid block whose three faces all sample tile `id` (the
         *  new block's own id) — the common "one tile per block" case. Returns the
         *  new id. Recomputes the atlas grid to fit. */
        uint16_t AddBlock(const std::string& name, const glm::vec3& color)
        {
            const uint16_t id = static_cast<uint16_t>(m_Blocks.size());
            BlockType b(name, color);
            b.TileTop = b.TileSide = b.TileBottom = id;
            m_Blocks.push_back(b);
            RefitAtlas();
            return id;
        }

        /** @brief Append a fully specified block (per-face tiles already set). */
        uint16_t AddBlock(const BlockType& block)
        {
            m_Blocks.push_back(block);
            RefitAtlas();
            return static_cast<uint16_t>(m_Blocks.size() - 1);
        }

        // ---- queries (pure, hot-path safe) ----------------------------------

        uint16_t Count() const { return static_cast<uint16_t>(m_Blocks.size()); }

        bool IsAir(uint16_t id) const { return id == 0 || id >= m_Blocks.size(); }

        /** @brief Solid AND within range. Air (0) and out-of-range ids are not solid. */
        bool IsSolid(uint16_t id) const
        {
            return id != 0 && id < m_Blocks.size() && m_Blocks[id].Solid;
        }

        /** @brief True when face F of block A should be culled by neighbour B:
         *  an opaque solid neighbour hides it. A transparent neighbour (or Air)
         *  never culls, so glass/leaves faces survive (parked-blocks-ready). */
        bool Occludes(uint16_t neighbour) const
        {
            return neighbour < m_Blocks.size() && m_Blocks[neighbour].Solid
                && !m_Blocks[neighbour].Transparent;
        }

        const BlockType& Get(uint16_t id) const
        {
            return m_Blocks[id < m_Blocks.size() ? id : 0];
        }
        BlockType&       GetMutable(uint16_t id)
        {
            return m_Blocks[id < m_Blocks.size() ? id : 0];
        }

        const std::vector<BlockType>& Blocks() const { return m_Blocks; }

        uint32_t AtlasTilesX() const { return m_AtlasTilesX; }
        uint32_t AtlasTilesY() const { return m_AtlasTilesY; }
        void SetAtlasTiles(uint32_t x, uint32_t y)
        {
            m_AtlasTilesX = x < 1 ? 1 : x;
            m_AtlasTilesY = y < 1 ? 1 : y;
        }

        /** @brief Normalized UV rect {u0, v0, u1, v1} of atlas tile `tile`
         *  (row-major, top-left origin — matches SpriteAnimation::FrameUV). Pure. */
        glm::vec4 TileUV(uint16_t tile) const
        {
            const uint32_t tx = m_AtlasTilesX, ty = m_AtlasTilesY;
            const uint32_t col = tx ? (tile % tx) : 0;
            const uint32_t row = tx ? (tile / tx) : 0;
            const float u0 = (float)col / (float)tx;
            const float u1 = (float)(col + 1) / (float)tx;
            const float v0 = (float)row / (float)ty;
            const float v1 = (float)(row + 1) / (float)ty;
            return { u0, v0, u1, v1 };
        }

        // ---- `.cpal` JSON (BlockPalette.cpp) --------------------------------

        /** @brief Serialize to a `.cpal` JSON string (pure — no disk I/O). */
        std::string ToJson() const;
        /** @brief Parse a `.cpal` JSON string; returns false (palette left as-is)
         *  on malformed input. Always keeps id 0 == Air. */
        bool        FromJson(const std::string& json);

        /** @brief Load / save a `.cpal` via the VFS (paths go through FileSystem). */
        static Ref<BlockPalette> Load(const std::string& path);
        bool                     Save(const std::string& path) const;

        /** @brief The ForgeBlocks starter set: grass, dirt, stone, sand, wood,
         *  leaves (ids 1..6). Convenient default for samples + the editor. */
        static Ref<BlockPalette> CreateDefault();

    private:
        // Grow the square-ish atlas grid so every distinct tile index fits. Tiles
        // are addressed by index, so the grid just needs >= (maxTile+1) cells.
        void RefitAtlas()
        {
            uint32_t maxTile = 0;
            for (const BlockType& b : m_Blocks)
            {
                maxTile = std::max<uint32_t>(maxTile, b.TileTop);
                maxTile = std::max<uint32_t>(maxTile, b.TileSide);
                maxTile = std::max<uint32_t>(maxTile, b.TileBottom);
            }
            const uint32_t need = maxTile + 1;
            uint32_t side = 1;
            while (side * side < need) ++side;
            m_AtlasTilesX = side;
            m_AtlasTilesY = side;
        }

        std::vector<BlockType> m_Blocks;      // index == block id; [0] == Air
        uint32_t m_AtlasTilesX = 1;
        uint32_t m_AtlasTilesY = 1;
    };
}
