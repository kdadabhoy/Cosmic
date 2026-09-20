// Module.cpp — @PROJECT_NAME@ game module (the FlowDemo sample, App Platform / AP-04).
//
// The zero-code two-screen app (Phase 17 / U8): every screen and all navigation is
// data — scenes/*.cscene + flows/Main.cflow — no scene references a script. This
// module exists only so the standalone player boots (CS_MODULE_BEGIN/END expand to
// CosmicModule_Register + CreatePluginLayer); add scripts between the markers when
// you outgrow zero-code. Rebuild in the editor with "Build Scripts" (Ctrl+B).

#include <Cosmic.h>

CS_MODULE_BEGIN(@PROJECT_NAME@)
    // CS_SCREENS_BEGIN — managed by Starforge ▸ Screens (one CS_SCRIPT per screen script)
    // CS_SCREENS_END
CS_MODULE_END()
