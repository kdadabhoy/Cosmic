#pragma once

// Layer.h
// Last Modified 5/24/2026

/**
 * General Description:
 * The Layer class serves as the primary polymorphic base for all engine components.
 * It acts as a self-contained "slice" of the application, representing a distinct
 * module such as a game world, a UI overlay, or a physics simulation.
 *
 * Layers are designed to be managed by the LayerStack, which orchestrates their
 * lifecycle and ensures they are updated and rendered in a prioritized sequence.
 * This architecture allows for a clean separation of concerns, where different
 * systems (e.g., rendering vs. UI) can coexist without being tightly coupled.
 *
 * Each layer manages its own internal scalable timeline clock. This layout decouples
 * client simulation/visual speed from the global engine runtime, enabling per-layer
 * features like independent slow-motion effects, timeline scrubbing, or pausing.
 *
 * Public Function Prototypes (Pre and Post Conditions):
 *
 * 1. Layer(const std::string& name)
 *    Post: The layer is initialized with a debug name for identification, an accumulated
 *          local time of 0.0f, and a baseline time scale value of 1.0f.
 *
 * 2. OnAttach()
 *    Post: Called when the layer is pushed to the LayerStack; used for client-side resource
 *          initialization (e.g., texture creation, shader allocations).
 *
 * 3. OnDetach()
 *    Post: Called when the layer is removed from the LayerStack; used for client resource cleanup.
 *
 * 4. OnUpdate(float deltaTime)
 *    Pre:  deltaTime contains the scaled variable time elapsed since the last engine frame iteration.
 *    Post: Executes frame-dependent logic based on variable delta-time.
 *
 * 5. OnFixedUpdate(float deltaFixedTime)
 *    Pre:  deltaFixedTime is a deterministic, constant interval chunk (typically 1/60s).
 *    Post: Executes simulation-critical, frame-rate independent updates (e.g., physics, networking).
 *
 * 6. OnRender()
 *    Post: Dispatches traditional world-space rasterization draw commands (sprites, geometry).
 *
 * 7. OnImGuiRender()
 *    Post: Dispatches modern UI context panel commands directly to the ImGui render pipeline.
 *
 * 8. OnEvent(Event& event)
 *    Pre:  An active hardware or internal application signal has been dispatched.
 *    Post: Processes the signal context; may flag the event as "handled" to halt down-stack propagation.
 *
 * 9. GetName()
 *    Post: Returns a reference to the unmodifiable debug string tag.
 *
 * 10. UpdateLayerTime(float deltaTime)
 *    Pre:  The master engine frame timestep has been provided in seconds.
 *    Post: Accumulates scaled time into the internal layer clock using the formula:
 *          m_LocalTime += deltaTime * m_LocalTimeScale.
 *
 * 11. GetLocalTime() / SetLocalTime(float time)
 *    Post: Retrieves the current local time accumulator value or directly overwrites it
 *          (useful for syncing with editor timeline scrubbers or level resets).
 *
 * 12. GetTimeScale() / SetTimeScale(float scale)
 *    Post: Handles timeline rate multiplication changes (0.0f = Paused, 0.5f = Slow-Mo, 1.0f = Normal).
 */

#include "core/Core.h"
#include "events/Event.h"
#include <string>
#include <sstream>
#include <iostream>

namespace Cosmic
{
    class COSMIC_API Layer
    {
    public:
        // /////////////////////////////////////////////////////////////////////////////
        // Construction & Lifecycle
        // /////////////////////////////////////////////////////////////////////////////

        Layer(const std::string& name = "Layer")
            : m_DebugName(name), m_LocalTime(0.0f), m_LocalTimeScale(1.0f)
        {
        }

        virtual ~Layer() = default;

        virtual void OnAttach() {};
        virtual void OnDetach() {};

        // /////////////////////////////////////////////////////////////////////////////
        // Logic & Execution Pass Hooks
        // /////////////////////////////////////////////////////////////////////////////

        virtual void OnUpdate(float deltaTime) {};
        virtual void OnFixedUpdate(float deltaFixedTime) {};

        // /////////////////////////////////////////////////////////////////////////////
        // Rasterization & Rendering passes
        // /////////////////////////////////////////////////////////////////////////////

        virtual void OnRender() {};
        virtual void OnImGuiRender() {};

        // /////////////////////////////////////////////////////////////////////////////
        // Event Handling & Core Utilities
        // /////////////////////////////////////////////////////////////////////////////

        virtual void OnEvent(Event& event) {};

        inline const std::string& GetName() const { return m_DebugName; };

        // /////////////////////////////////////////////////////////////////////////////
        // Local Context Timeline Time API
        // /////////////////////////////////////////////////////////////////////////////

        inline void UpdateLayerTime(float deltaTime) { m_LocalTime += deltaTime * m_LocalTimeScale; }

        inline float GetLocalTime() const { return m_LocalTime; }
        inline void SetLocalTime(float time) { m_LocalTime = time; }

        inline float GetTimeScale() const { return m_LocalTimeScale; }
        inline void SetTimeScale(float scale) { m_LocalTimeScale = scale; }

    protected:
        std::string m_DebugName;
        float m_LocalTime;
        float m_LocalTimeScale;
    };
}