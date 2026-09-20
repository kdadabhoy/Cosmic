// Module.cpp — PendulumLab2 app module (app template, App Platform / AP-04).
//
#include "screens/LabScreen.h"
#include "screens/StoppedScreen.h"

// CS_MODULE_BEGIN/END expand to the two exports the engine expects:
//   * CosmicModule_Register(ModuleRegistry&) — scripts, services + custom
//     components (the editor calls this on hot reload);
//   * CreatePluginLayer() -> Cosmic::PlayerLayer — the standalone player, run by
//     the Launcher / `CosmicApp --project PendulumLab2` / a packaged exe.
//
// The app's LOGIC is the service (CS_SERVICE): one instance per run, ticked by the
// host, publishing DataBus channels the scenes' bound widgets display. The screen
// scripts (between the CS_SCREENS markers, one per scenes/<Name>.cscene) hold only
// per-display touches. Rebuild in the editor with "Build Scripts" (Ctrl+B); with
// kind = "app" the editor auto-builds on save and resumes Play (D-LIVE).

#include <Cosmic.h>

#include "services/PendulumService.h"
#include "screens/HomeScreen.h"
#include "screens/DashboardScreen.h"
#include "screens/SettingsScreen.h"

CS_MODULE_BEGIN(PendulumLab2)
    CS_SERVICE(PendulumService).Order(0) CS_END;

    // CS_SCREENS_BEGIN — managed by Starforge ▸ Screens (one CS_SCRIPT per screen script)
    CS_SCRIPT(HomeScreen)
        CS_FIELD(PulseHz).Range(0.0f, 5.0f)
        CS_FIELD(PulseMin).Range(0.0f, 1.0f)
    CS_END;

    CS_SCRIPT(StoppedScreen)
        CS_FIELD(ResetOnResume)
    CS_END;
    // CS_SCREENS_END
CS_MODULE_END()
