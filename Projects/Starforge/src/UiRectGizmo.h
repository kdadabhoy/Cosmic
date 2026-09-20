#pragma once

// UiRectGizmo.h — AP-03 (App Platform): the 2D rect gizmo for canvas UI elements.
//
// When the editor is in 2D mode and the primary selection carries a
// RectTransformComponent, the gizmo draws a move handle (the element's rect
// outline + interior) and eight resize handles over the element's RESOLVED rect
// in the viewport (a Renderer2D overlay, like the collider overlay). A drag is one
// CommandStack entry per gesture and edits OffsetMin / OffsetMax ONLY — anchors and
// pivot are never touched. Two snap chips (1/8 px, 16 px grid) live in the viewport
// strip next to the existing chips (ViewportController draws them off `Snap`).
//
// The pure math is `UiRectGizmoMath`, a static class with no ImGui / GL / scene
// dependency, unit-tested bit-exact in tests/test_ap03_editor.cpp (E03).

#include <Cosmic.h>
#include "scene/ui/UiComponents.h"

#include <string>

namespace Starforge
{
    struct EditorContext;

    // Handle ids: Move = anywhere inside the rect (not on a resize square);
    // the eight resize squares are named by compass direction (screen space,
    // +y DOWN — N is the TOP edge).
    enum class RectHandle : int { None = -1, Move = 0, N, NE, E, SE, S, SW, W, NW };

    struct RectGizmoSnap
    {
        bool PixelEighth = false;   // snap edges / corners to 1/8 px
        bool Grid16      = false;   // snap to a 16 px grid (wins when both are on)
    };

    class UiRectGizmoMath
    {
    public:
        static constexpr float kHandleHalf = 5.0f;   // resize square half-size (px)
        static constexpr float kMinSize    = 1.0f;   // resolved rect never shrinks below 1 px

        // The square for a resize handle (Move => the whole rect).
        static Cosmic::UiRect HandleRect(const Cosmic::UiRect& rect, RectHandle h);

        // Which handle `p` (canvas px) hits: resize squares first (so a tiny
        // element is still resizable), then Move when inside, else None.
        static RectHandle HitTest(const Cosmic::UiRect& rect, const glm::vec2& p);

        // One coordinate through the snap rule (Grid16 > PixelEighth > identity).
        static float Snap(float v, const RectGizmoSnap& snap);

        struct DragInput
        {
            RectHandle     Handle = RectHandle::Move;
            Cosmic::UiRect StartRect;               // resolved rect at press (canvas px)
            glm::vec2      StartOffsetMin{ 0.0f };  // RectTransform offsets at press
            glm::vec2      StartOffsetMax{ 0.0f };
            float          Scale = 1.0f;            // canvas scale (rect px = offset px * Scale)
            glm::vec2      Delta{ 0.0f };           // pointer delta since press (canvas px)
            RectGizmoSnap  Snap;
        };
        struct DragResult
        {
            Cosmic::UiRect Rect;         // the new resolved rect
            glm::vec2      OffsetMin;    // the new RectTransform offsets
            glm::vec2      OffsetMax;
        };

        // Apply a drag: Move translates (snapping the top-left corner, the size is
        // kept exactly); a resize moves only the edges its handle owns (snapped),
        // then clamps them so the rect keeps >= kMinSize on each axis. Offsets
        // come back as start + (edge delta / Scale), so a drag of exactly k px
        // moves the offsets by exactly k / Scale.
        static DragResult ApplyDrag(const DragInput& in);

        static bool MovesLeft  (RectHandle h) { return h == RectHandle::W  || h == RectHandle::NW || h == RectHandle::SW; }
        static bool MovesRight (RectHandle h) { return h == RectHandle::E  || h == RectHandle::NE || h == RectHandle::SE; }
        static bool MovesTop   (RectHandle h) { return h == RectHandle::N  || h == RectHandle::NE || h == RectHandle::NW; }
        static bool MovesBottom(RectHandle h) { return h == RectHandle::S  || h == RectHandle::SE || h == RectHandle::SW; }
        static const char* Name(RectHandle h);
    };

    // The interactive gizmo (editor state machine). Pointer state comes from Dear
    // ImGui's io (the same queue the L05/AP-03 self-tests inject into), so a
    // scripted press/drag/release drives exactly what a user's mouse would.
    class UiRectGizmo
    {
    public:
        RectGizmoSnap Snap;   // the viewport strip's two chips edit this

        // Per frame, from OnImGuiRender (edit mode + 2D only; the caller gates).
        // Resolves the primary selection's rect inside the letterbox band, runs the
        // press/drag/release machine and records the command on release. Returns
        // true while the gizmo owns the pointer (hovering a handle or dragging) —
        // the viewport then skips its own click-pick / click-away-deselect.
        bool Update(EditorContext& ctx, const glm::vec2& vpPos, const glm::vec2& vpSize,
                    const glm::vec4& bandUv);

        // Inside the viewport's DrawOverlay2D pass (after UiSystem::Render): the
        // outline + nine handles through Renderer2D in target pixel space.
        void Draw(EditorContext& ctx, uint32_t targetW, uint32_t targetH, const glm::vec4& bandUv);

        bool Busy() const     { return m_Dragging || m_Hover != RectHandle::None; }
        bool Dragging() const { return m_Dragging; }
        RectHandle Hover() const { return m_Hover; }
        bool HasTarget() const { return m_HasRect; }
        const Cosmic::UiRect& TargetRect() const { return m_Rect; }   // last resolved (viewport-local px)

        // Rect of a handle in SCREEN coordinates from the last Update — what a
        // scripted driver aims its injected pointer at.
        Cosmic::UiRect HandleScreenRect(RectHandle h) const;

    private:
        // Resolve the primary selection's element rect (viewport-local px) + scale.
        bool ResolveTarget(EditorContext& ctx, const glm::vec2& vpSize, const glm::vec4& bandUv,
                           Cosmic::UiRect& outRect, float& outScale);

        bool           m_HasRect  = false;
        Cosmic::UiRect m_Rect;                   // viewport-local px (last resolve)
        float          m_Scale    = 1.0f;
        glm::vec2      m_VpPos{ 0.0f };
        RectHandle     m_Hover    = RectHandle::None;

        bool           m_Dragging = false;
        RectHandle     m_DragHandle = RectHandle::None;
        glm::vec2      m_PressLocal{ 0.0f };
        Cosmic::UiRect m_StartRect;
        glm::vec2      m_StartMin{ 0.0f }, m_StartMax{ 0.0f };
        uint64_t       m_DragUuid = 0;
        bool           m_LmbWas   = false;
    };
}
