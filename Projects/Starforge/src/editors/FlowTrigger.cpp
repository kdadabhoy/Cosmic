// editors/FlowTrigger.cpp — see FlowTrigger.h (UX-01, contract §1). ImGui-free:
// compiled into the Starforge editor DLL and into CosmicTests (FE02).

#include "editors/FlowTrigger.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace Starforge
{
    namespace FlowTrigger
    {
        namespace
        {
            bool StartsWith(const std::string& s, const char* prefix)
            {
                return s.rfind(prefix, 0) == 0;
            }
        }

        Kind KindOf(const std::string& on)
        {
            if (on == "when")             return Kind::When;
            if (StartsWith(on, "key:"))   return Kind::Key;
            if (StartsWith(on, "timer:")) return Kind::Timer;
            return Kind::Event;
        }

        const char* Label(Kind k)
        {
            switch (k)
            {
            case Kind::Key:   return "Key";
            case Kind::Timer: return "Timer";
            case Kind::When:  return "When";
            default:          return "Event";
            }
        }

        std::string DefaultOn(Kind k)
        {
            switch (k)
            {
            case Kind::Key:   return "key:Escape";
            case Kind::Timer: return "timer:1";
            case Kind::When:  return "when";
            default:          return "signal";
            }
        }

        bool IsEmptyGuard(const Cosmic::FlowGuard& g)
        {
            return g.Channel.empty() && g.Var.empty() && g.Entity.empty();
        }

        bool TimerSeconds(const std::string& on, float& seconds)
        {
            seconds = 0.0f;
            if (!StartsWith(on, "timer:"))
                return false;
            const char* text = on.c_str() + 6;
            char* end = nullptr;
            const double v = std::strtod(text, &end);
            if (end == text || !std::isfinite(v))
                return false;
            seconds = (float)v;
            return true;
        }

        std::string TimerOn(float seconds)
        {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "timer:%g", (double)seconds);
            return buf;
        }

        bool SetKind(Cosmic::FlowTransition& tr, Kind to, Memory& mem)
        {
            const Kind from = KindOf(tr.On);
            if (from == to)
                return false;

            if (from != Kind::When)
                mem.Last[(int)from] = tr.On;

            if (to == Kind::When)
            {
                tr.On = "when";
                // Previous guard fields (a guard switched off earlier) come back as they were;
                // a transition that never had one gets the empty skeleton, which Validate flags.
                mem.AutoGuard = !tr.HasGuard;
                tr.HasGuard = true;
                return true;
            }

            const std::string& last = mem.Last[(int)to];
            tr.On = last.empty() ? DefaultOn(to) : last;
            if (from == Kind::When)
            {
                // Drop only the guard SetKind(When) added, and only while it is still empty.
                if (mem.AutoGuard && tr.HasGuard && IsEmptyGuard(tr.Guard))
                    tr.HasGuard = false;
                mem.AutoGuard = false;
            }
            return true;
        }
    }
}
