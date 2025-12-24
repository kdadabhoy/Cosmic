#include "Cosmic.h"
#include "SandboxLayer.h"

class AirplaneSim : public Cosmic::Application
{
public:
	AirplaneSim()
	{
		PushLayer(new SandboxLayer());
	}

	~AirplaneSim() {}
};

// If you are using a separate EntryPoint.h, you just return the new app.
// If you are keeping main() in this file, use this:
int main()
{
	auto app = new AirplaneSim();
	app->Run();
	delete app;
	return 0;
}