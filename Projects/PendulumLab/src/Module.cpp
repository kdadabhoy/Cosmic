// Module.cpp — PendulumLab app module (Cosmic App Platform / AP-04, the §8 sample).
//
// CS_MODULE_BEGIN/END expand to the two exports the engine expects:
//   * CosmicModule_Register(ModuleRegistry&) — scripts, services + custom
//     components (the editor calls this on hot reload);
//   * CreatePluginLayer() -> Cosmic::PlayerLayer — the standalone player, run by
//     the packaged PendulumLab.exe (CosmicApp.exe renamed) through boot.cfg.
//
// The simulation is PendulumService (CS_SERVICE); the screens hold per-display
// touches only. Y02SelfTestService is the in-app acceptance harness (catalog Y02/
// Y03): it is registered always but does nothing unless COSMIC_Y02_SELFTEST is set.

#include <Cosmic.h>

#include "services/PendulumService.h"
#include "Y02SelfTest.h"
#include "screens/HomeScreen.h"
#include "screens/LabScreen.h"
#include "screens/SettingsScreen.h"
#include "screens/StoppedScreen.h"

CS_MODULE_BEGIN(PendulumLab)
    CS_SERVICE(PendulumService).Order(0) CS_END;
    CS_SERVICE(Y02SelfTestService).Order(100) CS_END;   // ticks after the physics

    // CS_SCREENS_BEGIN — managed by Starforge ▸ Screens (one CS_SCRIPT per screen script)
    CS_SCRIPT(HomeScreen)
        CS_FIELD(SwingPx).Range(0.0f, 200.0f)
        CS_FIELD(SwingHz).Range(0.0f, 2.0f)
        CS_FIELD(PulseMin).Range(0.0f, 1.0f)
    CS_END;

    CS_SCRIPT(LabScreen)
        CS_FIELD(RodWidth).Range(0.01f, 0.5f)
    CS_END;

    CS_SCRIPT(SettingsScreen)
        CS_FIELD(ChangesSeen)
    CS_END;

    CS_SCRIPT(StoppedScreen)
        CS_FIELD(ResetOnResume)
    CS_END;
    // CS_SCREENS_END
CS_MODULE_END()
