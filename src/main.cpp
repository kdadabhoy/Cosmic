#include "core/Application.h"
#include <iostream>
using std::cout;
using std::endl;


int main() {
    Application app;


    if (app.initialize()) {
        app.run();
    }
   
    return 0;
}