#pragma once

// editors/FlowTrigger.h
//
// ============================================================================
// Starforge Flow Editor — the transition trigger-kind model (UX-01, contract §1).
// ============================================================================
//
// A transition's `On` string encodes what fires it (FlowMachine.h, FlowTransition):
//   Event  a signal name ("start_clicked"; UiButton / UiSlider / UiToggle signals),
//   Key    "key:<Name>" (FlowKeyBridge rising edge),
//   Timer  "timer:<seconds>",
//   When   "when" (condition only: fires once its `if` guard passes).
// The transition inspector shows one selector over the four kinds; switching goes
// through SetKind, which owns the rules (kept here, ImGui-free, so CosmicTests drives
// the exact command path the inspector runs — FE02):
//   * to When: `On` = "when"; a transition without a guard gets "Guard (if)" switched on
//     — with its previous guard fields when it still carries them, otherwise the empty
//     guard skeleton (Validate reports it until a channel / variable / entity is named);
//   * away from When: `On` = the last value this transition had for the target kind
//     (remembered per transition for the document's lifetime; defaults "signal",
//     "key:Escape", "timer:1"), and the guard is switched off ONLY when SetKind(When)
//     added it and it is still empty — a guard the user filled in is kept;
//   * between Event / Key / Timer: the current value is remembered for its kind and the
//     target kind's last value (or default) is restored.
// The on-disk .cflow format is untouched: kinds are derived from `On`, never stored.
// ============================================================================

#include "scene/FlowMachine.h"

#include <string>

namespace Starforge
{
    namespace FlowTrigger
    {
        enum class Kind : int { Event = 0, Key = 1, Timer = 2, When = 3 };

        // Per-transition switching memory (the inspector keeps one per transition).
        struct Memory
        {
            std::string Last[3];     // last Event / Key / Timer `On` value ("" = never seen)
            bool        AutoGuard = false;   // SetKind(When) switched the guard on
        };

        Kind        KindOf(const std::string& on);
        const char* Label(Kind k);                       // "Event", "Key", "Timer", "When"
        std::string DefaultOn(Kind k);                   // "signal", "key:Escape", "timer:1", "when"

        // True when a guard names no source (no channel, no variable, no entity): it can
        // never pass (FlowMachine's entity lookup of "" fails) — FlowAsset::Validate reports it.
        bool IsEmptyGuard(const Cosmic::FlowGuard& g);

        // "timer:<seconds>" helpers for the Timer field (false / 0 when `on` is not a timer).
        bool        TimerSeconds(const std::string& on, float& seconds);
        std::string TimerOn(float seconds);

        // Switch `tr` to kind `to` under the rules above. Returns false (and changes
        // nothing) when `tr` already is of that kind.
        bool SetKind(Cosmic::FlowTransition& tr, Kind to, Memory& mem);
    }
}
