#pragma once
// scene/ui/UiSystem.h
//
// ============================================================================
// Cosmic in-game UI runtime (Phase 17 / U1).
// ============================================================================
//
// Engine free functions (shared by the Starforge editor viewport AND the
// standalone PlayerLayer) that turn a canvas of UI entities into layout,
// pointer interaction, and a Renderer2D draw pass. The math is PURE and
// headless-tested (ResolveRect, StepButtonState); only Render touches GL.
//
// Rendering-order contract (doc 16 §1): world (SceneRenderer HDR+post) →
// canvas UI (this, screen-space, sorted by Canvas.SortOrder then ZOrder) →
// editor overlays. SceneRenderer's DrawOverlay2D callback is the injection
// point (LDR target bound, after composite).
//
// CANVAS SPACE: viewport-local pixels, origin TOP-LEFT, +y DOWN. The pointer
// handed to Update is in the same space (screen mouse − viewport top-left).
// ============================================================================

#include "core/Core.h"
#include "scene/ui/UiComponents.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace Cosmic
{
    class Scene;

    /**
     * @brief Pointer state for one UI update, in canvas space (viewport-local,
     * top-left origin). PressedEdge/ReleasedEdge are this frame's button
     * down/up transitions (the caller derives them from Input); Down is the
     * current held state. Kept explicit so button logic is testable.
     */
    struct UiPointer
    {
        glm::vec2 Position{ 0.0f };
        bool      Down         = false;
        bool      PressedEdge  = false;
        bool      ReleasedEdge = false;
    };

    /** @brief Result of stepping one button's state machine (pure). */
    struct ButtonStep
    {
        UiButtonState State = UiButtonState::Normal;
        bool          Armed = false;   // a press began on this button, still held
        bool          Emit  = false;   // release-inside happened this step
    };

    /** @brief One resolved UI element ready to draw / hit-test. */
    struct UiElement
    {
        uint32_t Handle = 0;      // entt::entity value (cast back at the call site)
        UiRect   Rect;
        float    Scale       = 1.0f;  // owning canvas' pixel scale (for text/9-slice)
        int32_t  CanvasOrder = 0;
        int32_t  ZOrder      = 0;
        int32_t  Seq         = 0;  // DFS order, stable tie-break
    };

    class COSMIC_API UiSystem
    {
    public:
        // ---- pure layout / interaction (headless-tested) --------------------

        /** @brief Resolve a child rect from its parent rect + anchors + offsets.
         *  `scale` multiplies the pixel offsets (canvas ScaleWithHeight). Pure. */
        static UiRect ResolveRect(const UiRect& parent, const RectTransformComponent& rt,
                                  float scale = 1.0f);

        /** @brief The rect's pivot point in canvas space (rotation reference). */
        static glm::vec2 PivotPoint(const UiRect& rect, const glm::vec2& pivot);

        /** @brief Project a WORLD point through `viewProj` into canvas space
         *  (top-left origin, +y DOWN, within `canvasRect`). Returns false when the
         *  point is BEHIND the camera (clip.w <= 0). The X6 world-anchor projector —
         *  pure + headless-tested, works for 2D (ortho) and 3D (perspective) VPs. */
        static bool ProjectToCanvas(const glm::vec3& worldPos, const glm::mat4& viewProj,
                                    const UiRect& canvasRect, glm::vec2& outPoint);

        /** @brief Canvas pixel-scale factor for a viewport (1.0 for ConstantPixel). */
        static float CanvasScale(const CanvasComponent& canvas, const UiRect& viewport);

        /** @brief One step of a button's Normal/Hover/Pressed/Disabled machine.
         *  Emits on a release that both began and ended on the button. Pure. */
        static ButtonStep StepButtonState(UiButtonState prev, bool armedPrev,
                                          bool interactable, bool hovered,
                                          bool pressedEdge, bool releasedEdge, bool down);

        // ---- scene-driven (engine, shared by editor + player) ---------------

        /** @brief Resolve every canvas subtree in the scene into a back-to-front
         *  ordered list (ascending CanvasOrder, ZOrder, Seq). Pure over the scene
         *  data (no GL). Empty when the scene has no CanvasComponent. */
        static void CollectElements(Scene& scene, const UiRect& viewport,
                                    std::vector<UiElement>& out,
                                    const glm::mat4* cameraViewProj = nullptr);

        /** @brief Advance button states from the pointer and emit signals on the
         *  scene EventBus (U2). Returns true when the pointer is over an
         *  interactable button (the caller then skips 3D scene picking). */
        static bool Update(Scene& scene, const UiRect& viewport, const UiPointer& pointer,
                           const glm::mat4* cameraViewProj = nullptr);

        /** @brief Topmost drawable UI element under `point` (any element that
         *  CollectElements returns, not just buttons — an editor uses this to
         *  select UI entities the 3D ID-pass picker can't see). Returns false
         *  when nothing is hit; outEntity is the entt::entity value. Pure. */
        static bool HitTest(Scene& scene, const UiRect& viewport, const glm::vec2& point,
                            uint32_t& outEntity, const glm::mat4* cameraViewProj = nullptr);

        /** @brief Draw the scene's canvases through Renderer2D in screen space.
         *  PRE: the destination FBO is bound and its GL viewport is the full
         *  target; canvas space is viewport-local with Min at (0,0). Main-thread/GL. */
        static void Render(Scene& scene, const UiRect& viewport,
                           const glm::mat4* cameraViewProj = nullptr);

        /** @brief Letterboxed variant (U7): lay the canvases out in `canvasRect`
         *  — a sub-rect of the bound target (an aspect-locked game view) — while
         *  projecting over the full targetW x targetH, so elements land at their
         *  absolute positions inside the band and authored anchors stay truthful.
         *  canvasRect == the full target behaves exactly like the 2-arg Render. */
        static void Render(Scene& scene, const UiRect& canvasRect,
                           uint32_t targetW, uint32_t targetH,
                           const glm::mat4* cameraViewProj = nullptr);
    };
}
