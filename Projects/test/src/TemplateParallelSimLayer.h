#pragma once

// TemplateParallelSimLayer.h
// Last Modified: 5/27/2026

/**
 * TemplateParallelSimLayer
 * ========================
 * Demonstrates the Approach B parallel physics pipeline:
 *   - Entities carry both BallComponent (visual) and PhysicsBody (simulation).
 *   - BallPhysicsSystem handles all integration via DoubleBuffer<PhysicsBody>.
 *   - This layer is responsible only for spawning, despawning, rendering, and
 *     exposing ImGui controls.  It does NOT touch physics state directly.
 *
 * Render loop reads TransformComponent.Position (synced by the merge pass)
 * and BallComponent.Color/Radius for DrawCircle calls.  This keeps the render
 * pass fully decoupled from simulation internals.
 */

#include <Cosmic.h>
#include "BallPhysicsSystem.h"
#include "Components.h"
#include <random>

namespace Workspace
{
    class TemplateParallelSimLayer : public Cosmic::Layer
    {
    public:
        explicit TemplateParallelSimLayer(Cosmic::Ref<Cosmic::Scene> scene);
        virtual ~TemplateParallelSimLayer() override = default;

        virtual void OnAttach()                          override;
        virtual void OnDetach()                          override;
        virtual void OnUpdate(float ts)                  override;
        virtual void OnFixedUpdate(float deltaFixedTime) override;
        virtual void OnImGuiRender()                     override;
        virtual void OnEvent(Cosmic::Event& e)           override;

    private:
        /**
         * @brief Spawn a ball entity with BallComponent + PhysicsBody.
         *
         * Both components are added atomically (within the same frame) so the
         * PhysicsSystem's Prepare pass always sees a consistent pair.
         *
         * @param position  Initial world-space XY position.
         * @param velocity  Initial XY velocity in world-units / second.
         * @param radius    Collision and rendering radius.
         * @param color     RGBA tint used by DrawCircle.
         */
        void SpawnBall(glm::vec2 position, glm::vec2 velocity,
            float radius = 0.22f,
            glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f });

        /**
         * @brief Destroy all ball entities and reset simulation counters.
         * Entities are collected first to avoid iterator invalidation.
         */
        void ClearBalls();

        bool OnWindowResize(Cosmic::WindowResizeEvent& e);

    private:
        // ------------------------------------------------------------------
        // Scene & camera
        // ------------------------------------------------------------------
        Cosmic::Ref<Cosmic::Scene>           m_Scene;
        Cosmic::OrthographicCameraController m_Camera;
        glm::vec2                            m_ViewportSize = { 1280.0f, 720.0f };

        // ------------------------------------------------------------------
        // Optional custom circle shader (loaded from VFS; may be nullptr)
        // ------------------------------------------------------------------
        Cosmic::Ref<Cosmic::Shader> m_SpecularCircleShader = nullptr;

        // ------------------------------------------------------------------
        // Pointer to the physics system registered in the scene.
        // Owned by the scene; this layer borrows the raw pointer for ImGui
        // access to simulation parameters (Gravity, Damping, Bounds).
        // ------------------------------------------------------------------
        BallPhysicsSystem* m_PhysicsSystem = nullptr;

        // ------------------------------------------------------------------
        // ImGui spawn controls
        // ------------------------------------------------------------------
        int m_SpawnCount = 8;

        // ------------------------------------------------------------------
        // Telemetry
        // ------------------------------------------------------------------
        uint32_t m_FixedTicks = 0;

        // ------------------------------------------------------------------
        // RNG for randomised spawn properties
        // ------------------------------------------------------------------
        std::mt19937 m_Rng{ std::random_device{}() };
    };

} // namespace Workspace
