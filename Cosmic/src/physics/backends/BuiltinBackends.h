#pragma once
// physics/backends/BuiltinBackends.h
//
// Engine-private registration hooks for the backends that ship in the DLL. Each
// backend TU keeps its implementation class file-local (anonymous namespace) and
// exposes only the one function below, which PhysicsBackend.cpp calls from
// RegisterBuiltinPhysicsBackends().
//
// Not a public header: COSMIC_WITH_JOLT is a PRIVATE define on the Cosmic target,
// so only engine translation units may include this.

namespace Cosmic
{
    /** @brief NullBackend.cpp — always built, so COSMIC_WITH_JOLT=OFF is a valid
     *  configuration and there is always something for Create() to return. */
    void RegisterNullPhysicsBackend();

#ifdef COSMIC_WITH_JOLT
    /** @brief JoltBackend.cpp — the engine's real simulation. */
    void RegisterJoltPhysicsBackend();
#endif
}
