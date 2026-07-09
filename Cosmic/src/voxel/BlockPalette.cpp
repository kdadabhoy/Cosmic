// voxel/BlockPalette.cpp — `.cpal` JSON (de)serialization + the ForgeBlocks
// starter palette (Phase 18 / V1). nlohmann is engine-private, so the JSON lives
// here rather than in the header.

#include "voxel/BlockPalette.h"
#include "utils/FileSystem.h"
#include "core/Log.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace Cosmic
{
    using nlohmann::json;

    std::string BlockPalette::ToJson() const
    {
        json j;
        j["cosmic_asset"] = "block_palette";
        j["version"]      = 1;
        j["atlas_tiles_x"] = m_AtlasTilesX;
        j["atlas_tiles_y"] = m_AtlasTilesY;

        json blocks = json::array();
        for (const BlockType& b : m_Blocks)
        {
            json jb;
            jb["name"]        = b.Name;
            jb["solid"]       = b.Solid;
            jb["transparent"] = b.Transparent;
            jb["emissive"]    = b.Emissive;
            jb["tile_top"]    = b.TileTop;
            jb["tile_side"]   = b.TileSide;
            jb["tile_bottom"] = b.TileBottom;
            jb["color"]       = { b.Color.r, b.Color.g, b.Color.b };
            blocks.push_back(std::move(jb));
        }
        j["blocks"] = std::move(blocks);
        return j.dump(2);
    }

    bool BlockPalette::FromJson(const std::string& text)
    {
        json j = json::parse(text, nullptr, /*allow_exceptions*/ false);
        if (j.is_discarded() || !j.contains("blocks") || !j["blocks"].is_array())
            return false;

        std::vector<BlockType> parsed;
        for (const json& jb : j["blocks"])
        {
            BlockType b;
            b.Name        = jb.value("name", std::string("Block"));
            b.Solid       = jb.value("solid", true);
            b.Transparent = jb.value("transparent", false);
            b.Emissive    = jb.value("emissive", false);
            b.TileTop     = static_cast<uint16_t>(jb.value("tile_top", 0));
            b.TileSide    = static_cast<uint16_t>(jb.value("tile_side", 0));
            b.TileBottom  = static_cast<uint16_t>(jb.value("tile_bottom", 0));
            if (jb.contains("color") && jb["color"].is_array() && jb["color"].size() == 3)
                b.Color = { jb["color"][0].get<float>(), jb["color"][1].get<float>(), jb["color"][2].get<float>() };
            parsed.push_back(std::move(b));
        }

        // Guarantee id 0 == Air even if the file omitted / renamed it.
        if (parsed.empty())
            return false;
        parsed[0].Solid = false;
        parsed[0].Name  = parsed[0].Name.empty() ? "Air" : parsed[0].Name;

        m_Blocks = std::move(parsed);
        m_AtlasTilesX = std::max<uint32_t>(1, j.value("atlas_tiles_x", 1u));
        m_AtlasTilesY = std::max<uint32_t>(1, j.value("atlas_tiles_y", 1u));
        return true;
    }

    Ref<BlockPalette> BlockPalette::Load(const std::string& path)
    {
        const std::string resolved = FileSystem::Resolve(path);
        std::ifstream is(std::filesystem::u8path(resolved), std::ios::binary);
        if (!is)
        {
            CS_CORE_WARN("BlockPalette::Load — cannot open '{}'", resolved);
            return nullptr;
        }
        std::stringstream ss;
        ss << is.rdbuf();

        auto pal = BlockPalette::Create();
        if (!pal->FromJson(ss.str()))
        {
            CS_CORE_ERROR("BlockPalette::Load — malformed .cpal '{}'", resolved);
            return nullptr;
        }
        return pal;
    }

    bool BlockPalette::Save(const std::string& path) const
    {
        const std::string resolved = FileSystem::Resolve(path);
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(resolved).parent_path(), ec);

        std::ofstream os(std::filesystem::u8path(resolved), std::ios::binary | std::ios::trunc);
        if (!os)
        {
            CS_CORE_ERROR("BlockPalette::Save — cannot open '{}'", resolved);
            return false;
        }
        os << ToJson();
        return static_cast<bool>(os);
    }

    Ref<BlockPalette> BlockPalette::CreateDefault()
    {
        auto pal = BlockPalette::Create();

        // Grass has a distinct top (green) vs. side (dirt-with-grass) vs. bottom
        // (dirt) — the classic 3-face block. The rest are single-tile blocks.
        BlockType grass("Grass", { 0.34f, 0.62f, 0.24f });
        grass.TileTop    = 1;    // its own id — green top
        grass.TileBottom = 2;    // dirt tile
        grass.TileSide   = 7;    // grass-side tile (added after the plain blocks)
        pal->AddBlock(grass);                                  // id 1

        pal->AddBlock("Dirt",   { 0.42f, 0.30f, 0.18f });      // id 2 -> tile 2
        pal->AddBlock("Stone",  { 0.48f, 0.48f, 0.50f });      // id 3
        pal->AddBlock("Sand",   { 0.80f, 0.74f, 0.52f });      // id 4
        pal->AddBlock("Wood",   { 0.52f, 0.37f, 0.22f });      // id 5
        pal->AddBlock("Leaves", { 0.22f, 0.44f, 0.18f });      // id 6

        // Tile 7 = the grass-side face grass references (a dirt band with a green
        // top edge, approximated by a mid green-brown). It is a tile, not a block.
        pal->SetAtlasTiles(std::max<uint32_t>(pal->AtlasTilesX(), 3),
                           std::max<uint32_t>(pal->AtlasTilesY(), 3));
        return pal;
    }
}
