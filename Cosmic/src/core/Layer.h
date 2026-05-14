#pragma once

// Layer.h
// Last Modified 5/14/2026

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
 * Public Function Prototypes (Pre and Post Conditions):
 *
 * 1. Layer(const std::string& name)
 *    Post: The layer is initialized with a debug name for identification.
 *
 * 2. OnAttach()
 *    Post: Called when the layer is pushed to the LayerStack; used for resource initialization.
 *
 * 3. OnDetach()
 *    Post: Called when the layer is removed from the LayerStack; used for resource cleanup.
 *
 * 4. OnUpdate(float deltaTime)
 *    Post: Executes frame-dependent logic based on variable delta-time.
 *
 * 5. OnFixedUpdate(float deltaFixedTime)
 *    Post: Executes simulation-critical logic based on a constant time step.
 *
 * 6. OnRender() / OnImGuiRender()
 *    Post: Dispatches draw calls to the Graphics API or the ImGui subsystem respectively.
 *
 * 7. OnEvent(Event& event)
 *    Post: Processes hardware or application signals; may "handle" the event to stop propagation.
 */

#include "events/Event.h"
#include <string>
#include <sstream>
#include <iostream>

namespace Cosmic
{
    class Layer
    {
    public:
        ////////////////////////////////
        // Construction & Lifecycle
        ///////////////////////////////

        Layer(const std::string& name = "Layer")
            : m_DebugName(name)
        {
        }

        virtual ~Layer() = default;



        /**
         * OnAttach
         * Triggered when the layer is added to the active LayerStack.
         * Override this to perform setup tasks like creating textures,
         * loading shaders, or initializing buffers.
         */
        virtual void OnAttach() {};


        /**
         * OnDetach
         * Triggered when the layer is removed from the LayerStack.
         * Override this to clean up any resources allocated in OnAttach.
         */
        virtual void OnDetach() {};




        ////////////////////////////////
        // Logic & Execution
        ///////////////////////////////


        /**
         * OnUpdate
         * The primary heartbeat for game logic. This is called once per frame.
         * @param deltaTime: Time in seconds elapsed since the last frame.
         */
        virtual void OnUpdate(float deltaTime) {};


        /**
         * OnFixedUpdate
         * Used for simulation logic (like physics) that requires a consistent
         * time interval across all hardware, regardless of frame-rate.
         * @param deltaFixedTime: The fixed constant time interval.
         */
        virtual void OnFixedUpdate(float deltaFixedTime) {};




        ////////////////////////////////
        // Rendering
        ///////////////////////////////


        /**
         * OnRender
         * Called during the standard render pass. This is where world-space
         * objects (sprites, quads, lines) should be drawn.
         */
        virtual void OnRender() {};


        /**
         * OnImGuiRender
         * A specialized render pass for the ImGui UI system. This ensures UI
         * code is separated from core gameplay rendering.
         */
        virtual void OnImGuiRender() {};




        ////////////////////////////////
        // Event Handling & Utilities
        ///////////////////////////////

        /**
         * OnEvent
         * Entry point for the engine's event system. Allows the layer to respond
         * to window, keyboard, and mouse signals.
         * @param event: The dispatched event object to be processed.
         */
        virtual void OnEvent(Event& event) {};


        /**
         * GetName
         * Returns the debug name assigned during construction. Useful for
         * profiling and logging within the LayerStack.
         */
        inline const std::string& GetName() const       { return m_DebugName; };



    protected:
        std::string m_DebugName; // Unique identifier for the layer instance
    };
}