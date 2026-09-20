#pragma once
// scene/FlowKeyBridge.h
//
// ============================================================================
// Cosmic flow key bridge — keyboard edges into "key:<Name>" flow signals
// (App Platform / AP-01, design contract §5).
// ============================================================================
//
// A .cflow names key triggers as transitions on "key:Escape", "key:Space", ...
// Both hosts used to hand-roll a single Escape edge; the bridge generalises it:
// Bind() resolves every FlowMachine::KeySignals() name to an engine key code
// (unknown names warn once and are skipped), Poll() feeds one FeedSignal per
// rising edge of each bound key. The probe is injectable so the bridge is
// headless-testable; the default reads Input::IsKeyPressed (lazily, at Poll).
//
// GL-free; the key table is fixed (contract §5): Escape, Space, Enter, Tab,
// Backspace, Up, Down, Left, Right, F1..F12, A..Z, 0..9.
// ============================================================================

#include "core/Core.h"

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace Cosmic
{
    struct FlowAsset;
    class FlowMachine;

    class COSMIC_API FlowKeyBridge
    {
    public:
        using Probe = std::function<bool(int keyCode)>;              // default: Input::IsKeyPressed

        void Bind(const FlowAsset& asset, Probe probe = {});          // resolves every KeySignals() name -> key code; unknown names warn once
        void Poll(FlowMachine& machine);                              // rising edge per bound key -> machine.FeedSignal("key:<Name>")
        static int KeyCodeFor(const std::string& name);               // Escape, Space, Enter, Tab, Backspace, Up, Down, Left, Right, F1..F12, A..Z, 0..9; -1 unknown

        size_t BoundCount() const { return m_Keys.size(); }
        void   Clear();                                               // forget the bindings (Bind replaces them anyway)

    private:
        struct Bound { std::string Signal; int Code = -1; bool Prev = false; };
        std::vector<Bound>              m_Keys;
        Probe                           m_Probe;
        std::unordered_set<std::string> m_Warned;   // unknown names already reported
    };
}
