/**
 * @file Simulation.h
 * @brief The abstract interface for all engineering projects.
 *
 * PURPOSE: In a Host/Client architecture, the Simulation class is the "Client."
 * By inheriting from this class, any project (aerodynamics, structural, etc.)
 * becomes compatible with the WorkspaceLayer. This allows the engine to
 * swap complex logic modules at runtime without needing to recompile the
 * entire application shell.
 */




 // Need to add an void OnFixedUpdate(float deltaFixedTime) {};


#pragma once
#include "Cosmic.h"

namespace Workspace
{

    class Simulation
    {
    public:
        virtual ~Simulation() = default;

        // Called every frame for physics and logic calculations
        virtual void OnUpdate(float ts) = 0;

        // Called for 2D/3D rendering commands
        virtual void OnRender() = 0;

        // Called to draw project-specific ImGui debugging/control panels
        virtual void OnImGuiRender() = 0;

        // Synchronizes the simulation's camera projection with the UI viewport size
        virtual void SetViewportSize(float width, float height) = 0;


        virtual void OnEvent(Cosmic::Event& e) {};

    };

}