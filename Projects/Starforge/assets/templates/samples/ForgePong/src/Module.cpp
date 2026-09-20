// Module.cpp — @PROJECT_NAME@ game module (the ForgePong sample, App Platform / AP-04).
//
// 2D sprites + canvas UI + a menu -> game -> win flow + two tiny scripts (Phase 17 /
// U8). CS_MODULE_BEGIN/END expand to the two exports the engine expects:
//   * CosmicModule_Register(ModuleRegistry&) — scripts + custom components (the
//     editor calls this on hot reload);
//   * CreatePluginLayer() -> Cosmic::PlayerLayer — the standalone player, run by
//     the Launcher / `CosmicApp --project @PROJECT_NAME@` / a packaged exe.
//
// Add one CS_SCRIPT / CS_COMPONENT block per class. Rebuild in the editor with
// "Build Scripts" (Ctrl+B).

#include <Cosmic.h>

#include "scripts/PaddleController.h"
#include "scripts/PongBall.h"

CS_MODULE_BEGIN(@PROJECT_NAME@)
    // ForgePong (Phase 17 / U8) — 2D sprites + UI + flow working together.
    CS_SCRIPT(PaddleController)
        CS_FIELD(Speed).Range(0.0f, 30.0f)
        CS_FIELD(LimitY).Range(0.0f, 10.0f)
        CS_FIELD(UseArrows)
    CS_END;

    CS_SCRIPT(PongBall)
        CS_FIELD(Speed).Range(1.0f, 30.0f)
        CS_FIELD(SpeedUp).Range(1.0f, 1.5f)
        CS_FIELD(CourtHalfW).Range(1.0f, 32.0f)
        CS_FIELD(CourtHalfH).Range(1.0f, 32.0f)
        CS_FIELD(WinScore).Range(1.0f, 99.0f)
    CS_END;

    // CS_SCREENS_BEGIN — managed by Starforge ▸ Screens (one CS_SCRIPT per screen script)
    // CS_SCREENS_END
CS_MODULE_END()
