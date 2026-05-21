#include <Cosmic.h>

// Basic Bootloader / Entry Point

int main()
{
	auto app = new Cosmic::Application();
	app->Run();
	delete app;
	return 0;
}