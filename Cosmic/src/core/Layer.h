#pragma once

#include "events/Event.h"

#include <string>
#include <sstream>
#include <iostream>


namespace Cosmic
{
    class Layer 
    {
    public:
        Layer(const std::string& name = "Layer")
            : m_DebugName(name) 
        {

        }


        virtual ~Layer() = default;

        // Called when the layer is added to the stack (setup)
        virtual void OnAttach() {};


        // Called when the layer is removed (cleanup)
        virtual void OnDetach() {};


        // Logic updates (movement, physics, aircraft climbing)
        virtual void OnUpdate(float deltaTime) {};


        // Rendering calls
        virtual void OnRender() {};


        // For ImGUI
        virtual void OnImGuiRender() {};
        inline const std::string& GetName() const { return m_DebugName; };


        // --- Event Handling ---
        // Every layer can now override this to "catch" window resizes or key presses
        virtual void OnEvent(Event& event) {};


    protected:
        std::string m_DebugName;
    };

}



// Think of a layer as one part of a screen... one layer could be
// a background. The next layer could be an airplane... etc
// Layers are used primarily in order to be able to have a draw order

// We will use polymorphisms and derive many layers from the abstract Layer.h