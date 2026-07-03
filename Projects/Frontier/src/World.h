#pragma once

// World.h
//
// ============================================================================
// Frontier — the World framework (Phase 11, docs/plans/10-phase11-frontier-plan.md).
// ============================================================================
//
// A World is one self-contained explorable environment (the seamless island,
// the night volcano, the blizzard peak, ...). The FrontierApp root layer owns
// the registry, the homescreen, the shared camera and the dock layout; the
// active World owns its content and drives its own frame from OnUpdate.
//
// SKELETON STATUS: this interface ships ahead of the engine features it will
// consume. The Phase 11 work orders grow WorldContext instead of introducing
// singletons — when an F-item lands, append its pointer here and feed it from
// FrontierApp::OnUpdate:
//
//   TODO(F1): Cosmic::FlyCameraController* Camera — replaces OrbitFallback as
//             the exploration camera (WASD + mouse-look; ground-probe clamp).
//   TODO(F2): Cosmic::SceneRenderer* Renderer — worlds fill a SceneRenderDesc
//             and call Renderer->Render(desc) instead of hand-rolling passes.
//   TODO(F3): GPU-profiler results pointer for the HUD panel.
//   TODO(F10): ambience audio helper (DistanceLoop registry).
// ============================================================================

#include <Cosmic.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdint>

namespace Frontier
{
    /** Static identity + presentation of a world: homescreen tile text, spawn
     *  pose for the shared camera, and the placeholder-era clear color. */
    struct WorldInfo
    {
        const char* Name  = "World";
        const char* Icon  = "";        // Lucide glyph (ui/IconsLucide.h)
        const char* Blurb = "";
        glm::vec3   SpawnPosition{ 0.0f, 30.0f, 60.0f };
        float       SpawnYawDeg   = 0.0f;
        float       SpawnPitchDeg = -20.0f;
        glm::vec4   PlaceholderClear{ 0.06f, 0.08f, 0.12f, 1.0f };
    };

    /** Per-frame services the root layer hands the active world. Grown by the
     *  Phase 11 work orders (see the header banner). */
    struct WorldContext
    {
        float    DeltaTime     = 0.0f;
        float    TimeSeconds   = 0.0f;   // world-local clock (resets on enter)
        uint32_t ViewportWidth  = 0;
        uint32_t ViewportHeight = 0;

        // Placeholder-era exploration camera (F1 replaces this with the fly camera).
        Cosmic::OrbitCameraController* OrbitFallback = nullptr;
    };

    class World
    {
    public:
        virtual ~World() = default;

        virtual const WorldInfo& GetInfo() const = 0;

        /** Called once when the world becomes active (build content here —
         *  terrain/water/emitters are cheap to keep, so building in the ctor
         *  is also fine for small worlds). */
        virtual void OnAttach() {}
        virtual void OnDetach() {}

        /** Simulate + render one frame. PRE: the app viewport FBO is bound,
         *  viewport set, and cleared to PlaceholderClear. The world drives its
         *  own passes (skeleton: a Renderer3D scene; post-F2: SceneRenderer). */
        virtual void OnUpdate(WorldContext& ctx) = 0;

        /** World-specific ImGui panels (docked via FrontierApp's layout). */
        virtual void OnPanels(WorldContext& ctx) { (void)ctx; }

    protected:
        /** Shared skeleton visual so every stub world renders SOMETHING
         *  distinctive on day one: grid, axes, and a ring of wire boxes in the
         *  world's tint. The F12+ content work orders delete their world's
         *  call to this. */
        void DrawPlaceholder(WorldContext& ctx, const glm::vec4& tint)
        {
            if (!ctx.OrbitFallback)
                return;

            Cosmic::Renderer3D::BeginScene(ctx.OrbitFallback->GetCamera());
            Cosmic::Renderer3D::DrawGrid(60.0f, 5.0f, { 0.30f, 0.32f, 0.36f, 1.0f });
            Cosmic::Renderer3D::DrawAxes(glm::mat4(1.0f), 4.0f);

            for (int i = 0; i < 8; ++i)
            {
                const float a = (float)i / 8.0f * 6.28318f + ctx.TimeSeconds * 0.15f;
                glm::mat4 xf  = glm::translate(glm::mat4(1.0f),
                                               { std::cos(a) * 22.0f,
                                                 2.0f + std::sin(ctx.TimeSeconds + (float)i) * 1.5f,
                                                 std::sin(a) * 22.0f });
                xf = glm::scale(xf, glm::vec3(2.5f));
                Cosmic::Renderer3D::DrawWireBox(xf, tint);
            }

            Cosmic::Renderer3D::EndScene();
        }
    };

} // namespace Frontier
