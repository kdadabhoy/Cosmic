// widgets/NodeCanvasRoute.cpp — NodeCanvas::RouteLink (UX-01, contract §1).
//
// Pure geometry (ImVec2 / ImRect / float, no ImGui context): compiled into the Starforge
// editor DLL and into CosmicTests (FE01). NodeCanvas::Begin installs it as the vendored
// imgui-node-editor's link router (VENDOR-NOTES.md, local patch 2), so Link::GetCurve —
// and through it drawing, hit-testing and bounds — use these control points.
//
// Forward links (the end pin right of the start pin) keep imgui-node-editor's own curve,
// float for float. Backward links and self-loops are one cubic that leaves the output pin
// to the right at 45 degrees, passes below the union of both node rects and enters the
// input pin from the left at 45 degrees: control points start + (s, s) and end + (-s, s),
// with the smallest reach `s` (bisection, fixed step count — deterministic) for which
//   * the curve's rightmost point clears the union's right side and its leftmost point
//     clears the union's left side (by kPad + kClear), and
//   * where the curve crosses back over the union's right edge (heading left) and over its
//     left edge, it is already below the union's bottom (by kPad + kClear).
// Between those crossings the y-curve is concave (both control points sit below both
// pins), so the whole under-pass stays below the union; before the first crossing the
// curve moves right and down, away from the source box.

#include "widgets/NodeCanvas.h"

#include <imgui_internal.h>

namespace Starforge
{
    namespace
    {
        constexpr float kPad   = 2.0f;    // the FE01 oracle's grown-rect tolerance
        constexpr float kClear = 10.0f;   // clearance the loop keeps from the union's sides / bottom

        inline float Cubic(float p0, float p1, float p2, float p3, float t)
        {
            const float u = 1.0f - t;
            return u * u * u * p0 + 3.0f * u * u * t * p1 + 3.0f * u * t * t * p2 + t * t * t * p3;
        }

        // The two parameters where the x-cubic turns (dx/dt = 0): its rightmost point, then
        // its leftmost point. False when the curve does not swing right-then-left.
        bool XTurns(float x0, float x1, float x2, float x3, float& tRight, float& tLeft)
        {
            const float d0 = x1 - x0, d1 = x2 - x1, d2 = x3 - x2;
            const float a = d0 - 2.0f * d1 + d2;
            const float b = 2.0f * (d1 - d0);
            const float c = d0;
            const float disc = b * b - 4.0f * a * c;
            if (!(a > 0.0f) || !(disc > 0.0f))
                return false;
            const float r = ImSqrt(disc);
            tRight = (-b - r) / (2.0f * a);
            tLeft  = (-b + r) / (2.0f * a);
            return tRight > 0.0f && tLeft < 1.0f && tRight < tLeft;
        }

        // x(t) falls monotonically on [lo, hi] (between the two turns): where it crosses `level`.
        float CrossLeft(float x0, float x1, float x2, float x3, float lo, float hi, float level)
        {
            for (int i = 0; i < 20; ++i)
            {
                const float mid = 0.5f * (lo + hi);
                if (Cubic(x0, x1, x2, x3, mid) > level) lo = mid;
                else                                    hi = mid;
            }
            return 0.5f * (lo + hi);
        }

        // Does reach `s` carry the loop around the union rect `u`?
        bool Clears(float s, const ImVec2& a, const ImVec2& b, const ImRect& u)
        {
            const float x1 = a.x + s, x2 = b.x - s;
            const float y1 = a.y + s, y2 = b.y + s;
            float tRight = 0.0f, tLeft = 0.0f;
            if (!XTurns(a.x, x1, x2, b.x, tRight, tLeft))
                return false;
            if (Cubic(a.x, x1, x2, b.x, tRight) < u.Max.x + kPad + kClear) return false;
            if (Cubic(a.x, x1, x2, b.x, tLeft)  > u.Min.x - kPad - kClear) return false;
            const float floorY = u.Max.y + kPad + kClear;
            const float tIn  = CrossLeft(a.x, x1, x2, b.x, tRight, tLeft, u.Max.x + kPad);
            const float tOut = CrossLeft(a.x, x1, x2, b.x, tRight, tLeft, u.Min.x - kPad);
            return Cubic(a.y, y1, y2, b.y, tIn) >= floorY && Cubic(a.y, y1, y2, b.y, tOut) >= floorY;
        }
    }

    void NodeCanvas::RouteLink(ImVec2 start, ImVec2 end, const ImRect& startNode, const ImRect& endNode,
                               float strength, ImVec2& cp0, ImVec2& cp1)
    {
        const bool selfLoop = startNode.Min.x == endNode.Min.x && startNode.Min.y == endNode.Min.y &&
                              startNode.Max.x == endNode.Max.x && startNode.Max.y == endNode.Max.y;

        if (!selfLoop && (end.x - start.x) >= 0.0f)   // dot(end - start, (1,0)) >= 0: forward
        {
            // imgui-node-editor's Link::GetCurve, operation for operation: easeLinkStrength with
            // both pins at `strength`, the pins' default directions (1,0) and (-1,0).
            const float distanceX    = end.x - start.x;
            const float distanceY    = end.y - start.y;
            const float distance     = ImSqrt(distanceX * distanceX + distanceY * distanceY);
            const float halfDistance = distance * 0.5f;
            float eased = strength;
            if (halfDistance < eased)
                eased = eased * ImSin(IM_PI * 0.5f * halfDistance / eased);
            cp0 = ImVec2(start.x + 1.0f * eased, start.y + 0.0f * eased);
            cp1 = ImVec2(end.x + -1.0f * eased, end.y + 0.0f * eased);
            return;
        }

        // Backward link or self-loop: under the union of both rects.
        ImRect u;
        u.Min = ImVec2(ImMin(startNode.Min.x, endNode.Min.x), ImMin(startNode.Min.y, endNode.Min.y));
        u.Max = ImVec2(ImMax(startNode.Max.x, endNode.Max.x), ImMax(startNode.Max.y, endNode.Max.y));

        float lo = 0.0f;
        float hi = ImMax(strength, 4.0f * ((u.Max.x - u.Min.x) + (u.Max.y - u.Min.y)) + 64.0f);
        for (int i = 0; i < 24; ++i)
        {
            const float mid = 0.5f * (lo + hi);
            if (Clears(mid, start, end, u)) hi = mid;
            else                            lo = mid;
        }
        const float s = hi;   // the smallest clearing reach found (the initial bound if none)
        cp0 = ImVec2(start.x + s, start.y + s);
        cp1 = ImVec2(end.x - s, end.y + s);
    }
}
