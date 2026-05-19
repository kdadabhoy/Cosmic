// Main.cpp
// Last Modified: 5/19/2026

/**
 * General Description:
 * This file serves as the project-independent "Ignition Switch" for the Cosmic
 * Engineering Suite. It bootstraps the generic engine Application runtime host.
 */

#include "Cosmic.h"

 /**
  * Main
  * * Entry point for the executable.
  * * Instantiates the standard engine core application, executes its blocking
  * main heartbeat loop, and safely frees resources upon exiting.
  */
int main()
{
	// Instantiate the core engine application runtime
	auto app = new Cosmic::Application();

	// Run blocks execution, running the internal engine lifecycle loop
	app->Run();

	// Safely delete the application instance once the window is closed
	delete app;
	return 0;
}