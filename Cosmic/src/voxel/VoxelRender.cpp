// voxel/VoxelRender.cpp — procedural atlas texture + material, palette version
// hash, and the flattened-recipe mapping (Phase 18 / V3 + V6).

#include "voxel/VoxelRender.h"
#include "voxel/BlockPalette.h"
#include "scene/Components.h"
#include "assets/AssetLibrary.h"
#include "graphics/Material.h"
#include "graphics/Texture.h"

#include <algorithm>
#include <functional>
#include <vector>

namespace Cosmic
{
    namespace
    {
        template<typename T>
        void HashCombine(std::size_t& seed, const T& v)
        {
            seed ^= std::hash<T>{}(v) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
        }

        uint8_t ToByte(float v)
        {
            const float c = std::clamp(v, 0.0f, 1.0f);
            return static_cast<uint8_t>(c * 255.0f + 0.5f);
        }
    }

    std::size_t VoxelPaletteVersion(const BlockPalette& pal)
    {
        std::size_t s = 0;
        HashCombine(s, (uint32_t)pal.Count());
        HashCombine(s, pal.AtlasTilesX());
        HashCombine(s, pal.AtlasTilesY());
        for (const BlockType& b : pal.Blocks())
        {
            HashCombine(s, b.TileTop);
            HashCombine(s, b.TileSide);
            HashCombine(s, b.TileBottom);
            HashCombine(s, b.Color.r);
            HashCombine(s, b.Color.g);
            HashCombine(s, b.Color.b);
        }
        return s;
    }

    void BuildVoxelAtlas(VoxelRenderData& rd, const BlockPalette& pal)
    {
        const uint32_t tilesX = std::max<uint32_t>(1, pal.AtlasTilesX());
        const uint32_t tilesY = std::max<uint32_t>(1, pal.AtlasTilesY());
        constexpr uint32_t TILE = 16;
        const uint32_t W = tilesX * TILE;
        const uint32_t H = tilesY * TILE;

        // One colour per atlas tile: default mid-grey, overwritten by each block's
        // face tiles (top brighter, bottom darker — cheap directional shading).
        std::vector<glm::vec3> tileColor((size_t)tilesX * tilesY, glm::vec3(0.62f));
        for (uint16_t id = 1; id < pal.Count(); ++id)
        {
            const BlockType& b = pal.Get(id);
            auto put = [&](uint16_t tile, const glm::vec3& c)
            {
                if (tile < tileColor.size()) tileColor[tile] = c;
            };
            put(b.TileSide,   b.Color);
            put(b.TileTop,    glm::min(b.Color * 1.12f, glm::vec3(1.0f)));
            put(b.TileBottom, b.Color * 0.78f);
        }

        std::vector<uint8_t> px((size_t)W * H * 4, 255);
        for (uint32_t ty = 0; ty < tilesY; ++ty)
        {
            for (uint32_t tx = 0; tx < tilesX; ++tx)
            {
                const glm::vec3 c = tileColor[(size_t)ty * tilesX + tx];
                const uint8_t r = ToByte(c.r), g = ToByte(c.g), b = ToByte(c.b);
                for (uint32_t j = 0; j < TILE; ++j)
                {
                    for (uint32_t i = 0; i < TILE; ++i)
                    {
                        const uint32_t x = tx * TILE + i;
                        const uint32_t y = ty * TILE + j;
                        const size_t o = ((size_t)y * W + x) * 4;
                        px[o + 0] = r; px[o + 1] = g; px[o + 2] = b; px[o + 3] = 255;
                    }
                }
            }
        }

        Ref<Texture2D> tex = Texture2D::Create(W, H, /*mipmapped*/ false);
        tex->SetData(px.data(), (uint32_t)px.size());
        tex->SetSampling(TextureFilter::Nearest, TextureWrap::ClampToEdge);
        rd.AtlasTexture = tex;

        // A PBR material sampling the atlas (rough, non-metal). Mirrors
        // AssetLibrary::BuildMaterial but binds the runtime texture directly.
        Ref<Shader> pbr = AssetLibrary::GetShader("engine://shaders/PBR.glsl");
        if (pbr)
        {
            Ref<Material> m = Material::Create(pbr, "VoxelAtlas");
            m->Set("u_Albedo",    glm::vec4(1.0f));
            m->Set("u_Metallic",  0.0f);
            m->Set("u_Roughness", 0.95f);
            m->Set("u_AO",        1.0f);
            m->Set("u_Emissive",  glm::vec3(0.0f));
            m->Set("u_AlbedoMap",    tex);
            m->Set("u_HasAlbedoMap", 1.0f);
            m->Set("u_HasNormalMap",     0.0f);
            m->Set("u_HasMetalRoughMap", 0.0f);
            m->Set("u_HasAOMap",         0.0f);
            m->Set("u_HasEmissiveMap",   0.0f);
            rd.AtlasMaterial = m;
        }
    }

    VoxelGeneratorRecipe BuildVoxelRecipe(const VoxelVolumeComponent& c)
    {
        VoxelGeneratorRecipe r;
        r.Enabled       = c.GenEnabled;
        r.Seed          = c.Seed;
        r.SurfaceLevel  = c.SurfaceLevel;
        r.Amplitude     = c.Amplitude;
        r.Frequency     = c.Frequency;
        r.Octaves       = c.Octaves;
        r.Lacunarity    = c.Lacunarity;
        r.Gain          = c.Gain;
        r.Ridged        = c.Ridged;
        r.CaveThreshold = c.CaveThreshold;
        r.CaveFrequency = c.CaveFrequency;
        r.DirtDepth     = c.DirtDepth;
        r.SandLevel     = c.SandLevel;
        r.GrassBlock    = (uint16_t)c.GrassBlock;
        r.DirtBlock     = (uint16_t)c.DirtBlock;
        r.StoneBlock    = (uint16_t)c.StoneBlock;
        r.SandBlock     = (uint16_t)c.SandBlock;
        return r;
    }
}
