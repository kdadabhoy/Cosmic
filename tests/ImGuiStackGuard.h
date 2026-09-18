#pragma once

// ImGuiStackGuard.h — WO-07 (2D stability): a Release-safe reader of Dear ImGui's
// push/pop stack depths, plus a scoped guard that asserts they balance.
//
// WHY THIS EXISTS. KI-1 is a snap-chip that pushes a style colour guarded on a
// flag, flips the flag inside the button, then pops guarded on the *changed*
// flag — so every click leaves the colour stack off by one. In a Debug build
// ImGui's own IM_ASSERT catches that (abort). In a **Release** build IM_ASSERT
// compiles out, so the imbalance is silent memory corruption and a whole-frame
// "did it crash?" check misses it. L05 must therefore assert stack balance after
// EACH scripted UI action by reading the ACTUAL stack sizes — which is what this
// header does, in both configurations.
//
// The depths come straight out of ImGuiContext / ImGuiWindow via
// <imgui_internal.h> (the same fields ImGui's ErrorCheck* routines read). No
// private ImGui behaviour is altered; this only observes.

#include <imgui.h>
#include <imgui_internal.h>

namespace CosmicTest
{
    // A snapshot of every ImGui stack that a widget helper can leak. Captured
    // from the live context; safe to call with no current context (returns zeros)
    // and safe in Release (reads real ImVector::Size fields).
    struct ImGuiStackDepths
    {
        int color      = 0;   // ImGuiContext::ColorStack       (PushStyleColor)
        int styleVar   = 0;   // ImGuiContext::StyleVarStack    (PushStyleVar)
        int font       = 0;   // ImGuiContext::FontStack        (PushFont)
        int beginPopup = 0;   // ImGuiContext::BeginPopupStack  (BeginPopup/Menu)
        int id         = 0;   // current window's IDStack       (PushID)
        int group      = 0;   // current window's DC.GroupStack (BeginGroup)

        static ImGuiStackDepths Capture()
        {
            ImGuiStackDepths d;
            ImGuiContext* g = ImGui::GetCurrentContext();
            if (!g)
                return d;
            d.color      = g->ColorStack.Size;
            d.styleVar   = g->StyleVarStack.Size;
            d.font       = g->FontStack.Size;
            d.beginPopup = g->BeginPopupStack.Size;
            if (ImGuiWindow* w = g->CurrentWindow)
            {
                d.id    = w->IDStack.Size;
                d.group = w->DC.GroupStack.Size;
            }
            return d;
        }

        bool operator==(const ImGuiStackDepths& o) const
        {
            return color == o.color && styleVar == o.styleVar && font == o.font &&
                   beginPopup == o.beginPopup && id == o.id && group == o.group;
        }
        bool operator!=(const ImGuiStackDepths& o) const { return !(*this == o); }

        // A compact "C<n> S<n> F<n> P<n> ID<n> G<n>" for logs/CAPTURE.
        void Format(char* buf, size_t n) const
        {
#if defined(_MSC_VER)
            _snprintf_s(buf, n, _TRUNCATE,
#else
            snprintf(buf, n,
#endif
                "C%d S%d F%d P%d ID%d G%d", color, styleVar, font, beginPopup, id, group);
        }
    };

    // Difference of two snapshots (after - before), field-wise. Zero == balanced.
    inline ImGuiStackDepths StackDelta(const ImGuiStackDepths& before,
                                       const ImGuiStackDepths& after)
    {
        ImGuiStackDepths d;
        d.color      = after.color      - before.color;
        d.styleVar   = after.styleVar   - before.styleVar;
        d.font       = after.font       - before.font;
        d.beginPopup = after.beginPopup - before.beginPopup;
        d.id         = after.id         - before.id;
        d.group      = after.group      - before.group;
        return d;
    }

    inline bool IsBalanced(const ImGuiStackDepths& before, const ImGuiStackDepths& after)
    {
        return before == after;
    }
}
