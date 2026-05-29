#pragma once

// TemplateRenderBenchmarkLayer.h
// Last Modified: 5/28/2026

/**
 * ============================================================================
 * TemplateRenderBenchmarkLayer
 * ============================================================================
 *
 * PURPOSE
 * -------
 * A showcase / diagnostic layer that lets you toggle four independent axes
 * at runtime from an ImGui panel:
 *
 *   1. Render Mode      — Batched (DrawQuad per entity) vs
 *                         Instanced (DrawInstancedQuads, single GPU call)
 *   2. Physics Mode     — Single-threaded (main thread loop) vs
 *                         Multi-threaded (JobSystem + ParallelFor)
 *   3. Render Shape     — Quads or Circles (so you can benchmark both
 *                         instanced pipelines independently)
 *   4. Entity Count     — Spawn slider from 1 to MaxEntities
 *
 * All four axes are completely independent — you can mix and match any
 * combination to isolate exactly which subsystem is the bottleneck.
 *
 * ARCHITECTURE
 * ------------
 * Physics follows the engine's Approach-B pattern already established in
 * TemplateParallelSimLayer:
 *
 *   PhysicsBody   — plain-old-data simulation state (position, velocity,
 *                   radius, mass, restitution, drag). Trivially copyable
 *                   so DoubleBuffer<PhysicsBody> uses memcpy.
 *
 *   BallComponent — visual-only data (color, radius for rendering).
 *                   Never touched by workers.
 *
 * In multi-threaded mode the layer manually drives the Prepare → ParallelFor
 * → WaitIdle → Merge pipeline inside OnFixedUpdate, matching exactly how
 * BallPhysicsSystem does it but without requiring a full ParallelSystem
 * registration so the layer remains self-contained for easy copy-paste.
 *
 * In single-threaded mode the same integration math runs on the main thread
 * in a plain for-loop — making the comparison apples-to-apples.
 *
 * RENDER PATH DETAILS
 * -------------------
 * Batched  : calls Renderer2D::DrawQuad / DrawCircle once per entity.
 *            Each draw call appends four vertices to the batch buffer.
 *            The GPU sees (entityCount / MaxQuadsPerBatch) draw calls.
 *
 * Instanced: packs all entities into a std::vector<InstanceQuadData> or
 *            InstanceCircleData and calls DrawInstancedQuads /
 *            DrawInstancedCircles once. The GPU sees a single
 *            glDrawElementsInstanced call (or one per 20 000 entities
 *            if you exceed the hardware ceiling).
 *
 * USAGE
 * -----
 * From your plugin entry point or a composite layer:
 *
 *   auto scene = Cosmic::CreateRef<Cosmic::Scene>();
 *   layer->PushInternalLayer(new TemplateRenderBenchmarkLayer(scene));
 *
 * or register it directly:
 *
 *   Cosmic::Application::Get().PushLayer(new TemplateRenderBenchmarkLayer(scene));
 *
 * ============================================================================
 */

#include <Cosmic.h>
#include "jobs/DoubleBuffer.h"
#include "jobs/ParallelFor.h"

#include <glm/glm.hpp>
#include <random>
#include <vector>
#include <chrono>
#include <atomic>
#include <mutex>

namespace Workspace
{
    // =========================================================================
    // BenchmarkBallComponent  — visual / identity data (render thread only)
    // =========================================================================
    struct BenchmarkBallComponent
    {
        float     Radius = 0.2f;
        glm::vec4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
    };

    // =========================================================================
    // BenchmarkPhysicsBody  — simulation state (parallel workers only)
    //
    // 32-byte layout, trivially copyable, SIMD-friendly.
    // Mirrors Components.h::PhysicsBody but is self-contained so this layer
    // compiles without pulling in the full template project headers.
    // =========================================================================
    struct BenchmarkPhysicsBody
    {
        glm::vec2 Position = { 0.0f, 0.0f };  //  8 bytes
        glm::vec2 Velocity = { 0.0f, 0.0f };  //  8 bytes
        float     Radius = 0.2f;             //  4 bytes
        float     Mass = 1.0f;             //  4 bytes
        float     Restitution = 0.85f;            //  4 bytes
        float     LinearDrag = 1.0f;             //  4 bytes
        //                                           --------
        //                                           32 bytes total
    };

    static_assert(sizeof(BenchmarkPhysicsBody) == 32,
        "BenchmarkPhysicsBody must be exactly 32 bytes for DoubleBuffer memcpy safety.");

    // =========================================================================
    // RenderMode / PhysicsMode enums
    // =========================================================================
    enum class RenderMode : int { Batched = 0, Instanced = 1 };
    enum class PhysicsMode : int { SingleThreaded = 0, MultiThreaded = 1 };
    enum class ShapeMode : int { Quads = 0, Circles = 1 };

    // =========================================================================
    // TemplateRenderBenchmarkLayer
    // =========================================================================
    class TemplateRenderBenchmarkLayer : public Cosmic::Layer
    {
    public:
        explicit TemplateRenderBenchmarkLayer(Cosmic::Ref<Cosmic::Scene> scene);
        virtual ~TemplateRenderBenchmarkLayer() override = default;

        // ---------------------------------------------------------------------
        // Layer interface
        // ---------------------------------------------------------------------
        virtual void OnAttach()                          override;
        virtual void OnDetach()                          override;
        virtual void OnUpdate(float ts)                  override;
        virtual void OnFixedUpdate(float deltaFixedTime) override;
        virtual void OnImGuiRender()                     override;
        virtual void OnEvent(Cosmic::Event& e)           override;

    private:
        // ---------------------------------------------------------------------
        // Spawn / clear helpers
        // ---------------------------------------------------------------------
        void SpawnBall(glm::vec2 position, glm::vec2 velocity,
            float radius, glm::vec4 color);
        void SpawnBatch(int count);
        void ClearBalls();
        void RebuildToCount(int targetCount);

        // ---------------------------------------------------------------------
        // Physics integration (called from OnFixedUpdate)
        // ---------------------------------------------------------------------

        /// Integrates all bodies on the main thread (SingleThreaded mode).
        void PhysicsSingleThreaded(float dt);

        /// Integrates all bodies via the JobSystem (MultiThreaded mode).
        /// Follows the Prepare → ParallelFor → WaitIdle → Merge pipeline.
        void PhysicsMultiThreaded(float dt);

        /// Core integration kernel — called by both paths, same math.
        /// Reads from `src`, writes to `dst`.
        static void IntegrateBody(const BenchmarkPhysicsBody& src,
            BenchmarkPhysicsBody& dst,
            float dt,
            float gravity,
            float damping,
            float boundsX,
            float boundsY);

        // ---------------------------------------------------------------------
        // Render helpers
        // ---------------------------------------------------------------------
        void RenderArena();
        void RenderBatched();
        void RenderInstanced();

        // ---------------------------------------------------------------------
        // Event handlers
        // ---------------------------------------------------------------------
        bool OnWindowResize(Cosmic::WindowResizeEvent& e);

        // ---------------------------------------------------------------------
        // ImGui sub-panels
        // ---------------------------------------------------------------------
        void ImGuiModePanel();
        void ImGuiSpawnPanel();
        void ImGuiPhysicsPanel();
        void ImGuiPerfPanel();

        // ---------------------------------------------------------------------
        // Timing helpers
        // ---------------------------------------------------------------------
        using Clock = std::chrono::high_resolution_clock;
        using TimePoint = std::chrono::time_point<Clock>;
        static float ElapsedMs(TimePoint start, TimePoint end)
        {
            return std::chrono::duration<float, std::milli>(end - start).count();
        }

    private:
        // --- Scene & camera ---
        Cosmic::Ref<Cosmic::Scene>                   m_Scene;
        Cosmic::OrthographicCameraController         m_Camera;
        glm::vec2                                    m_ViewportSize = { 1280.0f, 720.0f };

        // --- Mode toggles ---
        RenderMode  m_RenderMode = RenderMode::Batched;
        PhysicsMode m_PhysicsMode = PhysicsMode::SingleThreaded;
        ShapeMode   m_ShapeMode = ShapeMode::Circles;

        // --- Simulation parameters (exposed to ImGui) ---
        float m_Gravity = -9.8f;
        float m_Damping = 0.15f;
        float m_BoundsX = 7.0f;
        float m_BoundsY = 4.5f;
        int   m_SpawnCount = 50;
        int   m_TargetCount = 0;   ///< live target; RebuildToCount enforces it

        static constexpr int k_MaxEntities = 20000;
        static constexpr int k_MinEntities = 1;

        // --- Entity tracking ---
        // We store entity handles in insertion order so RebuildToCount can
        // destroy the tail without touching the whole list.
        std::vector<Cosmic::Entity> m_Balls;

        // --- Physics double-buffer ---
        // Read buffer: authoritative state at the start of this fixed tick.
        // Write buffer: results being computed by workers.
        // Entity slots maps buffer index → entt entity for write-back.
        Cosmic::DoubleBuffer<BenchmarkPhysicsBody> m_PhysicsBuffer;
        std::vector<entt::entity>                  m_EntitySlots;

        // --- Counters ---
        uint32_t m_FixedTicks = 0;

        // --- Perf telemetry (ms) ---
        float m_LastPhysicsMs = 0.0f;  ///< Total fixed-update wall time
        float m_LastRenderMs = 0.0f;  ///< Total OnUpdate render wall time
        float m_PrepareMs = 0.0f;  ///< Parallel-mode prepare phase
        float m_ExecuteMs = 0.0f;  ///< Parallel-mode execute phase
        float m_MergeMs = 0.0f;  ///< Parallel-mode merge phase
        float m_FrameTimeMs = 0.0f;  ///< Rolling wall-time per frame
        TimePoint m_LastFrameTime = Clock::now();

        // --- RNG ---
        std::mt19937 m_Rng{ std::random_device{}() };
    };

} // namespace Workspace

// =============================================================================
// DLL-safe component registration
// These must appear exactly once per compilation unit that uses the types.
// =============================================================================
CS_REGISTER_COMPONENT(Workspace::BenchmarkBallComponent)
CS_REGISTER_COMPONENT(Workspace::BenchmarkPhysicsBody)