# viper-fc — portable Viper flight computer

The flight code that will fly on the Teensy 4.1, developed and regression-tested inside
[ViperSim](../) (plan: [`docs/plans/archive/04-viper-sim-plan.md`](../../../docs/plans/archive/04-viper-sim-plan.md)).

- **Header-only C++17**, no `Arduino.h`, no engine, no STL containers in flight code, no heap
  after init. The same files compile in the ViperSim DLL (MSVC), the doctest unit tests, and the
  PlatformIO Teensy build.
- **`include/viperfc/`** — estimator (complementary), mode machine
  (HOVER/TRANSITION/CRUISE/ORBIT/RTL/FAILSAFE), cascaded controllers, tailsitter mixers, failsafe
  supervisor with hover-budget energy accounting, telemetry schema, HIL wire protocol.
- **`tests/`** — plain doctest (`ViperFcTests` target, built with the engine's `COSMIC_BUILD_TESTS`):
  quaternion integration, mixer saturation, transition table, failsafe thresholds, estimator
  convergence.
- **`firmware/`** — PlatformIO project for the Teensy 4.1 HIL node (P6): receives simulated
  sensors over USB-CDC (COBS+CRC16, the engine's freestanding `serial/Framing.h`), runs the same
  `FlightComputer`, answers with actuator frames echoing the sim timestamp for on-screen latency.

Consumed by `Projects/ViperSim/CMakeLists.txt` via `add_subdirectory` (target `viperfc`).
