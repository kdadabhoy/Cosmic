// Main.cpp
// Last Modified 5/14/2026

/**
 * General Description:
 * 
 * This file serves as the "Ignition Switch" for the entire Cosmic Engineering Suite.
 * Its sole purpose is to bootstrap the engine and launch the Workspace (The Editor).
 * It defines the SimulationHost, which is the high-level container that owns the
 * operating system window, the graphics context, and the main execution loop.
 * 
 * Why does this file exist?
 * In a professional engine architecture, we separate the "Engine" from the "Project."
 * This file acts as the bridge. By pushing the WorkspaceLayer here, we tell the
 * engine: "Start up, but instead of running a game, run the Engineering Editor."
 * 
 * Note for Beginners:
 * 
 * You generally do not need to modify this file. If you want to create a new
 * simulation, stress test, or project, you should head over to the 'projects/'
 * directory. This file is just the "Shell" that holds those projects.
 * 
 * Public Function Prototypes (Pre and Post Conditions):
 * 
 * 1. int main()
 * Pre:  The environment is set up to support OpenGL 4.1+.
 * Post: The engine enters a blocking loop. It will only return 0 once the
 * user closes the application window.
 */

#include "Cosmic.h"
#include "WorkspaceLayer.h"

 /**
  * SimulationHost
  * * The "Manager of Managers."
  * * It inherits from Cosmic::Application, giving it the power to handle
  * window events, timing, and layers.
  */
class SimulationHost : public Cosmic::Application
{
public:
	SimulationHost()
	{
		// We push the WorkspaceLayer as the primary interface.
		// Think of this like inserting the 'Editor' cartridge into the console.
		PushLayer(new Workspace::WorkspaceLayer());
	}

	~SimulationHost() {}
};

/**
 * Main
 * * Entry point for the executable.
 * * It creates the app, calls Run() to start the "Heartbeat," and cleans up
 * memory once the app is closed.
 */
int main()
{
	auto app = new SimulationHost();

	// Run() starts the internal engine loop that calls OnUpdate and OnRender
	app->Run();

	delete app;
	return 0;
}