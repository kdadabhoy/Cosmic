#pragma once

// viperfc/ViperFc.h — umbrella header for consumers (SimHal, TeensyHal, tests).
// The library is header-only C++17: no Arduino.h, no engine, no heap after
// init — the same files compile in the ViperSim DLL, the doctest runner, and
// the PlatformIO Teensy 4.1 project (doc 04 §1).

#include "viperfc/Math.h"
#include "viperfc/IHal.h"
#include "viperfc/Params.h"
#include "viperfc/Pid.h"
#include "viperfc/Estimator.h"
#include "viperfc/Mixer.h"
#include "viperfc/AttitudeControl.h"
#include "viperfc/PositionControl.h"
#include "viperfc/Tecs.h"
#include "viperfc/Transition.h"
#include "viperfc/Orbit.h"
#include "viperfc/Failsafe.h"
#include "viperfc/FlightComputer.h"
#include "viperfc/TelemetrySchema.h"
