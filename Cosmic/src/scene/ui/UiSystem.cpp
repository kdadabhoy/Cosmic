// scene/ui/UiSystem.cpp — in-game UI runtime (Phase 17 / U1).

#include "scene/ui/UiSystem.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"          // RelationshipComponent (E3 hierarchy)
#include "scene/EventBus.h"
#include "core/Log.h"                  // WO-09 — the depth-ceiling warning
#include "data/DataBus.h"              // AP-02 — bound widgets read/write the host bus

#include "renderer/Renderer2D.h"
#include "renderer/RenderCommand.h"
#include "graphics/Font.h"
#include "graphics/SubTexture2D.h"
#include "assets/AssetLibrary.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <unordered_set>   // WO-09 — the iterative canvas walk's visited set
#include <sstream>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <limits>

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

    bool UiSystem::ProjectToCanvas(const glm::vec3& worldPos, const glm::mat4& viewProj,
                                   const UiRect& canvasRect, glm::vec2& outPoint)
    {
        const glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 1e-6f)
            return false;                             // behind the camera / on the plane
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;   // [-1,1]^3
        const float u = ndc.x * 0.5f + 0.5f;          // 0 left  -> 1 right
        const float v = 1.0f - (ndc.y * 0.5f + 0.5f); // 0 top   -> 1 bottom (+y DOWN)
        outPoint = canvasRect.Min + glm::vec2(u, v) * canvasRect.Size();
        return true;
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
    // Bound-widget helpers (AP-02, §3) — pure
    // ========================================================================

    namespace
    {
        // Classify a printf format: returns the number of conversions and, when
        // exactly one, its conversion character (with `%%` not counted). Any
        // conversion that is not numeric (s, c, p, n, ...) or is malformed makes
        // the format unusable (reported as -1 conversions).
        int CountNumericConversions(const std::string& fmt, char& conv)
        {
            int n = 0;
            conv = 0;
            for (size_t i = 0; i < fmt.size(); ++i)
            {
                if (fmt[i] != '%') continue;
                if (i + 1 < fmt.size() && fmt[i + 1] == '%') { ++i; continue; }   // literal percent
                size_t j = i + 1;
                while (j < fmt.size() && std::strchr("-+ #0", fmt[j])) ++j;       // flags
                while (j < fmt.size() && std::isdigit((unsigned char)fmt[j])) ++j; // width
                if (j < fmt.size() && fmt[j] == '.')                                // precision
                {
                    ++j;
                    while (j < fmt.size() && std::isdigit((unsigned char)fmt[j])) ++j;
                }
                if (j >= fmt.size()) return -1;                                     // dangling '%'
                const char c = fmt[j];
                if (!std::strchr("diuoxXfFeEgGaA", c)) return -1;                   // '*', length modifiers, s/c/p/n: literal
                ++n;
                conv = c;
                i = j;
            }
            return n;
        }

        std::string FormatNumber(const std::string& fmt, char conv, double v)
        {
            char buf[256];
            int written = -1;
            if (std::strchr("di", conv))
            {
                // Integer conversions take the truncated value through %lld: passing a
                // double to %d is undefined behaviour, so the spec is rewritten.
                std::string f = fmt;
                const size_t pos = f.rfind(conv);
                f.replace(pos, 1, "lld");
                const double t = std::isfinite(v) ? std::trunc(v) : 0.0;
                written = std::snprintf(buf, sizeof(buf), f.c_str(), (long long)t);
            }
            else if (std::strchr("uoxX", conv))
            {
                std::string f = fmt;
                const size_t pos = f.rfind(conv);
                f.insert(pos, "ll");
                const double t = std::isfinite(v) && v > 0.0 ? std::trunc(v) : 0.0;
                written = std::snprintf(buf, sizeof(buf), f.c_str(), (unsigned long long)t);
            }
            else
            {
                written = std::snprintf(buf, sizeof(buf), fmt.c_str(), v);
            }
            if (written < 0) return fmt;
            return std::string(buf, (size_t)std::min<int>(written, (int)sizeof(buf) - 1));
        }
    }

    std::string UiSystem::FormatValue(const UiValueTextComponent& comp, const DataValue* value, bool stale)
    {
        (void)stale;   // the colour carries staleness; the text is identical (pinned by V04)
        std::string body;
        if (!value)
        {
            body = comp.Placeholder;
        }
        else if (value->ValueKind == DataValue::Kind::Bool)
        {
            body = value->Bool ? "true" : "false";
        }
        else if (value->ValueKind == DataValue::Kind::String)
        {
            body = value->AsString();
        }
        else
        {
            const double v = value->Number;
            if (!std::isfinite(v))
            {
                body = comp.Placeholder;   // channels are data; a NaN/inf shows the placeholder (§1)
            }
            else
            {
                char conv = 0;
                const int n = CountNumericConversions(comp.Format, conv);
                body = (n == 1) ? FormatNumber(comp.Format, conv, v) : comp.Format;
            }
        }
        return comp.Prefix + body + comp.Suffix;
    }

    float UiSystem::GaugeFill(float min, float max, double value)
    {
        if (!std::isfinite(value)) return 0.0f;
        if (max == min) return value >= (double)max ? 1.0f : 0.0f;
        const double t = (value - (double)min) / ((double)max - (double)min);
        if (!(t > 0.0)) return 0.0f;              // also catches a NaN from inf ranges
        return t >= 1.0 ? 1.0f : (float)t;
    }

    double UiSystem::SliderValueAt(const UiRect& rect, UiSliderOrientation orientation,
                                   const glm::vec2& p, float min, float max, float step)
    {
        double t = 0.0;
        if (orientation == UiSliderOrientation::Vertical)
        {
            const double h = (double)rect.Height();
            t = h > 0.0 ? ((double)rect.Max.y - (double)p.y) / h : 0.0;   // bottom = min, top = max
        }
        else
        {
            const double w = (double)rect.Width();
            t = w > 0.0 ? ((double)p.x - (double)rect.Min.x) / w : 0.0;
        }
        if (!(t > 0.0)) t = 0.0;
        if (t > 1.0)    t = 1.0;

        double v = (double)min + t * ((double)max - (double)min);
        if (step > 0.0f && std::isfinite(step))
            v = (double)min + std::round((v - (double)min) / (double)step) * (double)step;

        const double lo = std::min((double)min, (double)max);
        const double hi = std::max((double)min, (double)max);
        return std::min(std::max(v, lo), hi);
    }

    UiRect UiSystem::SliderKnobRect(const UiRect& rect, UiSliderOrientation orientation,
                                    float knobSizePx, float min, float max, double value)
    {
        // KI-65: the draw's geometry, verbatim — DrawSlider calls this too.
        const float knob = std::max(2.0f, knobSizePx);
        const float t    = GaugeFill(min, max, value);
        const glm::vec2 c = rect.Center();
        glm::vec2 center;
        if (orientation == UiSliderOrientation::Vertical)
            center = { c.x, rect.Max.y - t * rect.Height() };
        else
            center = { rect.Min.x + t * rect.Width(), c.y };
        const glm::vec2 half{ knob * 0.5f, knob * 0.5f };
        return UiRect{ center - half, center + half };
    }

    // ========================================================================
    // Scene traversal
    // ========================================================================

    namespace
    {
        // Preorder walk of one canvas subtree (parent, then children in authored
        // order). Iterative (WO-09 / KI-42): an explicit stack bounded by
        // Scene::kMaxHierarchyDepth per path and a per-walk visited set, so a
        // legally deep hierarchy can never overflow the C++ stack and a hand-authored
        // Children cycle is entered once and left. Elements below the ceiling are
        // omitted (warned once per walk); the Seq numbering is exactly the order the
        // recursive walk produced.
        void VisitUi(Scene& scene, entt::entity root, const UiRect& rootRect,
                     float scale, int32_t canvasOrder,
                     int32_t& seq, std::vector<UiElement>& out,
                     const UiRect& canvasRect, const glm::mat4* cameraVP)
        {
            auto& reg = scene.GetRegistry();

            struct Frame { entt::entity Node; UiRect ParentRect; int Depth; bool IsCanvasRoot; };
            std::vector<Frame> work{ { root, rootRect, 0, true } };
            std::unordered_set<entt::entity> visited;
            bool warned = false;
            while (!work.empty())
            {
                const Frame f = work.back();
                work.pop_back();
                const entt::entity node = f.Node;
                if (!reg.valid(node) || !visited.insert(node).second)
                    continue;

                UiRect rect = f.ParentRect;
                int32_t z = 0;
                if (!f.IsCanvasRoot)
                {
                    // X6 — world-anchored: project the tracked world point into canvas
                    // space and use it (plus ScreenOffset) as a zero-size parent origin,
                    // so the RectTransform's offsets size the box around it. Behind the
                    // camera / off-screen ⇒ hide this element AND its subtree.
                    UiRect effectiveParent = f.ParentRect;
                    if (auto* anchor = reg.try_get<UiWorldAnchorComponent>(node); anchor && cameraVP)
                    {
                        glm::vec3 worldPos = anchor->WorldOffset;
                        if (anchor->TargetEntity != 0)
                            if (Entity target = scene.FindByUUID(UUID(anchor->TargetEntity)))
                                worldPos += glm::vec3(scene.GetWorldTransform(target)[3]);

                        glm::vec2 pt;
                        if (!UiSystem::ProjectToCanvas(worldPos, *cameraVP, canvasRect, pt))
                            continue;                            // behind camera -> hidden
                        pt += anchor->ScreenOffset;
                        if (anchor->HideWhenOffscreen && !canvasRect.Contains(pt))
                            continue;                            // off-screen -> hidden
                        effectiveParent = UiRect{ pt, pt };      // zero-size origin at the point
                    }

                    if (auto* rt = reg.try_get<RectTransformComponent>(node))
                    {
                        rect = UiSystem::ResolveRect(effectiveParent, *rt, scale);
                        z = rt->ZOrder;
                    }
                    else
                    {
                        rect = effectiveParent;
                    }
                }

                // Add as a drawable/interactive element if it carries any UI content.
                const bool drawable = reg.any_of<UiImageComponent, UiTextComponent, UiButtonComponent,
                                                 // AP-02 bound widgets + the hosted-panel frame
                                                 UiGaugeComponent, UiIndicatorComponent, UiPlotComponent,
                                                 UiSliderComponent, UiToggleComponent, UiHostedPanelComponent>(node);
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

                // Children (E3 UUID links, authored order): pushed in reverse so the
                // first child is visited next.
                if (f.Depth + 1 >= Scene::kMaxHierarchyDepth)
                {
                    if (!warned)
                    {
                        warned = true;
                        CS_CORE_WARN("UiSystem: canvas hierarchy deeper than {0} levels — deeper elements are not laid out.",
                                     Scene::kMaxHierarchyDepth);
                    }
                    continue;
                }
                if (auto* rel = reg.try_get<RelationshipComponent>(node))
                {
                    for (auto c = rel->Children.rbegin(); c != rel->Children.rend(); ++c)
                        if (Entity child = scene.FindByUUID(*c))
                            work.push_back({ static_cast<entt::entity>(child), rect, f.Depth + 1, false });
                }
            }
        }
    }

    void UiSystem::CollectElements(Scene& scene, const UiRect& viewport,
                                   std::vector<UiElement>& out,
                                   const glm::mat4* cameraViewProj)
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
            VisitUi(scene, c.Handle, viewport, scale, c.Order, seq, out, viewport, cameraViewProj);
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

    bool UiSystem::Update(Scene& scene, const UiRect& viewport, const UiPointer& pointer,
                          const glm::mat4* cameraViewProj, DataBus* bus)
    {
        std::vector<UiElement> elements;
        CollectElements(scene, viewport, elements, cameraViewProj);

        auto& reg = scene.GetRegistry();

        // Topmost interactable button / slider / toggle under the pointer (front-
        // to-back). A slider mid-drag keeps the pointer (the drag may leave its
        // rect and still steer it), so it wins regardless of what is under the
        // pointer now.
        entt::entity topHit = entt::null;
        for (auto it = elements.rbegin(); it != elements.rend(); ++it)
        {
            const entt::entity e = static_cast<entt::entity>(it->Handle);
            if (auto* sl = reg.try_get<UiSliderComponent>(e); sl && sl->Interactable && sl->Dragging)
            {
                topHit = e;
                break;
            }
        }
        if (topHit == entt::null)
        {
            for (auto it = elements.rbegin(); it != elements.rend(); ++it)
            {
                const entt::entity e = static_cast<entt::entity>(it->Handle);
                bool interactable = false;
                if (auto* btn = reg.try_get<UiButtonComponent>(e); btn && btn->Interactable) interactable = true;
                bool hit = it->Rect.Contains(pointer.Position);
                if (auto* sl  = reg.try_get<UiSliderComponent>(e); sl  && sl->Interactable)
                {
                    interactable = true;
                    // KI-65: the drawn knob is a grab target even where it overhangs the
                    // rect — inside the rect OR inside the knob (two rects, deliberately not
                    // their bounding box: the empty overhang beside the knob is not a target).
                    // The knob sits where the DRAW puts it: at the bus value (Min when the
                    // channel is absent, like DrawSlider's live mode); without a bus the
                    // preview value, which is what a bus-less host draws.
                    const double shown = bus ? (bus->Has(sl->Channel) ? bus->GetNumber(sl->Channel, (double)sl->Min)
                                                                      : (double)sl->Min)
                                             : (double)sl->PreviewValue;
                    const UiRect knob = SliderKnobRect(it->Rect, sl->Orientation, sl->KnobSize * it->Scale,
                                                       sl->Min, sl->Max, shown);
                    hit = hit || knob.Contains(pointer.Position);
                }
                if (auto* tg  = reg.try_get<UiToggleComponent>(e); tg  && tg->Interactable)  interactable = true;
                if (!interactable) continue;
                if (hit) { topHit = e; break; }
            }
        }

        // Step every button; emit on release-inside. (Unchanged from U1: the only
        // difference a pre-AP-02 scene can observe is none — it has no sliders or
        // toggles to take the hit.)
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

        // Sliders (AP-02): press inside arms + writes; every frame while dragging
        // writes; release ends the drag and emits Signal only when the value moved
        // from where the press found it. Without a bus there is nothing to write
        // or compare, so a slider is hit-tested (it still blocks scene picking)
        // but never drags.
        for (const UiElement& el : elements)
        {
            const entt::entity e = static_cast<entt::entity>(el.Handle);
            auto* sl = reg.try_get<UiSliderComponent>(e);
            if (!sl) continue;
            if (!sl->Interactable || !bus)
            {
                sl->Dragging = false;
                continue;
            }

            const bool hovered = (e == topHit);
            if (!sl->Dragging)
            {
                if (hovered && pointer.PressedEdge && pointer.Down)
                {
                    sl->Dragging       = true;
                    sl->DragStartValue = bus->Has(sl->Channel) ? bus->GetNumber(sl->Channel, (double)sl->Min)
                                                               : std::numeric_limits<double>::quiet_NaN();
                }
                else
                {
                    continue;
                }
            }

            // Dragging (possibly since this very frame): steer + write.
            const double v = SliderValueAt(el.Rect, sl->Orientation, pointer.Position, sl->Min, sl->Max, sl->Step);
            if (!sl->Channel.empty())
                bus->Set(sl->Channel, v);

            if (pointer.ReleasedEdge || !pointer.Down)
            {
                sl->Dragging = false;
                const bool changed = !(v == sl->DragStartValue);   // a NaN start (missing channel) counts as changed
                if (changed && !sl->Signal.empty())
                    scene.Events().Emit(sl->Signal, Entity(e, &scene));
            }
        }

        // Toggles (AP-02): the button machine decides release-inside; the flip
        // writes the bus and emits Signal. Without a bus there is no state to flip.
        for (const UiElement& el : elements)
        {
            const entt::entity e = static_cast<entt::entity>(el.Handle);
            auto* tg = reg.try_get<UiToggleComponent>(e);
            if (!tg) continue;

            const bool hovered = (e == topHit);
            ButtonStep step = StepButtonState(UiButtonState::Normal, tg->Armed, tg->Interactable,
                                              hovered, pointer.PressedEdge, pointer.ReleasedEdge,
                                              pointer.Down);
            tg->Armed = step.Armed;
            if (step.Emit && bus)
            {
                const bool cur = bus->GetBool(tg->Channel, false);
                if (!tg->Channel.empty())
                    bus->SetBool(tg->Channel, !cur);
                if (!tg->Signal.empty())
                    scene.Events().Emit(tg->Signal, Entity(e, &scene));
            }
        }

        return topHit != entt::null;
    }

    bool UiSystem::HitTest(Scene& scene, const UiRect& viewport, const glm::vec2& point,
                           uint32_t& outEntity, const glm::mat4* cameraViewProj)
    {
        std::vector<UiElement> elements;
        CollectElements(scene, viewport, elements, cameraViewProj);

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
                    // so V is flipped: v = 1 - y/th). The canvas projection is +y DOWN,
                    // which puts the quad corner carrying uvMin at the cell's screen
                    // TOP — so the top band gets uvMin.v = vs[row] (the higher v), not
                    // vs[row + 1], or every band renders upside-down (WO-08 KI-37).
                    const float us[4] = { 0.0f, l / tw, 1.0f - r / tw, 1.0f };
                    const float vs[4] = { 1.0f, 1.0f - t / th, b / th, 0.0f };

                    for (int row = 0; row < 3; ++row)
                        for (int col = 0; col < 3; ++col)
                        {
                            const float cx = (xs[col] + xs[col + 1]) * 0.5f;
                            const float cy = (ys[row] + ys[row + 1]) * 0.5f;
                            const glm::vec2 cellSize = { xs[col + 1] - xs[col], ys[row + 1] - ys[row] };
                            if (cellSize.x <= 0.0f || cellSize.y <= 0.0f) continue;
                            const glm::vec2 uvMin = { us[col],     vs[row]     };
                            const glm::vec2 uvMax = { us[col + 1], vs[row + 1] };
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

            // Under the +y-DOWN canvas projection the plain textured quad puts v = 0
            // at the rect's top, i.e. the image upside-down (files are loaded with
            // v = 1 as their top row; an FBO's row 0 is its bottom). Route through a
            // sub-texture whose V runs 1 -> 0 from the screen top down (WO-08 KI-37).
            auto upright = CreateRef<SubTexture2D>(tex, glm::vec2{ 0.0f, 1.0f }, glm::vec2{ 1.0f, 0.0f });
            Renderer2D::DrawQuad(glm::vec3(center, 0.0f), size, upright, tint);
        }

        // `text` is what gets drawn (UiText.Text, or a UiValueText's resolved string);
        // `color` likewise (UiText.Color, or the StaleColor). txt supplies font, size
        // and alignment and is never written except for its lazily resolved font.
        void DrawTextInRect(UiTextComponent& txt, const UiRect& rect, float scale,
                            const std::string& text, const glm::vec4& color)
        {
            ResolveFont(txt);
            const Ref<Font>& font = txt.ResolvedFont;
            if (!font || text.empty()) return;

            const float pixelSize = txt.SizePx * scale;
            const float lineEm    = font->LineHeight();

            // Split into lines (v1: honor explicit '\n'; wrap is a follow-up).
            std::vector<std::string> lines;
            {
                std::stringstream ss(text);
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
                Renderer2D::DrawString(ln, font, transform, color);
            }
        }

        // ====================================================================
        // Bound widgets (AP-02, §3) — every draw is a Renderer2D verb.
        // ====================================================================

        // Live mode = a bus is present and the caller did not ask for preview.
        // Preview mode substitutes the Preview* fields, EXCEPT when a bus is present
        // and holds the channel (so E05's preview-vs-live pair is byte-identical).
        struct WidgetCtx
        {
            const DataBus* Bus     = nullptr;
            bool           Preview = true;

            bool Live() const { return Bus && !Preview; }

            // Numeric read: returns false when the widget should show its
            // "missing" state (live mode, channel absent). `out` receives the
            // bus value or the preview value.
            bool Number(const std::string& channel, float previewValue, double& out) const
            {
                if (Bus && !channel.empty() && Bus->Has(channel)) { out = Bus->GetNumber(channel, 0.0); return true; }
                if (Live()) { out = 0.0; return false; }
                out = (double)previewValue;
                return true;
            }
            bool Flag(const std::string& channel, bool previewOn) const
            {
                if (Bus && !channel.empty() && Bus->Has(channel)) return Bus->GetBool(channel, false);
                if (Live()) return false;
                return previewOn;
            }
        };

        // Axis-aligned filled rect (canvas px) as one quad.
        inline void FillRect(const UiRect& r, const glm::vec4& color)
        {
            const glm::vec2 size = r.Size();
            if (size.x <= 0.0f || size.y <= 0.0f) return;
            Renderer2D::DrawQuad(glm::vec3(r.Center(), 0.0f), size, color);
        }

        // A thick segment as a rotated quad (GL line width is not available to the
        // batch; quads also keep painter order with the widget's own background).
        inline void ThickSegment(const glm::vec2& a, const glm::vec2& b, float thickness, const glm::vec4& color)
        {
            const glm::vec2 d = b - a;
            const float len = glm::length(d);
            if (!(len > 0.0f) || !std::isfinite(len)) return;
            const float angle = std::atan2(d.y, d.x);
            // Extend by half the thickness at both ends so consecutive segments join.
            Renderer2D::DrawRotatedQuad(glm::vec3((a + b) * 0.5f, 0.0f), { len + thickness, thickness }, angle, color);
        }

        // 1-px (or `px` wide) outline of a rect as four quads.
        inline void StrokeRect(const UiRect& r, float px, const glm::vec4& color)
        {
            if (r.Width() <= 0.0f || r.Height() <= 0.0f) return;
            const float t = std::max(1.0f, px);
            FillRect({ r.Min,                         { r.Max.x, r.Min.y + t } }, color);   // top
            FillRect({ { r.Min.x, r.Max.y - t },      r.Max },                    color);   // bottom
            FillRect({ { r.Min.x, r.Min.y + t },      { r.Min.x + t, r.Max.y - t } }, color);   // left
            FillRect({ { r.Max.x - t, r.Min.y + t },  { r.Max.x, r.Max.y - t } },     color);   // right
        }

        void ResolveSlot(const std::string& path, Ref<Texture2D>& tex, std::string& resolvedPath)
        {
            if (path == resolvedPath && (tex || path.empty())) return;
            resolvedPath = path;
            tex = path.empty() ? nullptr : AssetLibrary::GetTexture(path);
        }

        // ---- gauge --------------------------------------------------------------
        void DrawGauge(const UiGaugeComponent& g, const UiRect& rect, const WidgetCtx& ctx)
        {
            double v = 0.0;
            ctx.Number(g.Channel, g.PreviewValue, v);          // missing in live mode => 0 => empty
            const float fill = UiSystem::GaugeFill(g.Min, g.Max, v);

            if (g.Style == UiGaugeStyle::Arc)
            {
                const glm::vec2 c = rect.Center();
                const float radius = 0.5f * std::min(rect.Width(), rect.Height());
                if (!(radius > 0.0f)) return;
                const float thick = std::min(radius, std::max(1.0f, radius * std::min(std::max(g.Thickness, 0.0f), 1.0f)));
                const float rMid  = radius - thick * 0.5f;
                // 270 degrees, gap centred at the bottom (+y is DOWN in canvas space):
                // start at the bottom-left (135 deg), sweep through the top to the
                // bottom-right (405 deg). Both ring and fill are polylines of quads.
                constexpr int   kSegments = 48;
                const float kStart = glm::radians(135.0f);
                const float kSweep = glm::radians(270.0f);
                auto pointAt = [&](float t) {
                    const float a = kStart + kSweep * t;
                    return c + glm::vec2(std::cos(a), std::sin(a)) * rMid;
                };
                auto arc = [&](float t0, float t1, const glm::vec4& color) {
                    if (!(t1 > t0)) return;
                    const int n = std::max(1, (int)std::ceil((t1 - t0) * kSegments));
                    glm::vec2 prev = pointAt(t0);
                    for (int i = 1; i <= n; ++i)
                    {
                        const glm::vec2 cur = pointAt(t0 + (t1 - t0) * (float)i / (float)n);
                        // Chord segments: no end extension (it would poke out of the ring).
                        const glm::vec2 d = cur - prev;
                        const float len = glm::length(d);
                        if (len > 0.0f)
                            Renderer2D::DrawRotatedQuad(glm::vec3((prev + cur) * 0.5f, 0.0f), { len + 1.0f, thick },
                                                        std::atan2(d.y, d.x), color);
                        prev = cur;
                    }
                };
                arc(0.0f, 1.0f, g.TrackColor);
                arc(0.0f, fill, g.FillColor);
                return;
            }

            // Bar: track + fill clipped to `fill`.
            FillRect(rect, g.TrackColor);
            if (fill <= 0.0f) return;
            UiRect f = rect;
            if (g.Direction == UiGaugeDirection::BottomToTop)
                f.Min.y = rect.Max.y - rect.Height() * fill;
            else
                f.Max.x = rect.Min.x + rect.Width() * fill;
            FillRect(f, g.FillColor);
        }

        // ---- indicator ----------------------------------------------------------
        bool IndicatorOn(const UiIndicatorComponent& ind, const WidgetCtx& ctx)
        {
            if (!ctx.Live())
            {
                if (!(ctx.Bus && !ind.Channel.empty() && ctx.Bus->Has(ind.Channel)))
                    return ind.PreviewOn;
            }
            else if (ind.Channel.empty() || !ctx.Bus->Has(ind.Channel))
            {
                return false;
            }
            const double v = ctx.Bus->Get(ind.Channel).AsNumber();   // bool -> 0/1; string -> strtod
            if (!std::isfinite(v)) return false;
            const double th = (double)ind.Threshold;
            const std::string& op = ind.Op;
            if (op == "==") return v == th;
            if (op == "!=") return v != th;
            if (op == "<")  return v <  th;
            if (op == ">")  return v >  th;
            if (op == "<=") return v <= th;
            if (op == ">=") return v >= th;
            return false;                                             // unknown op: Off
        }

        void DrawIndicator(UiIndicatorComponent& ind, const UiRect& rect, float scale, const WidgetCtx& ctx)
        {
            const bool on = IndicatorOn(ind, ctx);
            ResolveSlot(ind.OnTexture,  ind.ResolvedOn,  ind.ResolvedOnPath);
            ResolveSlot(ind.OffTexture, ind.ResolvedOff, ind.ResolvedOffPath);
            DrawImageQuad(rect, on ? ind.ResolvedOn : ind.ResolvedOff, on ? ind.OnTint : ind.OffTint,
                          glm::vec4(0.0f), false, scale);
        }

        // ---- plot ---------------------------------------------------------------
        void DrawPlot(const UiPlotComponent& pl, const UiRect& rect, float scale, const WidgetCtx& ctx)
        {
            if (rect.Width() <= 0.0f || rect.Height() <= 0.0f) return;
            FillRect(rect, pl.BackgroundColor);

            const double window = (pl.WindowSeconds > 0.0f && std::isfinite(pl.WindowSeconds)) ? (double)pl.WindowSeconds : 10.0;
            const std::string* channels[4] = { &pl.Channel, &pl.Channel2, &pl.Channel3, &pl.Channel4 };
            const glm::vec4    colors[4]   = { pl.LineColor, pl.LineColor2, pl.LineColor3, pl.LineColor4 };

            // Gather each channel's points as (x in 0..1 across the window, y value).
            // A non-finite sample is skipped (never drawn) AND breaks the polyline —
            // it is stored as one NaN "break" point so the trace shows a gap instead
            // of a line across missing data. Then decimate to <= 513 points (512
            // segments), keeping breaks.
            constexpr size_t kMaxPoints = 513;
            const glm::dvec2 kBreak{ std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN() };
            auto isBreak = [](const glm::dvec2& q) { return !std::isfinite(q.x); };
            std::vector<glm::dvec2> series[4];
            bool any = false;
            std::vector<DataSample> hist;
            for (int i = 0; i < 4; ++i)
            {
                const std::string& ch = *channels[i];
                if (ch.empty()) continue;
                std::vector<glm::dvec2>& pts = series[i];
                const bool fromBus = ctx.Bus && ctx.Bus->Has(ch);
                if (fromBus)
                {
                    hist.clear();
                    ctx.Bus->History(ch, hist, window);
                    const double now = ctx.Bus->Now();
                    pts.reserve(hist.size());
                    for (const DataSample& smp : hist)
                    {
                        if (!std::isfinite(smp.Value) || !std::isfinite(smp.Time))
                        {
                            if (!pts.empty() && !isBreak(pts.back())) pts.push_back(kBreak);   // skipped: one break
                            continue;
                        }
                        const double x = 1.0 - std::min(std::max((now - smp.Time) / window, 0.0), 1.0);
                        pts.push_back({ x, smp.Value });
                    }
                    while (!pts.empty() && isBreak(pts.back())) pts.pop_back();
                }
                else if (!ctx.Live())
                {
                    // Preview: two cycles of a sine of PreviewAmplitude across the
                    // window, a quarter-turn phase step per channel so overlays read.
                    constexpr int kPreviewPoints = 128;
                    pts.reserve(kPreviewPoints + 1);
                    for (int k = 0; k <= kPreviewPoints; ++k)
                    {
                        const double x = (double)k / (double)kPreviewPoints;
                        pts.push_back({ x, (double)pl.PreviewAmplitude * std::sin(x * 4.0 * glm::pi<double>() + i * glm::half_pi<double>()) });
                    }
                }
                // live + missing channel: nothing drawn for this channel

                if (pts.size() > kMaxPoints)
                {
                    // One point per stride-wide window (a window holding a break yields
                    // the break) plus the newest point so the trace still ends "now".
                    const size_t stride = (pts.size() + kMaxPoints - 2) / (kMaxPoints - 1);
                    std::vector<glm::dvec2> dec;
                    dec.reserve(kMaxPoints);
                    for (size_t k = 0; k < pts.size(); k += stride)
                    {
                        bool brk = false;
                        for (size_t j = k; j < pts.size() && j < k + stride; ++j) if (isBreak(pts[j])) { brk = true; break; }
                        dec.push_back(brk ? kBreak : pts[k]);
                    }
                    if (!isBreak(pts.back()) && (isBreak(dec.back()) || dec.back() != pts.back())) dec.push_back(pts.back());
                    pts.swap(dec);
                }
                if (!pts.empty()) any = true;
            }

            // Y range.
            double yMin = (double)pl.YMin, yMax = (double)pl.YMax;
            if (pl.AutoScaleY && any)
            {
                yMin =  std::numeric_limits<double>::infinity();
                yMax = -std::numeric_limits<double>::infinity();
                for (const auto& pts : series)
                    for (const glm::dvec2& q : pts) if (!isBreak(q)) { yMin = std::min(yMin, q.y); yMax = std::max(yMax, q.y); }
                const double range = yMax - yMin;
                const double pad = range > 0.0 ? range * 0.05 : 1.0;
                yMin -= pad; yMax += pad;
            }
            if (!(yMax > yMin) || !std::isfinite(yMin) || !std::isfinite(yMax)) { yMin = -1.0; yMax = 1.0; }

            // Grid (1-px quads so it stays UNDER the traces in painter order).
            const int divisions = std::min(std::max(pl.GridDivisions, 0), 64);
            if (divisions > 0 && pl.GridColor.a > 0.0f)
            {
                for (int i = 0; i <= divisions; ++i)
                {
                    const float t = (float)i / (float)divisions;
                    const float x = std::round(rect.Min.x + rect.Width()  * t);
                    const float y = std::round(rect.Min.y + rect.Height() * t);
                    FillRect({ { std::min(x, rect.Max.x - 1.0f), rect.Min.y }, { std::min(x, rect.Max.x - 1.0f) + 1.0f, rect.Max.y } }, pl.GridColor);
                    FillRect({ { rect.Min.x, std::min(y, rect.Max.y - 1.0f) }, { rect.Max.x, std::min(y, rect.Max.y - 1.0f) + 1.0f } }, pl.GridColor);
                }
            }

            // Traces.
            const float lw = std::max(1.0f, pl.LineWidth * scale);
            auto toPx = [&](const glm::dvec2& q) {
                const double ny = (q.y - yMin) / (yMax - yMin);
                return glm::vec2(rect.Min.x + (float)q.x * rect.Width(),
                                 rect.Max.y - (float)std::min(std::max(ny, 0.0), 1.0) * rect.Height());
            };
            for (int i = 0; i < 4; ++i)
            {
                const auto& pts = series[i];
                if (pts.empty()) continue;
                if (pts.size() == 1)
                {
                    FillRect({ toPx(pts[0]) - glm::vec2(lw * 0.5f), toPx(pts[0]) + glm::vec2(lw * 0.5f) }, colors[i]);
                    continue;
                }
                for (size_t k = 1; k < pts.size(); ++k)
                {
                    if (isBreak(pts[k - 1]) || isBreak(pts[k])) continue;      // the gap
                    ThickSegment(toPx(pts[k - 1]), toPx(pts[k]), lw, colors[i]);
                }
            }

            // Labels: max Y (top-left), min Y (bottom-left), window (bottom-right).
            if (pl.ShowLabels)
            {
                Ref<Font> font = Font::Default();
                if (font)
                {
                    const float px = std::max(6.0f, 10.0f * scale);
                    char buf[64];
                    auto label = [&](const char* text, float x, float baselineY) {
                        glm::mat4 transform = glm::translate(glm::mat4(1.0f), { x, baselineY, 0.0f })
                                            * glm::scale(glm::mat4(1.0f), { px, -px, 1.0f });
                        Renderer2D::DrawString(text, font, transform, glm::vec4(1.0f, 1.0f, 1.0f, 0.7f));
                    };
                    std::snprintf(buf, sizeof(buf), "%g", yMax);
                    label(buf, rect.Min.x + 2.0f, rect.Min.y + font->Ascent() * px + 1.0f);
                    std::snprintf(buf, sizeof(buf), "%g", yMin);
                    label(buf, rect.Min.x + 2.0f, rect.Max.y - 2.0f);
                    std::snprintf(buf, sizeof(buf), "%gs", window);
                    const float w = MeasureLineEm(*font, buf) * px;
                    label(buf, rect.Max.x - w - 2.0f, rect.Max.y - 2.0f);
                }
            }
        }

        // ---- slider -------------------------------------------------------------
        void DrawSlider(const UiSliderComponent& sl, const UiRect& rect, float scale, const WidgetCtx& ctx)
        {
            if (rect.Width() <= 0.0f || rect.Height() <= 0.0f) return;
            double v = 0.0;
            if (!ctx.Number(sl.Channel, sl.PreviewValue, v)) v = (double)sl.Min;   // live + missing: empty
            const float t = UiSystem::GaugeFill(sl.Min, sl.Max, v);

            // KI-65: the knob geometry comes from SliderKnobRect — the SAME rect Update
            // grabs by — so the picture and the hit region cannot drift apart again.
            const UiRect knobRect = UiSystem::SliderKnobRect(rect, sl.Orientation, sl.KnobSize * scale,
                                                             sl.Min, sl.Max, v);
            const float knob  = knobRect.Width();
            const bool  vert  = sl.Orientation == UiSliderOrientation::Vertical;
            const float thick = std::max(2.0f, std::min(vert ? rect.Width() : rect.Height(), knob * 0.35f));
            const glm::vec2 c = rect.Center();
            const glm::vec2 knobCenter = knobRect.Center();

            UiRect track, fill;
            if (vert)
            {
                track = { { c.x - thick * 0.5f, rect.Min.y }, { c.x + thick * 0.5f, rect.Max.y } };
                fill  = { { track.Min.x, knobCenter.y }, { track.Max.x, rect.Max.y } };
            }
            else
            {
                track = { { rect.Min.x, c.y - thick * 0.5f }, { rect.Max.x, c.y + thick * 0.5f } };
                fill  = { { rect.Min.x, track.Min.y }, { knobCenter.x, track.Max.y } };
            }
            FillRect(track, sl.TrackColor);
            FillRect(fill, sl.FillColor);
            glm::vec4 knobColor = sl.KnobColor;
            if (!sl.Interactable) knobColor.a *= 0.6f;
            Renderer2D::DrawQuad(glm::vec3(knobCenter, 0.0f), { knob, knob }, knobColor);
        }

        // ---- hosted panel -------------------------------------------------------
        void DrawHostedPanel(const UiHostedPanelComponent& hp, const UiRect& rect, float scale, const WidgetCtx& ctx)
        {
            if (rect.Width() <= 0.0f || rect.Height() <= 0.0f) return;
            if (hp.ShowFrame)
                StrokeRect(rect, std::max(1.0f, std::round(scale)), hp.FrameColor);

            // Placeholder label: preview mode, or the host has not reported a draw
            // for this panel (unregistered name / no host block yet). The label is
            // the same text in both modes so a preview/live pair stays byte-identical
            // (E05); the host, not the canvas, knows why a panel was not drawn.
            if (!ctx.Live() || !hp.DrawnThisFrame)
            {
                Ref<Font> font = Font::Default();
                if (!font) return;
                std::string text = hp.PlaceholderText.empty() ? hp.PanelName : hp.PlaceholderText;
                if (text.empty()) text = "(panel)";
                const float px = std::max(6.0f, std::min(14.0f * scale, rect.Height() * 0.5f));
                const float w  = MeasureLineEm(*font, text) * px;
                const float x  = rect.Center().x - w * 0.5f;
                const float baselineY = rect.Center().y + (font->Ascent() - font->LineHeight() * 0.5f) * px;
                glm::mat4 transform = glm::translate(glm::mat4(1.0f), { x, baselineY, 0.0f })
                                    * glm::scale(glm::mat4(1.0f), { px, -px, 1.0f });
                glm::vec4 color = hp.FrameColor;
                color.a = std::min(1.0f, color.a * 3.0f);
                Renderer2D::DrawString(text, font, transform, color);
            }
        }
    }

    void UiSystem::Render(Scene& scene, const UiRect& viewport, const glm::mat4* cameraViewProj,
                          const DataBus* bus, bool preview)
    {
        // Projection spans the layout rect itself (the classic full-target case).
        Render(scene, viewport,
               (uint32_t)std::max(1.0f, viewport.Width()),
               (uint32_t)std::max(1.0f, viewport.Height()),
               cameraViewProj, bus, preview);
    }

    void UiSystem::CollectHostedPanels(Scene& scene, const UiRect& viewport,
                                       std::vector<UiHostedPanelDraw>& out,
                                       const glm::mat4* cameraViewProj)
    {
        out.clear();
        std::vector<UiElement> elements;
        CollectElements(scene, viewport, elements, cameraViewProj);   // back-to-front, like the draw

        auto& reg = scene.GetRegistry();
        for (const UiElement& el : elements)
        {
            const entt::entity e = static_cast<entt::entity>(el.Handle);
            auto* hp = reg.try_get<UiHostedPanelComponent>(e);
            if (!hp) continue;
            // The host reports a successful PanelRegistry::Draw by setting
            // DrawnThisFrame; collection opens the frame's report, so clear it.
            hp->DrawnThisFrame = false;
            UiHostedPanelDraw d;
            d.Handle = el.Handle;
            d.Name   = hp->PanelName;
            d.Rect   = el.Rect;
            d.Scale  = el.Scale;
            out.push_back(std::move(d));
        }
    }

    void UiSystem::Render(Scene& scene, const UiRect& canvasRect,
                          uint32_t targetW, uint32_t targetH,
                          const glm::mat4* cameraViewProj,
                          const DataBus* bus, bool preview)
    {
        // Bound widgets (AP-02): no bus => preview mode regardless of `preview`.
        WidgetCtx ctx;
        ctx.Bus     = bus;
        ctx.Preview = preview || bus == nullptr;

        std::vector<UiElement> elements;
        CollectElements(scene, canvasRect, elements, cameraViewProj);
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

            // A toggle is an image + this (like UiButton): its On/Off state picks the
            // tint multiplied into the sibling image and, when set, the texture.
            UiToggleComponent* toggle = reg.try_get<UiToggleComponent>(e);
            bool toggleOn = false;
            if (toggle)
            {
                toggleOn = ctx.Flag(toggle->Channel, toggle->PreviewOn);
                ResolveSlot(toggle->OnTexture,  toggle->ResolvedOn,  toggle->ResolvedOnPath);
                ResolveSlot(toggle->OffTexture, toggle->ResolvedOff, toggle->ResolvedOffPath);
            }

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
                // X7 — a script-supplied RuntimeTexture (e.g. a RenderToTexture
                // minimap) wins over the path-loaded image.
                Ref<Texture2D> tex = img->RuntimeTexture ? img->RuntimeTexture : img->Resolved;
                if (toggle)
                {
                    tint *= toggleOn ? toggle->OnTint : toggle->OffTint;
                    const Ref<Texture2D>& stateTex = toggleOn ? toggle->ResolvedOn : toggle->ResolvedOff;
                    if (stateTex) tex = stateTex;
                }
                DrawImageQuad(el.Rect, tex, tint, img->NineSlice,
                              img->PreserveAspect, el.Scale);
            }
            else if (toggle)
            {
                // No sibling image: the toggle draws its own quad so it is visible.
                DrawImageQuad(el.Rect, toggleOn ? toggle->ResolvedOn : toggle->ResolvedOff,
                              toggleOn ? toggle->OnTint : toggle->OffTint, glm::vec4(0.0f), false, el.Scale);
            }

            // Bound widgets (AP-02) — over the image, under the text.
            if (auto* g = reg.try_get<UiGaugeComponent>(e))
                DrawGauge(*g, el.Rect, ctx);
            if (auto* ind = reg.try_get<UiIndicatorComponent>(e))
                DrawIndicator(*ind, el.Rect, el.Scale, ctx);
            if (auto* pl = reg.try_get<UiPlotComponent>(e))
                DrawPlot(*pl, el.Rect, el.Scale, ctx);
            if (auto* sl = reg.try_get<UiSliderComponent>(e))
                DrawSlider(*sl, el.Rect, el.Scale, ctx);
            if (auto* hp = reg.try_get<UiHostedPanelComponent>(e))
                DrawHostedPanel(*hp, el.Rect, el.Scale, ctx);

            if (auto* txt = reg.try_get<UiTextComponent>(e))
            {
                if (auto* vt = reg.try_get<UiValueTextComponent>(e))
                {
                    // Value text: the resolved string + stale colour go through the
                    // sibling UiText's font/size/alignment; txt->Text is untouched.
                    const DataValue* valuePtr = nullptr;
                    DataValue value;
                    bool stale = false;
                    if (ctx.Bus && !vt->Channel.empty() && ctx.Bus->Has(vt->Channel))
                    {
                        value    = ctx.Bus->Get(vt->Channel);
                        valuePtr = &value;
                        if (ctx.Live() && vt->StaleAfter > 0.0f)
                            stale = ctx.Bus->Age(vt->Channel) > (double)vt->StaleAfter;
                    }
                    else if (!ctx.Live())
                    {
                        value    = DataValue::MakeNumber((double)vt->PreviewValue);
                        valuePtr = &value;
                    }
                    DrawTextInRect(*txt, el.Rect, el.Scale, FormatValue(*vt, valuePtr, stale),
                                   stale ? vt->StaleColor : txt->Color);
                }
                else
                {
                    DrawTextInRect(*txt, el.Rect, el.Scale, txt->Text, txt->Color);
                }
            }
        }

        Renderer2D::PopRenderPass();

        // Restore engine render-state defaults (depth ON/ON, alpha blend).
        RenderCommand::SetDepthWrite(true);
        RenderCommand::SetDepthTest(true);
        RenderCommand::SetBlendMode(RendererAPI::BlendMode::Alpha);
    }
}
