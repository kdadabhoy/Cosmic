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

        // Runtime texture slot (X7 / gap §12.3): when set, UiSystem draws THIS
        // instead of the path-loaded texture — the injection point for a
        // SceneRenderer::RenderToTexture result (a live minimap, security-camera
        // feed, portal). Set from a script/app each frame; not serialized.
        Ref<Texture2D> RuntimeTexture;

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

    /**
     * @brief World-anchored UI (X6 / gap §12.2). Attach to a UI element to pin it
     * to a WORLD position instead of a parent-relative anchor: before layout,
     * UiSystem projects (target entity's world position + WorldOffset) through the
     * active camera into canvas space and treats that point (plus ScreenOffset) as
     * the element's origin — the RectTransform's offsets then size the box around
     * it. Nameplates, health bars, interaction prompts. Works for 2D and 3D
     * cameras. Behind the camera (or off-screen when HideWhenOffscreen) the element
     * and its subtree are hidden. TargetEntity == 0 uses WorldOffset as an absolute
     * world point. Compat: no component ⇒ the normal parent-relative layout.
     */
    struct COSMIC_API UiWorldAnchorComponent
    {
        uint64_t  TargetEntity = 0;        // UUID of the tracked world entity (0 = absolute point)
        glm::vec3 WorldOffset{ 0.0f };     // added to the target's world position
        glm::vec2 ScreenOffset{ 0.0f };    // canvas-pixel nudge after projection (e.g. lift above the head)
        bool      HideWhenOffscreen = true;

        UiWorldAnchorComponent() = default;
        UiWorldAnchorComponent(const UiWorldAnchorComponent&) = default;
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

    // ========================================================================
    // Bound widgets (App Platform AP-02, contract §3).
    // ========================================================================
    //
    // Widgets that READ a host-owned DataBus channel (value text, gauge,
    // indicator, plot) or WRITE one (slider, toggle), plus the canvas-side half
    // of a hosted ImGui panel. Every draw goes through Renderer2D inside
    // UiSystem::Render; the interaction lives in UiSystem::Update. Preview mode
    // (no bus, or `preview == true`) substitutes the Preview* fields so a
    // screen is previewable in the editor without an app running; in live mode
    // a missing channel shows the placeholder / off state / an empty plot.
    // Runtime-only members (Dragging, Armed, DrawnThisFrame, Resolved*) are not
    // reflected. The engine knows nothing about what a channel means (D-UI).

    enum class UiGaugeStyle        : int32_t { Bar = 0, Arc = 1 };
    enum class UiGaugeDirection    : int32_t { LeftToRight = 0, BottomToTop = 1 };
    enum class UiSliderOrientation : int32_t { Horizontal = 0, Vertical = 1 };

    /**
     * @brief Prints a channel through a sibling UiTextComponent (same entity),
     * which supplies font/size/colour/alignment. At draw time the resolved
     * string (Prefix + formatted value + Suffix) replaces what UiText would
     * have drawn; UiText.Text itself is never modified. Format is printf with
     * exactly one numeric conversion; anything else is used literally.
     * Missing channel (live mode) -> Placeholder; Age > StaleAfter -> StaleColor.
     */
    struct COSMIC_API UiValueTextComponent
    {
        std::string Channel;
        std::string Format      = "%.2f";
        std::string Prefix;
        std::string Suffix;
        std::string Placeholder = "--";
        float       StaleAfter  = 0.0f;                    // seconds; 0 = never stale
        glm::vec4   StaleColor{ 0.6f, 0.6f, 0.6f, 1.0f };
        float       PreviewValue = 0.0f;

        UiValueTextComponent() = default;
        UiValueTextComponent(const UiValueTextComponent&) = default;
    };

    /**
     * @brief Fills the element rect with a track + a fill proportional to
     * clamp((v - Min) / (Max - Min), 0, 1). Bar: two quads (Direction picks
     * the growth axis). Arc: a 270-degree ring with its gap at the bottom,
     * Thickness = ring thickness as a fraction of the radius. A sibling UiImage
     * draws behind.
     */
    struct COSMIC_API UiGaugeComponent
    {
        std::string      Channel;
        float            Min = 0.0f;
        float            Max = 100.0f;
        UiGaugeStyle     Style     = UiGaugeStyle::Bar;
        UiGaugeDirection Direction = UiGaugeDirection::LeftToRight;   // ignored by Arc
        glm::vec4        FillColor { 0.2f,  0.8f,  0.3f,  1.0f };
        glm::vec4        TrackColor{ 0.15f, 0.15f, 0.18f, 1.0f };
        float            Thickness = 0.25f;
        float            PreviewValue = 50.0f;

        UiGaugeComponent() = default;
        UiGaugeComponent(const UiGaugeComponent&) = default;
    };

    /**
     * @brief A quad whose texture + tint follow a comparison of the channel
     * against Threshold (Op: == != < > <= >=). Bool channels compare as 0/1;
     * a missing or non-finite value is Off. Empty texture = solid tint.
     */
    struct COSMIC_API UiIndicatorComponent
    {
        std::string Channel;
        std::string Op        = "==";
        float       Threshold = 1.0f;
        glm::vec4   OnTint { 0.2f, 1.0f, 0.3f, 1.0f };
        glm::vec4   OffTint{ 0.3f, 0.3f, 0.3f, 1.0f };
        std::string OnTexture;                       // AssetPath("texture"); empty => solid
        std::string OffTexture;
        bool        PreviewOn = false;

        // Runtime-only (not reflected): lazily resolved textures + their source paths.
        Ref<Texture2D> ResolvedOn,  ResolvedOff;
        std::string    ResolvedOnPath, ResolvedOffPath;

        UiIndicatorComponent() = default;
        UiIndicatorComponent(const UiIndicatorComponent&) = default;
    };

    /**
     * @brief Time-series plot of up to four channels over the last WindowSeconds
     * of bus history (x = time within the window, y = value). Background quad,
     * grid, one polyline per bound channel, optional min/max/window labels. Y
     * range from the visible samples when AutoScaleY (padded 5 %), else
     * YMin..YMax. Non-finite samples are skipped; each channel is decimated to
     * at most 512 segments. Preview: a sine of PreviewAmplitude per channel.
     */
    struct COSMIC_API UiPlotComponent
    {
        std::string Channel;
        std::string Channel2;
        std::string Channel3;
        std::string Channel4;
        float       WindowSeconds = 10.0f;
        bool        AutoScaleY    = true;
        float       YMin = -1.0f;
        float       YMax =  1.0f;
        glm::vec4   LineColor { 0.3f, 0.8f, 1.0f, 1.0f };
        glm::vec4   LineColor2{ 1.0f, 0.6f, 0.2f, 1.0f };
        glm::vec4   LineColor3{ 0.6f, 1.0f, 0.4f, 1.0f };
        glm::vec4   LineColor4{ 1.0f, 0.4f, 0.8f, 1.0f };
        glm::vec4   GridColor      { 1.0f, 1.0f, 1.0f, 0.12f };
        glm::vec4   BackgroundColor{ 0.0f, 0.0f, 0.0f, 0.35f };
        int32_t     GridDivisions = 4;
        float       LineWidth     = 2.0f;                 // canvas px (scaled)
        bool        ShowLabels    = true;
        float       PreviewAmplitude = 1.0f;

        UiPlotComponent() = default;
        UiPlotComponent(const UiPlotComponent&) = default;
    };

    /**
     * @brief Writes a channel: press inside arms + sets, drag updates every
     * frame, release emits Signal (when non-empty) only if the value changed
     * since the press. Value = Min + t * (Max - Min), snapped to Step when
     * Step > 0. When not dragging the knob follows the bus (so the app's own
     * writes are reflected). Track + fill + knob, all Renderer2D quads.
     */
    struct COSMIC_API UiSliderComponent
    {
        std::string         Channel;                     // written with bus->Set
        float               Min  = 0.0f;
        float               Max  = 1.0f;
        float               Step = 0.0f;                 // 0 = continuous
        std::string         Signal;                      // emitted on release when changed; empty = none
        UiSliderOrientation Orientation = UiSliderOrientation::Horizontal;
        glm::vec4           TrackColor{ 0.15f, 0.15f, 0.18f, 1.0f };
        glm::vec4           FillColor { 0.3f,  0.6f,  1.0f,  1.0f };
        glm::vec4           KnobColor { 0.95f, 0.95f, 1.0f,  1.0f };
        float               KnobSize = 18.0f;            // canvas px (scaled)
        bool                Interactable = true;
        float               PreviewValue = 0.5f;

        // Runtime-only (not reflected): live drag state + the value at press time
        // (Signal fires on release only when the value differs from it).
        bool   Dragging       = false;
        double DragStartValue = 0.0;

        UiSliderComponent() = default;
        UiSliderComponent(const UiSliderComponent&) = default;
    };

    /**
     * @brief An image + this (like UiButton): the On/Off tint multiplies into
     * the sibling UiImage and, when set, the On/Off texture replaces its
     * texture. Release-inside flips bus->GetBool(Channel) and emits Signal.
     * Without a sibling UiImage the toggle draws its own quad.
     */
    struct COSMIC_API UiToggleComponent
    {
        std::string Channel;                             // bool written with bus->SetBool
        std::string Signal;                              // emitted on every flip; empty = none
        glm::vec4   OnTint { 0.2f, 1.0f, 0.3f, 1.0f };
        glm::vec4   OffTint{ 0.5f, 0.5f, 0.5f, 1.0f };
        std::string OnTexture;                           // AssetPath("texture"); empty => the image's own
        std::string OffTexture;
        bool        Interactable = true;
        bool        PreviewOn    = false;

        // Runtime-only (not reflected).
        bool           Armed = false;                    // the current press began on this toggle
        Ref<Texture2D> ResolvedOn,  ResolvedOff;
        std::string    ResolvedOnPath, ResolvedOffPath;

        UiToggleComponent() = default;
        UiToggleComponent(const UiToggleComponent&) = default;
    };

    /**
     * @brief Canvas-side half of a hosted ImGui panel (contract §4). Render draws
     * the frame (ShowFrame) and, in preview mode or while the host has not
     * reported a draw (DrawnThisFrame == false), the placeholder label
     * (PlaceholderText, empty = PanelName). The contents are drawn by the host:
     * UiSystem::CollectHostedPanels resolves the rects (and clears
     * DrawnThisFrame); the host sets DrawnThisFrame after PanelRegistry::Draw
     * returns true.
     */
    struct COSMIC_API UiHostedPanelComponent
    {
        std::string PanelName;
        bool        ShowFrame = true;
        glm::vec4   FrameColor{ 1.0f, 1.0f, 1.0f, 0.25f };
        std::string PlaceholderText;                     // empty = PanelName

        // Runtime-only (not reflected).
        bool DrawnThisFrame = false;

        UiHostedPanelComponent() = default;
        UiHostedPanelComponent(const UiHostedPanelComponent&) = default;
    };
}

// EnTT type-hash stabilization across the DLL boundary (see Components.h). Each
// expansion is a consteval full specialization → ODR-safe in every TU.
CS_REGISTER_COMPONENT(Cosmic::CanvasComponent)
CS_REGISTER_COMPONENT(Cosmic::RectTransformComponent)
CS_REGISTER_COMPONENT(Cosmic::UiImageComponent)
CS_REGISTER_COMPONENT(Cosmic::UiTextComponent)
CS_REGISTER_COMPONENT(Cosmic::UiButtonComponent)
CS_REGISTER_COMPONENT(Cosmic::UiWorldAnchorComponent)
// Bound widgets (AP-02, §3).
CS_REGISTER_COMPONENT(Cosmic::UiValueTextComponent)
CS_REGISTER_COMPONENT(Cosmic::UiGaugeComponent)
CS_REGISTER_COMPONENT(Cosmic::UiIndicatorComponent)
CS_REGISTER_COMPONENT(Cosmic::UiPlotComponent)
CS_REGISTER_COMPONENT(Cosmic::UiSliderComponent)
CS_REGISTER_COMPONENT(Cosmic::UiToggleComponent)
CS_REGISTER_COMPONENT(Cosmic::UiHostedPanelComponent)
