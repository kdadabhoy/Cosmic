// Module.cpp — Forge Isle game module (Phase 28 / doc 27; design:
// docs/design/forge-isle.md).
//
// CS_MODULE_BEGIN/END expand to the two exports the engine expects:
//   * CosmicModule_Register(ModuleRegistry&) — scripts + custom components (the
//     editor calls this on hot reload);
//   * CreatePluginLayer() -> Cosmic::PlayerLayer — the standalone player, run by
//     the Launcher / `CosmicApp --project ForgeIsle` / the packaged exe.
//
// Z1 registers the greybox walker only; Z2–Z6 add the character/quest/creature/
// tent-game scripts here as they land.

#include <Cosmic.h>

#include "scripts/PlayerController.h"

CS_MODULE_BEGIN(ForgeIsle)
    // The player (Z1 greybox tier; upgraded in Z2 — design doc §8).
    CS_SCRIPT(PlayerController)
        CS_FIELD(MoveSpeed).Range(0.0f, 20.0f)
        CS_FIELD(RunMultiplier).Range(1.0f, 4.0f)
        CS_FIELD(JumpSpeed).Range(0.0f, 20.0f)
        CS_FIELD(LookSensitivity).Range(0.01f, 1.0f)
    CS_END;
CS_MODULE_END()
