/**
 * @file Main.cpp
 * @brief Entry point for the Engineering Simulation Workspace.
 *
 * ROLE: This file is responsible for initializing the Cosmic Engine and
 * pushing the WorkspaceLayer onto the stack. It represents the "Release"
 * executable. For most engineering work, this file remains untouched;
 * development happens inside the 'projects/' folder.
 */

#include "Cosmic.h"
#include "WorkspaceLayer.h"

 // The Application class specialized for our Engineering Workspace
class SimulationHost : public Cosmic::Application
{
public:
    SimulationHost()
    {
        // We push the WorkspaceLayer as the primary interface
        PushLayer(new Workspace::WorkspaceLayer());
    }

    ~SimulationHost() {}
};

/**
 * The main loop. In your specific architecture, Application::Run()
 * is a blocking call that executes the heartbeat of the engine.
 */
int main()
{
    auto app = new SimulationHost();
    app->Run();
    delete app;
    return 0;
}