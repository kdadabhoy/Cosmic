// scene/ui/UiSystem.cpp — in-game UI runtime (Phase 17 / U1).

#include "scene/ui/UiSystem.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"          // RelationshipComponent (E3 hierarchy)
#include "scene/EventBus.h"

#include "renderer/Renderer2D.h"
#include "renderer/RenderCommand.h"
#include "graphics/Font.h"
#include "graphics/SubTexture2D.h"
#include "assets/AssetLibrary.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <sstream>

namespace Cosmic
{
    // ========================================================================
    // Pure layout / interaction
    // ========================================================================

    UiRect UiSystem::ResolveRect(const UiRect& parent, const RectTransformComponent& rt, float scale)
    {
        const glm::vec2 psize = parent.Size();
        const glm::vec2 anchorMinPt = parent.Min + psize * rt.AnchorMin;
        const glm::vec2 anchorMaxPt = parent.Min + psize * rt.AnchorMax;

        UiRect out;
        out.Min = anchorMinPt + rt.OffsetMin * scale;
        out.Max = anchorMaxPt + rt.OffsetMax * scale;
        return out;
    }

    glm::vec2 UiSystem::PivotPoint(const UiRect& rect, const glm::vec2& pivot)
    {
        return rect.Min + rect.Size() * pivot;
    }

    float UiSystem::CanvasScale(const CanvasComponent& canvas, const UiRect& viewport)
    {
        if (canvas.ScaleMode == UiScaleMode::ConstantPixel) return 1.0f;
        const float refH = (canvas.ReferenceHeight > 1.0f) ? canvas.ReferenceHeight : 1.0f;
        return viewport.Height() / refH;
    }

    ButtonStep UiSystem::StepButtonState(UiButtonState /*prev*/, bool armedPrev,
                                         bool interactable, bool hovered,
                                         bool pressedEdge, bool releasedEdge, bool down)
    {
        ButtonStep s;
        if (!interactable)
        {
            s.State = UiButtonState::Disabled;
            s.Armed = false;
            s.Emit  = false;
            return s;
        }

        bool armed = armedPrev;
        if (pressedEdge && hovered) armed = true;

        if (releasedEdge)
        {
            if (armed && hovered) s.Emit = true;   // release began + ended on the button
            armed = false;
        }
        if (!down) armed = false;                  // e.g. mouse released off-window

        s.Armed = armed;
        if (armed && hovered && down) s.State = UiButtonState::Pressed;
        else if (hovered)             s.State = UiButtonState::Hover;
        else                          s.State = UiButtonState::Normal;
        return s;
    }

    // ========================================================================
    // Scene traversal
    // ========================================================================

    namespace
    {
        void VisitUi(Scene& scene, entt::entity node, const UiRect& parentRect,
                     bool isCanvasRoot, float scale, int32_t canvasOrder,
                     int32_t& seq, std::vector<UiElement>& out)
        {
            auto& reg = scene.GetRegistry();

            UiRect rect = parentRect;
            int32_t z = 0;
            if (!isCanvasRoot)
            {
                if (auto* rt = reg.try_get<RectTransformComponent>(node))
                {
                    rect = UiSystem::ResolveRect(parentRect, *rt, scale);
                    z = rt->ZOrder;
                }
            }

            // Add as a drawable/interactive element if it carries any UI content.
            const bool drawable = reg.any_of<UiImageComponent, UiTextComponent, UiButtonComponent>(node);
            if (drawable)
            {
                UiElement el;
                el.Handle      = static_cast<uint32_t>(node);
                el.Rect        = rect;
                el.Scale       = scale;
                el.CanvasOrder = canvasOrder;
                el.ZOrder      = z;
                el.Seq         = seq++;
                out.push_back(el);
            }

            // Recurse into hierarchy children (E3 UUID links, authored order).
            if (auto* rel = reg.try_get<RelationshipComponent>(node))
            {
                for (UUID childId : rel->Children)
                {
                    Entity child = scene.FindByUUID(childId);
                    if (!child) continue;
                    VisitUi(scene, static_cast<entt::entity>(child), rect, false,
                            scale, canvasOrder, seq, out);
                }
            }
        }
    }

    void UiSystem::CollectElements(Scene& scene, const UiRect& viewport,
                                   std::vector<UiElement>& out)
    {
        out.clear();
        auto& reg = scene.GetRegistry();

        struct CanvasEntry { entt::entity Handle; int32_t Order; };
        std::vector<CanvasEntry> canvases;
        for (auto e : reg.view<CanvasComponent>())
        {
            if (!scene.IsActiveInHierarchy(e))   // T13 — inactive canvas (or ancestor): skip
                continue;
            canvases.push_back({ e, reg.get<CanvasComponent>(e).SortOrder });
        }

        std::stable_sort(canvases.begin(), canvases.end(),
                         [](const CanvasEntry& a, const CanvasEntry& b) { return a.Order < b.Order; });

        int32_t seq = 0;
        for (const CanvasEntry& c : canvases)
        {
            const CanvasComponent& canvas = reg.get<CanvasComponent>(c.Handle);
            const float scale = CanvasScale(canvas, viewport);
            VisitUi(scene, c.Handle, viewport, /*isCanvasRoot*/ true, scale, c.Order, seq, out);
        }

        // Back-to-front draw order: ascending CanvasOrder, then ZOrder, then Seq.
        std::stable_sort(out.begin(), out.end(), [](const UiElement& a, const UiElement& b)
        {
            if (a.CanvasOrder != b.CanvasOrder) return a.CanvasOrder < b.CanvasOrder;
            if (a.ZOrder      != b.ZOrder)      return a.ZOrder      < b.ZOrder;
            return a.Seq < b.Seq;
        });
    }

    // ========================================================================
    // Update (interaction)
    // ========================================================================

    bool UiSystem::Update(Scene& scene, const UiRect& viewport, const UiPointer& pointer)
    {
        std::vector<UiElement> elements;
        CollectElements(scene, viewport, elements);

        auto& reg = scene.GetRegistry();

        // Topmost interactable button under the pointer (front-to-back).
        entt::entity topHit = entt::null;
        for (auto it = elements.rbegin(); it != elements.rend(); ++it)
        {
            const entt::entity e = static_cast<entt::entity>(it->Handle);
            auto* btn = reg.try_get<UiButtonComponent>(e);
            if (!btn || !btn->Interactable) continue;
            if (it->Rect.Contains(pointer.Position)) { topHit = e; break; }
        }

        // Step every button; emit on release-inside.
        for (const UiElement& el : elements)
        {
            const entt::entity e = static_cast<entt::entity>(el.Handle);
            auto* btn = reg.try_get<UiButtonComponent>(e);
            if (!btn) continue;

            const bool hovered = (e == topHit);
            ButtonStep step = StepButtonState(btn->State, btn->Armed, btn->Interactable,
                                              hovered, pointer.PressedEdge, pointer.ReleasedEdge,
                                              pointer.Down);
            btn->State = step.State;
            btn->Armed = step.Armed;
            if (step.Emit)
                scene.Events().Emit(btn->Signal, Entity(e, &scene));
        }

        return topHit != entt::null;
    }

    bool UiSystem::HitTest(Scene& scene, const UiRect& viewport, const glm::vec2& point,
                           uint32_t& outEntity)
    {
        std::vector<UiElement> elements;
        CollectElements(scene, viewport, elements);

        // Front-to-back: the list is back-to-front draw order, so walk it reversed.
        for (auto it = elements.rbegin(); it != elements.rend(); ++it)
        {
            if (it->Rect.Contains(point))
            {
                outEntity = it->Handle;
                return true;
            }
        }
        return false;
    }

    // ========================================================================
    // Render (GL)
    // ========================================================================

    namespace
    {
        void ResolveImage(UiImageComponent& img)
        {
            if (img.TexturePath == img.ResolvedPath) return;
            img.ResolvedPath = img.TexturePath;
            img.Resolved = img.TexturePath.empty() ? nullptr
                                                    : AssetLibrary::GetTexture(img.TexturePath);
        }

        void ResolveFont(UiTextComponent& txt)
        {
            if (txt.ResolvedFont && txt.ResolvedFontPath == txt.FontPath) return;
            txt.ResolvedFontPath = txt.FontPath;
            txt.ResolvedFont = txt.FontPath.empty() ? Font::Default() : Font::Get(txt.FontPath);
            if (!txt.ResolvedFont) txt.ResolvedFont = Font::Default();
        }

        // Width of one line in EM units (glyph advances + kerning).
        float MeasureLineEm(const Font& font, const std::string& line)
        {
            float w = 0.0f;
            for (unsigned char c : line)
            {
                const Glyph* g = font.GetGlyph(c);
                if (!g) g = font.GetGlyph((uint32_t)'?');
                if (g) w += g->advance;
            }
            return w;
        }

        void DrawImageQuad(const UiRect& rect, const Ref<Texture2D>& tex,
                           const glm::vec4& tint, const glm::vec4& nineSlice,
                           bool preserveAspect, float scale)
        {
            glm::vec2 center = rect.Center();
            glm::vec2 size   = rect.Size();

            if (!tex)
            {
                Renderer2D::DrawQuad(glm::vec3(center, 0.0f), size, tint);
                return;
            }

            // 9-slice: corners fixed (border*scale px), edges + center stretch.
            const float l = nineSlice.x, t = nineSlice.y, r = nineSlice.z, b = nineSlice.w;
            if (l > 0.0f || t > 0.0f || r > 0.0f || b > 0.0f)
            {
                const float tw = (float)tex->GetWidth();
                const float th = (float)tex->GetHeight();
                if (tw > 0.0f && th > 0.0f)
                {
                    // Destination column/row edges (canvas px), clamped so borders
                    // never overlap in a small rect.
                    float lp = l * scale, rp = r * scale, tp = t * scale, bp = b * scale;
                    if (lp + rp > size.x) { float k = size.x / (lp + rp); lp *= k; rp *= k; }
                    if (tp + bp > size.y) { float k = size.y / (tp + bp); tp *= k; bp *= k; }

                    const float xs[4] = { rect.Min.x, rect.Min.x + lp, rect.Max.x - rp, rect.Max.x };
                    const float ys[4] = { rect.Min.y, rect.Min.y + tp, rect.Max.y - bp, rect.Max.y };
                    // Source UV edges (top-left origin; SubTexture2D UV is bottom-left,
                    // so V is flipped: v = 1 - y/th).
                    const float us[4] = { 0.0f, l / tw, 1.0f - r / tw, 1.0f };
                    const float vs[4] = { 1.0f, 1.0f - t / th, b / th, 0.0f };

                    for (int row = 0; row < 3; ++row)
                        for (int col = 0; col < 3; ++col)
                        {
                            const float cx = (xs[col] + xs[col + 1]) * 0.5f;
                            const float cy = (ys[row] + ys[row + 1]) * 0.5f;
                            const glm::vec2 cellSize = { xs[col + 1] - xs[col], ys[row + 1] - ys[row] };
                            if (cellSize.x <= 0.0f || cellSize.y <= 0.0f) continue;
                            const glm::vec2 uvMin = { us[col],     vs[row + 1] };
                            const glm::vec2 uvMax = { us[col + 1], vs[row]     };
                            auto sub = CreateRef<SubTexture2D>(tex, uvMin, uvMax);
                            Renderer2D::DrawQuad(glm::vec3(cx, cy, 0.0f), cellSize, sub, tint);
                        }
                    return;
                }
            }

            if (preserveAspect)
            {
                const float tw = (float)tex->GetWidth();
                const float th = (float)tex->GetHeight();
                if (tw > 0.0f && th > 0.0f)
                {
                    const float ar = tw / th;
                    const float rar = size.x / size.y;
                    if (ar > rar) size.y = size.x / ar;   // letterbox vertically
                    else          size.x = size.y * ar;   // pillarbox horizontally
                }
            }

            Renderer2D::DrawQuad(glm::vec3(center, 0.0f), size, tex, 1.0f, tint);
        }

        void DrawTextInRect(UiTextComponent& txt, const UiRect& rect, float scale)
        {
            ResolveFont(txt);
            const Ref<Font>& font = txt.ResolvedFont;
            if (!font || txt.Text.empty()) return;

            const float pixelSize = txt.SizePx * scale;
            const float lineEm    = font->LineHeight();

            // Split into lines (v1: honor explicit '\n'; wrap is a follow-up).
            std::vector<std::string> lines;
            {
                std::stringstream ss(txt.Text);
                std::string line;
                while (std::getline(ss, line))
                {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    lines.push_back(line);
                }
                if (lines.empty()) lines.push_back("");
            }

            const float blockH = lines.size() * lineEm * pixelSize;
            float topY;
            switch (txt.VAlign)
            {
                case UiVAlign::Top:    topY = rect.Min.y; break;
                case UiVAlign::Bottom: topY = rect.Max.y - blockH; break;
                case UiVAlign::Middle:
                default:               topY = rect.Center().y - blockH * 0.5f; break;
            }

            const float ascent = font->Ascent();
            for (size_t i = 0; i < lines.size(); ++i)
            {
                const std::string& ln = lines[i];
                const float lineW = MeasureLineEm(*font, ln) * pixelSize;
                float x;
                switch (txt.HAlign)
                {
                    case UiHAlign::Left:   x = rect.Min.x; break;
                    case UiHAlign::Right:  x = rect.Max.x - lineW; break;
                    case UiHAlign::Center:
                    default:               x = rect.Center().x - lineW * 0.5f; break;
                }
                // Baseline of this line (top-of-line + ascent), y-down.
                const float baselineY = topY + i * lineEm * pixelSize + ascent * pixelSize;

                // DrawString expects em coords, baseline at origin, y-UP; a negative
                // Y scale flips it into our y-down canvas space.
                glm::mat4 transform = glm::translate(glm::mat4(1.0f), { x, baselineY, 0.0f })
                                    * glm::scale(glm::mat4(1.0f), { pixelSize, -pixelSize, 1.0f });
                Renderer2D::DrawString(ln, font, transform, txt.Color);
            }
        }
    }

    void UiSystem::Render(Scene& scene, const UiRect& viewport)
    {
        // Projection spans the layout rect itself (the classic full-target case).
        Render(scene, viewport,
               (uint32_t)std::max(1.0f, viewport.Width()),
               (uint32_t)std::max(1.0f, viewport.Height()));
    }

    void UiSystem::Render(Scene& scene, const UiRect& canvasRect,
                          uint32_t targetW, uint32_t targetH)
    {
        std::vector<UiElement> elements;
        CollectElements(scene, canvasRect, elements);
        if (elements.empty()) return;

        auto& reg = scene.GetRegistry();

        const uint32_t w = std::max(1u, targetW);
        const uint32_t h = std::max(1u, targetH);

        // Screen-space ortho over the FULL target (top-left origin, +y DOWN).
        // Elements were resolved against canvasRect, so their absolute coords
        // already sit inside the band — the identity pixel mapping places them
        // there. canvasRect == {0,0,w,h} degenerates to the classic case.
        glm::mat4 proj = glm::ortho(0.0f, (float)w, (float)h, 0.0f, -1.0f, 1.0f);

        // UI is a painter-ordered overlay: no depth test/write, straight alpha.
        RenderCommand::SetDepthTest(false);
        RenderCommand::SetDepthWrite(false);
        RenderCommand::SetBlendMode(RendererAPI::BlendMode::Alpha);

        Renderer2D::PushRenderPass(proj, { 0.0f, 0.0f, (float)w, (float)h });

        for (const UiElement& el : elements)
        {
            const entt::entity e = static_cast<entt::entity>(el.Handle);

            if (auto* img = reg.try_get<UiImageComponent>(e))
            {
                ResolveImage(*img);
                glm::vec4 tint = img->Tint;
                if (auto* btn = reg.try_get<UiButtonComponent>(e))
                {
                    switch (btn->State)
                    {
                        case UiButtonState::Hover:    tint *= btn->HoverTint;    break;
                        case UiButtonState::Pressed:  tint *= btn->PressedTint;  break;
                        case UiButtonState::Disabled: tint *= btn->DisabledTint; break;
                        case UiButtonState::Normal:
                        default:                      tint *= btn->NormalTint;   break;
                    }
                }
                DrawImageQuad(el.Rect, img->Resolved, tint, img->NineSlice,
                              img->PreserveAspect, el.Scale);
            }

            if (auto* txt = reg.try_get<UiTextComponent>(e))
                DrawTextInRect(*txt, el.Rect, el.Scale);
        }

        Renderer2D::PopRenderPass();

        // Restore engine render-state defaults (depth ON/ON, alpha blend).
        RenderCommand::SetDepthWrite(true);
        RenderCommand::SetDepthTest(true);
        RenderCommand::SetBlendMode(RendererAPI::BlendMode::Alpha);
    }
}
