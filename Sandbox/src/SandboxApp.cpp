#include "Cosmic.h"
#include "SandboxLayer.h"

class AirplaneSim : public Cosmic::Application
{
public:
	AirplaneSim()
	{
		PushLayer(new Cosmic::SandboxLayer());
	}
	~AirplaneSim() {}
};

int main()
{
	auto app = new AirplaneSim();
	app->Run();
	delete app;
	return 0;
}