// CosmicTests entry point.
// Initializes the engine logger (telemetry code logs through it — the macros
// dereference the logger, so it must exist) then hands control to doctest.
// No window, no GL context, no Application instance.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include "core/Log.h"

int main(int argc, char** argv)
{
    Cosmic::Log::Init("logs");

    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
