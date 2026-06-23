#pragma once
// Renderer2D.h
// Last Modified: 5/28/2026
// 
// 
// **Needs Header documentation**

#include "core/Core.h"
#include "camera/OrthographicCamera.h"
#include "graphics/Texture.h"
#include "graphics/Material.h"
#include <glm/glm.hpp>
#include <string>

namespace Cosmic
{
    class SubTexture2D;
    class Font;

    class COSMIC_API Renderer2D
    {
    public:
        /////////////////////////////////////////////////////////////////////////////////
        // Lifecycle
        /////////////////////////////////////////////////////////////////////////////////

        static void Init();
        static void Shutdown();

        /////////////////////////////////////////////////////////////////////////////////
        // Scene / Pass Control
        /////////////////////////////////////////////////////////////////////////////////

        static void BeginScene(const OrthographicCamera& camera);
        static void EndScene();

        static void PushRenderPass(const glm::mat4& viewProj,
            const glm::vec4& viewportBounds);
        static void PopRenderPass();

        /////////////////////////////////////////////////////////////////////////////////
        // Flush
        /////////////////////////////////////////////////////////////////////////////////

        static void Flush();

        /////////////////////////////////////////////////////////////////////////////////
        // Viewport
        /////////////////////////////////////////////////////////////////////////////////

        static void SetViewportSize(uint32_t width, uint32_t height);

        /////////////////////////////////////////////////////////////////////////////////
        // SubTexture2D Drawing
        /////////////////////////////////////////////////////////////////////////////////

        static void DrawQuad(const glm::vec2& position, const glm::vec2& size,
            const Ref<SubTexture2D>& subTexture,
            const glm::vec4& tintColor = glm::vec4(1.0f));
        static void DrawQuad(const glm::vec3& position, const glm::vec2& size,
            const Ref<SubTexture2D>& subTexture,
            const glm::vec4& tintColor = glm::vec4(1.0f));
        static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size,
            float rotation,
            const Ref<SubTexture2D>& subTexture,
            const glm::vec4& tintColor = glm::vec4(1.0f));
        static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size,
            float rotation,
            const Ref<SubTexture2D>& subTexture,
            const glm::vec4& tintColor = glm::vec4(1.0f));

        /////////////////////////////////////////////////////////////////////////////////
        // Material-Based Drawing
        /////////////////////////////////////////////////////////////////////////////////

        static void DrawQuad(const glm::vec2& position, const glm::vec2& size,
            const Ref<Material>& material);
        static void DrawQuad(const glm::vec3& position, const glm::vec2& size,
            const Ref<Material>& material);
        static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size,
            float rotation, const Ref<Material>& material);

        /////////////////////////////////////////////////////////////////////////////////
        // Primitive Drawing — Color & Texture
        /////////////////////////////////////////////////////////////////////////////////

        static void DrawQuad(const glm::vec2& position, const glm::vec2& size,
            const glm::vec4& color);
        static void DrawQuad(const glm::vec3& position, const glm::vec2& size,
            const glm::vec4& color);
        static void DrawQuad(const glm::vec2& position, const glm::vec2& size,
            const Ref<Texture>& texture,
            float tilingFactor = 1.0f,
            const glm::vec4& tintColor = glm::vec4(1.0f));
        static void DrawQuad(const glm::vec3& position, const glm::vec2& size,
            const Ref<Texture>& texture,
            float tilingFactor = 1.0f,
            const glm::vec4& tintColor = glm::vec4(1.0f));

        /////////////////////////////////////////////////////////////////////////////////
        // Rotated Quads — Color & Texture
        /////////////////////////////////////////////////////////////////////////////////

        static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size,
            float rotation, const glm::vec4& color);
        static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size,
            float rotation, const glm::vec4& color);
        static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size,
            float rotation, const Ref<Texture>& texture,
            float tilingFactor = 1.0f,
            const glm::vec4& tintColor = glm::vec4(1.0f));
        static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size,
            float rotation, const Ref<Texture>& texture,
            float tilingFactor = 1.0f,
            const glm::vec4& tintColor = glm::vec4(1.0f));

        /////////////////////////////////////////////////////////////////////////////////
        // Specialized Math Primitives (SDF)
        /////////////////////////////////////////////////////////////////////////////////

        static void DrawCircle(
            const glm::vec3& position,
            const glm::vec2& size,
            const glm::vec4& color,
            float thickness,
            float fade,
            Cosmic::Ref<Cosmic::Shader> customShader = nullptr);

        inline static void DrawCircle(
            const glm::vec2& position,
            const glm::vec2& size,
            const glm::vec4& color,
            float thickness = 1.0f,
            float fade = 0.005f,
            Cosmic::Ref<Cosmic::Shader> customShader = nullptr)
        {
            DrawCircle({ position.x, position.y, 0.0f }, size, color,
                thickness, fade, customShader);
        }

        /////////////////////////////////////////////////////////////////////////////////
        // Text (world-space, SDF) — see Cosmic::Font
        /////////////////////////////////////////////////////////////////////////////////

        // Draw `text` with `font` under an arbitrary transform. Glyph metrics are in
        // em units (1 em = the transform's unit scale), so scale the transform to set
        // the world height. The baseline of the first line sits at the transform origin.
        static void DrawString(const std::string& text, const Ref<Font>& font,
            const glm::mat4& transform, const glm::vec4& color = glm::vec4(1.0f),
            float kerning = 0.0f, float lineSpacing = 0.0f);

        // Convenience: draw at a 2D position with a world-space size (height), no rotation.
        static void DrawString(const std::string& text, const Ref<Font>& font,
            const glm::vec2& position, float size, const glm::vec4& color = glm::vec4(1.0f),
            float kerning = 0.0f, float lineSpacing = 0.0f);

        /////////////////////////////////////////////////////////////////////////////////
        // Debug Geometry
        /////////////////////////////////////////////////////////////////////////////////

        static void DrawLine(const glm::vec3& p0, const glm::vec3& p1,
            const glm::vec4& color);
        static void DrawRect(const glm::vec3& position, const glm::vec2& size,
            const glm::vec4& color);

        /////////////////////////////////////////////////////////////////////////////////
        // Telemetry
        /////////////////////////////////////////////////////////////////////////////////

        struct Statistics
        {
            uint32_t DrawCalls = 0;
            uint32_t QuadCount = 0;
            uint32_t CircleCount = 0;
            uint32_t LineCount = 0;

            uint32_t GetTotalVertexCount() const { return QuadCount * 4 + CircleCount * 4 + LineCount * 2; }
            uint32_t GetTotalIndexCount()  const { return QuadCount * 6; }
        };

        static void       ResetStats();
        static Statistics GetStats();
        static void       SetStatsStatus(bool enabled);

        /////////////////////////////////////////////////////////////////////////////////
        // Render Pass State
        /////////////////////////////////////////////////////////////////////////////////

        struct RenderPassState
        {
            glm::mat4 ViewProjectionMatrix{ 1.0f };
            glm::vec4 ViewportBounds{ 0.0f, 0.0f, 1280.0f, 720.0f };
        };

        /////////////////////////////////////////////////////////////////////////////////
        // Instanced Circle Pipeline (existing)
        /////////////////////////////////////////////////////////////////////////////////

        struct InstanceCircleData
        {
            glm::vec3 Position;
            glm::vec2 Scale;
            glm::vec4 Color;
            float     Thickness;
            float     Fade;
        };

        static void DrawInstancedCircles(const InstanceCircleData* instances,
            uint32_t count,
            Ref<Shader> customShader = nullptr);

        /////////////////////////////////////////////////////////////////////////////////
        // NEW: Instanced Quad Pipeline
        /////////////////////////////////////////////////////////////////////////////////

        /**
         * @brief Per-instance data layout for hardware-instanced quad rendering.
         *
         * Matches the vertex attribute layout in QuadInstance.glsl:
         *   location 1  a_InstanceWorldPosition   (vec3)
         *   location 2  a_InstanceScale            (vec2)
         *   location 3  a_InstanceColor            (vec4)
         *   location 4  a_InstanceTexCoordOffset   (vec2)  — set to {0,0} for solid color
         *   location 5  a_InstanceTexCoordScale    (vec2)  — set to {1,1} for solid color
         *   location 6  a_InstanceTexIndex         (float) — 0 = white texture fallback
         *   location 7  a_InstanceTilingFactor     (float) — UV tiling multiplier
         *
         * Total size: 3+2+4+2+2+1+1 floats = 15 floats = 60 bytes per instance.
         *
         * Usage notes:
         *   - For flat-color quads:  set TexIndex = 0, TexCoordOffset = {0,0},
         *                            TexCoordScale = {1,1}, TilingFactor = 1
         *   - For textured quads:    bind the texture to u_Textures slot N,
         *                            set TexIndex = N
         *   - For sprite-sheet tiles: set TexCoordOffset/Scale to the normalised
         *                             UV rect of the tile within the atlas
         */
        struct InstanceQuadData
        {
            glm::vec3 Position;           // World-space centre of the quad
            glm::vec2 Scale;              // Full width and height in world units
            glm::vec4 Color;              // RGBA tint (multiplied with texture sample)
            glm::vec2 TexCoordOffset;     // Normalised UV origin  (atlas support)
            glm::vec2 TexCoordScale;      // Normalised UV extent  (atlas support)
            float     TexIndex;           // u_Textures[] slot index (0 = white)
            float     TilingFactor;       // UV tiling multiplier
        };

        static_assert(sizeof(InstanceQuadData) == 60,
            "InstanceQuadData must be exactly 60 bytes (15 floats) to match "
            "the QuadInstance.glsl attribute stride.");

        /**
         * @brief Draw quads using a single GPU instanced draw call.
         *
         * Flushes any pending batched geometry, streams all instance data to the
         * GPU in one SetData call, then issues a single DrawIndexedInstanced
         * through the RenderCommand abstraction.
         *
         * @param instances   Pointer to an array of InstanceQuadData structs.
         * @param count       Number of quads to draw.
         * @param customShader Optional override shader. Defaults to QuadInstance.glsl.
         *
         * Pre:   A render pass must be active (BeginScene / PushRenderPass called).
         *        `instances` must point to a valid array of at least `count` elements.
         * Post:  All `count` quads are submitted to the GPU as a single draw call.
         *        Pending batch state is reset afterwards so normal DrawQuad calls
         *        continue to work correctly.
         */
        static void DrawInstancedQuads(const InstanceQuadData* instances,
            uint32_t count,
            Ref<Shader> customShader = nullptr);

    private:
        static void FlushAndReset();
    };

} // namespace Cosmic