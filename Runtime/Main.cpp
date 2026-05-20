// Inside Runtime/Main.cpp
#include <Cosmic.h>

int main()
{
	auto app = new Cosmic::Application();
	app->Run();
	delete app;
	return 0;
}