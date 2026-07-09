#pragma once
// scene/ui/UiComponents.h
//
// ============================================================================
// Cosmic in-game UI — entity components (Phase 17 / U1).
// ============================================================================
//
// UI elements are ENTITIES: a canvas + rect-transform hierarchy authored the
// same way as any other scene content, so the Inspector, serializer, undo,
// prefabs and scripts all work on them for free (the E1/E2 dividend). This
// header defines the reflected data; scene/ui/UiSystem.{h,cpp} owns the pure
// layout/hit-test/button logic and the Renderer2D draw pass.
//
// CANVAS SPACE: pixels, origin TOP-LEFT, +x right, +y DOWN. A canvas root maps
// to the viewport rect; a child's rect is a pure function of its parent rect,
// anchors and offsets (UiSystem::ResolveRect — headless unit-tested). This is
// engine-generic: no shipped app attaches any of these components, so the
// compat gate holds (UiSystem does nothing on a scene with no CanvasComponent).
// ============================================================================

#include "core/Core.h"
#include "scene/ComponentRegistry.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <string>

namespace Cosmic
{
    class Texture2D;
    class Font;

    /**
     * @brief Canvas-space rectangle (pixels, top-left origin, +y down). The
     * canvas root's rect is the viewport; every element resolves to one of
     * these. Kept tiny + header-only so the pure layout math is testable.
     */
    struct UiRect
    {
        glm::vec2 Min{ 0.0f };   // top-left corner
        glm::vec2 Max{ 0.0f };   // bottom-right corner

        glm::vec2 Size()   const { return Max - Min; }
        glm::vec2 Center() const { return (Min + Max) * 0.5f; }
        float     Width()  const { return Max.x - Min.x; }
        float     Height() const { return Max.y - Min.y; }

        bool Contains(const glm::vec2& p) const
        {
            return p.x >= Min.x && p.x <= Max.x && p.y >= Min.y && p.y <= Max.y;
        }
    };

    /**
     * @brief How a Canvas scales its children with viewport size.
     *  ConstantPixel  — offsets are literal pixels (crisp fixed HUDs).
     *  ScaleWithHeight — offsets are multiplied by viewportH / ReferenceHeight,
     *                    so a layout authored for one height fills any window and
     *                    pixel art stays crisp at integer scales.
     */
    enum class UiScaleMode : int32_t { ConstantPixel = 0, ScaleWithHeight = 1 };

    /**
     * @brief Root of a UI element tree. Attach to one entity; its RectTransform
     * children resolve against the viewport. Multiple canvases in a scene draw
     * in ascending SortOrder (a HUD over a menu, etc.).
     */
    struct COSMIC_API CanvasComponent
    {
        UiScaleMode ScaleMode       = UiScaleMode::ScaleWithHeight;
        float       ReferenceHeight = 1080.0f;   // design height for ScaleWithHeight
        int32_t     SortOrder       = 0;         // lower draws first (further back)

        CanvasComponent() = default;
        CanvasComponent(const CanvasComponent&) = default;
    };

    /**
     * @brief Anchored rectangle within a parent rect (Unity-style anchors +
     * explicit corner offsets — no flexbox). AUTHORITATIVE for UI entities:
     * their sibling TransformComponent (added by CreateEntity) is ignored under
     * a Canvas. Resolution is a pure function (UiSystem::ResolveRect):
     *
     *   anchorMinPt = parent.Min + parent.Size()*AnchorMin
     *   anchorMaxPt = parent.Min + parent.Size()*AnchorMax
     *   rect.Min    = anchorMinPt + OffsetMin*scale
     *   rect.Max    = anchorMaxPt + OffsetMax*scale
     *
     * Point anchor (Min==Max) => a fixed-size box positioned by offsets; a
     * stretched anchor (e.g. Min={0,0} Max={1,1}) => insets from the parent edges.
     * Pivot is the rotation/scale reference point (0..1 within the rect); it does
     * not affect the resolved rect in v1 (no rotation yet).
     */
    struct COSMIC_API RectTransformComponent
    {
        glm::vec2 AnchorMin{ 0.0f, 0.0f };     // fraction of parent (top-left)
        glm::vec2 AnchorMax{ 0.0f, 0.0f };
        glm::vec2 OffsetMin{ 0.0f, 0.0f };     // pixels from the min anchor point
        glm::vec2 OffsetMax{ 100.0f, 40.0f };  // pixels from the max anchor point
        glm::vec2 Pivot{ 0.5f, 0.5f };
        int32_t   ZOrder = 0;                   // draw + hit order within a canvas

        RectTransformComponent() = default;
        RectTransformComponent(const RectTransformComponent&) = default;
    };

    /**
     * @brief Textured (or solid) rectangle. TexturePath empty => a flat Tint
     * quad. NineSlice keeps corners fixed while the middle stretches (l,t,r,b in
     * texture pixels; all-zero => a plain stretched quad). Runtime texture is
     * resolved lazily by UiSystem (main-thread/GL) and cached in Resolved.
     */
    struct COSMIC_API UiImageComponent
    {
        std::string    TexturePath;                 // AssetPath("texture"); empty => solid
        glm::vec4      Tint{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec4      NineSlice{ 0.0f };           // l, t, r, b border in texels
        bool           PreserveAspect = false;

        // Runtime-only (not reflected): lazily resolved texture + the path it was
        // resolved from (re-resolve when TexturePath changes).
        Ref<Texture2D> Resolved;
        std::string    ResolvedPath;

        UiImageComponent() = default;
        UiImageComponent(const UiImageComponent&) = default;
    };

    enum class UiHAlign : int32_t { Left = 0, Center = 1, Right = 2 };
    enum class UiVAlign : int32_t { Top = 0, Middle = 1, Bottom = 2 };

    /**
     * @brief A run of text inside the element's rect. Rendered through
     * Renderer2D::DrawString (SDF font atlas), so it stays crisp at any canvas
     * scale. FontPath empty => the engine default face. SizePx is the cap height
     * in canvas pixels BEFORE canvas scaling.
     */
    struct COSMIC_API UiTextComponent
    {
        std::string Text = "Text";
        std::string FontPath;                     // font stem or VFS path; empty => default
        float       SizePx = 32.0f;
        glm::vec4   Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        UiHAlign    HAlign = UiHAlign::Center;
        UiVAlign    VAlign = UiVAlign::Middle;
        bool        Wrap   = false;

        // Runtime-only (not reflected): lazily resolved font.
        Ref<Font>   ResolvedFont;
        std::string ResolvedFontPath;

        UiTextComponent() = default;
        UiTextComponent(const UiTextComponent&) = default;
    };

    /** @brief Live button interaction state (runtime-only). */
    enum class UiButtonState : int32_t { Normal = 0, Hover = 1, Pressed = 2, Disabled = 3 };

    /**
     * @brief Makes the element clickable. On a release INSIDE the element (after
     * the press began inside it) UiSystem emits Signal on the scene EventBus (U2)
     * — the single channel that reaches flow (U5) and scripts. The state tint is
     * multiplied into a sibling UiImageComponent (a button is an image + this).
     */
    struct COSMIC_API UiButtonComponent
    {
        std::string Signal = "clicked";          // emitted on release-inside
        glm::vec4   NormalTint  { 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec4   HoverTint   { 1.15f, 1.15f, 1.15f, 1.0f };
        glm::vec4   PressedTint { 0.8f, 0.8f, 0.8f, 1.0f };
        glm::vec4   DisabledTint{ 0.5f, 0.5f, 0.5f, 0.6f };
        bool        Interactable = true;

        // Runtime-only (not reflected): live state + whether the current press
        // began on this button (so a drag off-and-back still fires on release).
        UiButtonState State = UiButtonState::Normal;
        bool          Armed = false;

        UiButtonComponent() = default;
        UiButtonComponent(const UiButtonComponent&) = default;
    };
}

// EnTT type-hash stabilization across the DLL boundary (see Components.h). Each
// expansion is a consteval full specialization → ODR-safe in every TU.
CS_REGISTER_COMPONENT(Cosmic::CanvasComponent)
CS_REGISTER_COMPONENT(Cosmic::RectTransformComponent)
CS_REGISTER_COMPONENT(Cosmic::UiImageComponent)
CS_REGISTER_COMPONENT(Cosmic::UiTextComponent)
CS_REGISTER_COMPONENT(Cosmic::UiButtonComponent)
