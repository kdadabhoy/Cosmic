#include <core/Application.h>  // Use the path relative to the 'include' folder

#include <iostream>


// 1. Define your specific Game class
// Note: We use Cosmic::Application because you added the Cosmic namespace to the engine
class AirplaneSim : public Cosmic::Application {
public:
    AirplaneSim() {
        // PushLayer is a member of the Application class
        // PushLayer(new MenuLayer());
    }

    ~AirplaneSim() {}
};



// 2. The actual entry point that starts the program
int main() {
    auto app = new AirplaneSim();

    if (app->initialize()) {
        app->run();
    }

    delete app;
    return 0;
}