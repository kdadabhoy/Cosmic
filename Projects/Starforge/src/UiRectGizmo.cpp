// UiRectGizmo.cpp — see UiRectGizmo.h (AP-03, E03).

#include "UiRectGizmo.h"
#include "EditorContext.h"

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/ui/UiSystem.h"
#include "renderer/Renderer2D.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace Starforge
{
    using Cosmic::UiRect;

    // =========================================================================
    // Pure math
    // =========================================================================
    const char* UiRectGizmoMath::Name(RectHandle h)
    {
        switch (h)
        {
            case RectHandle::Move: return "Move";
            case RectHandle::N:    return "N";
            case RectHandle::NE:   return "NE";
            case RectHandle::E:    return "E";
            case RectHandle::SE:   return "SE";
            case RectHandle::S:    return "S";
            case RectHandle::SW:   return "SW";
            case RectHandle::W:    return "W";
            case RectHandle::NW:   return "NW";
            default:               return "None";
        }
    }

    UiRect UiRectGizmoMath::HandleRect(const UiRect& rect, RectHandle h)
    {
        if (h == RectHandle::Move || h == RectHandle::None)
            return rect;
        const glm::vec2 c = rect.Center();
        glm::vec2 p{ 0.0f };
        switch (h)
        {
            case RectHandle::N:  p = { c.x,        rect.Min.y }; break;
            case RectHandle::NE: p = { rect.Max.x, rect.Min.y }; break;
            case RectHandle::E:  p = { rect.Max.x, c.y        }; break;
            case RectHandle::SE: p = { rect.Max.x, rect.Max.y }; break;
            case RectHandle::S:  p = { c.x,        rect.Max.y }; break;
            case RectHandle::SW: p = { rect.Min.x, rect.Max.y }; break;
            case RectHandle::W:  p = { rect.Min.x, c.y        }; break;
            case RectHandle::NW: p = { rect.Min.x, rect.Min.y }; break;
            default: break;
        }
        UiRect out;
        out.Min = p - glm::vec2(kHandleHalf);
        out.Max = p + glm::vec2(kHandleHalf);
        return out;
    }

    RectHandle UiRectGizmoMath::HitTest(const UiRect& rect, const glm::vec2& p)
    {
        static const RectHandle kResize[] = { RectHandle::NW, RectHandle::NE, RectHandle::SE, RectHandle::SW,
                                              RectHandle::N,  RectHandle::E,  RectHandle::S,  RectHandle::W };
        for (RectHandle h : kResize)
            if (HandleRect(rect, h).Contains(p))
                return h;
        if (rect.Contains(p))
            return RectHandle::Move;
        return RectHandle::None;
    }

    float UiRectGizmoMath::Snap(float v, const RectGizmoSnap& snap)
    {
        if (snap.Grid16)      return std::round(v / 16.0f) * 16.0f;
        if (snap.PixelEighth) return std::round(v * 8.0f) / 8.0f;
        return v;
    }

    UiRectGizmoMath::DragResult UiRectGizmoMath::ApplyDrag(const DragInput& in)
    {
        const float scale = (in.Scale > 0.0f && std::isfinite(in.Scale)) ? in.Scale : 1.0f;
        UiRect r = in.StartRect;

        if (in.Handle == RectHandle::Move)
        {
            // Translate: snap the new top-left, keep the size exactly.
            const glm::vec2 size = in.StartRect.Size();
            glm::vec2 mn = in.StartRect.Min + in.Delta;
            mn = { Snap(mn.x, in.Snap), Snap(mn.y, in.Snap) };
            r.Min = mn;
            r.Max = mn + size;
        }
        else if (in.Handle != RectHandle::None)
        {
            if (MovesLeft(in.Handle))   r.Min.x = Snap(in.StartRect.Min.x + in.Delta.x, in.Snap);
            if (MovesRight(in.Handle))  r.Max.x = Snap(in.StartRect.Max.x + in.Delta.x, in.Snap);
            if (MovesTop(in.Handle))    r.Min.y = Snap(in.StartRect.Min.y + in.Delta.y, in.Snap);
            if (MovesBottom(in.Handle)) r.Max.y = Snap(in.StartRect.Max.y + in.Delta.y, in.Snap);

            // Minimum size: the edge being dragged yields.
            if (r.Max.x - r.Min.x < kMinSize)
            {
                if (MovesLeft(in.Handle)) r.Min.x = r.Max.x - kMinSize;
                else                      r.Max.x = r.Min.x + kMinSize;
            }
            if (r.Max.y - r.Min.y < kMinSize)
            {
                if (MovesTop(in.Handle)) r.Min.y = r.Max.y - kMinSize;
                else                     r.Max.y = r.Min.y + kMinSize;
            }
        }

        DragResult out;
        out.Rect      = r;
        out.OffsetMin = in.StartOffsetMin + (r.Min - in.StartRect.Min) / scale;
        out.OffsetMax = in.StartOffsetMax + (r.Max - in.StartRect.Max) / scale;
        return out;
    }

    // =========================================================================
    // The one-per-gesture command: OffsetMin/OffsetMax only, by UUID.
    // =========================================================================
    namespace
    {
        class RectOffsetsCommand final : public Cosmic::ICommand
        {
        public:
            RectOffsetsCommand(EditorContext& ctx, uint64_t uuid, std::string label,
                               glm::vec2 beforeMin, glm::vec2 beforeMax,
                               glm::vec2 afterMin,  glm::vec2 afterMax)
                : m_Ctx(&ctx), m_Uuid(uuid), m_Label(std::move(label)),
                  m_BeforeMin(beforeMin), m_BeforeMax(beforeMax), m_AfterMin(afterMin), m_AfterMax(afterMax) {}

            void Do() override   { Apply(m_AfterMin, m_AfterMax); }
            void Undo() override { Apply(m_BeforeMin, m_BeforeMax); }
            std::string Name() const override { return m_Label; }

        private:
            void Apply(const glm::vec2& mn, const glm::vec2& mx)
            {
                if (!m_Ctx->Scene) return;
                Cosmic::Entity e = m_Ctx->Scene->FindByUUID(Cosmic::UUID(m_Uuid));
                if (!e || !e.HasComponent<Cosmic::RectTransformComponent>()) return;
                auto& rt = e.GetComponent<Cosmic::RectTransformComponent>();
                rt.OffsetMin = mn;
                rt.OffsetMax = mx;
                m_Ctx->MarkDirty();
            }
            EditorContext* m_Ctx;
            uint64_t       m_Uuid;
            std::string    m_Label;
            glm::vec2      m_BeforeMin, m_BeforeMax, m_AfterMin, m_AfterMax;
        };

        UiRect BandRect(const glm::vec2& vpSize, const glm::vec4& bandUv)
        {
            return UiRect{ { bandUv.x * vpSize.x,                 bandUv.y * vpSize.y },
                           { (bandUv.x + bandUv.z) * vpSize.x,    (bandUv.y + bandUv.w) * vpSize.y } };
        }
    }

    // =========================================================================
    // Interactive gizmo
    // =========================================================================
    bool UiRectGizmo::ResolveTarget(EditorContext& ctx, const glm::vec2& vpSize, const glm::vec4& bandUv,
                                    UiRect& outRect, float& outScale)
    {
        Cosmic::Entity prim = ctx.PrimaryEntity();
        if (!prim || !prim.HasComponent<Cosmic::RectTransformComponent>())
            return false;
        std::vector<Cosmic::UiElement> elements;
        Cosmic::UiSystem::CollectElements(*ctx.Scene, BandRect(vpSize, bandUv), elements);
        const uint32_t handle = static_cast<uint32_t>(static_cast<entt::entity>(prim));
        for (const auto& el : elements)
            if (el.Handle == handle)
            {
                outRect  = el.Rect;
                outScale = el.Scale > 0.0f ? el.Scale : 1.0f;
                return true;
            }
        return false;   // a RectTransform outside any canvas has no resolved rect
    }

    UiRect UiRectGizmo::HandleScreenRect(RectHandle h) const
    {
        UiRect r = UiRectGizmoMath::HandleRect(m_Rect, h);
        r.Min += m_VpPos;
        r.Max += m_VpPos;
        return r;
    }

    bool UiRectGizmo::Update(EditorContext& ctx, const glm::vec2& vpPos, const glm::vec2& vpSize,
                             const glm::vec4& bandUv, bool pointerAvailable)
    {
        m_VpPos = vpPos;
        ImGuiIO& io = ImGui::GetIO();
        const bool lmb = io.MouseDown[0];
        const bool pressEdge   = lmb && !m_LmbWas;
        const bool releaseEdge = !lmb && m_LmbWas;
        m_LmbWas = lmb;

        const glm::vec2 local{ io.MousePos.x - vpPos.x, io.MousePos.y - vpPos.y };

        if (!ctx.Scene || vpSize.x < 1.0f || vpSize.y < 1.0f)
        {
            m_HasRect = false; m_Hover = RectHandle::None; m_Dragging = false;
            return false;
        }

        // ---- an active gesture ---------------------------------------------
        if (m_Dragging)
        {
            Cosmic::Entity e = ctx.Scene->FindByUUID(Cosmic::UUID(m_DragUuid));
            if (!e || !e.HasComponent<Cosmic::RectTransformComponent>())
            {
                m_Dragging = false;
                return false;
            }
            UiRectGizmoMath::DragInput in;
            in.Handle = m_DragHandle; in.StartRect = m_StartRect;
            in.StartOffsetMin = m_StartMin; in.StartOffsetMax = m_StartMax;
            in.Scale = m_Scale; in.Delta = local - m_PressLocal; in.Snap = Snap;
            const auto res = UiRectGizmoMath::ApplyDrag(in);
            auto& rt = e.GetComponent<Cosmic::RectTransformComponent>();
            rt.OffsetMin = res.OffsetMin;   // live, like an ImGui drag
            rt.OffsetMax = res.OffsetMax;
            m_Rect = res.Rect; m_HasRect = true;
            m_Hover = m_DragHandle;

            if (releaseEdge)
            {
                m_Dragging = false;
                const bool changed = rt.OffsetMin != m_StartMin || rt.OffsetMax != m_StartMax;
                if (changed && !ctx.Playing)
                {
                    ctx.MarkDirty();
                    ctx.Commands.Push(std::make_unique<RectOffsetsCommand>(
                        ctx, m_DragUuid,
                        m_DragHandle == RectHandle::Move ? "Move UI Rect" : "Resize UI Rect",
                        m_StartMin, m_StartMax, rt.OffsetMin, rt.OffsetMax));
                    ctx.Commands.SetMergeBarrier();
                }
            }
            return true;
        }

        // ---- idle: resolve + hover + press ----------------------------------
        UiRect rect; float scale = 1.0f;
        m_HasRect = ResolveTarget(ctx, vpSize, bandUv, rect, scale);
        if (!m_HasRect) { m_Hover = RectHandle::None; return false; }
        m_Rect = rect; m_Scale = scale;

        const bool inViewport = pointerAvailable && local.x >= 0.0f && local.y >= 0.0f &&
                                local.x <= vpSize.x && local.y <= vpSize.y;
        m_Hover = inViewport ? UiRectGizmoMath::HitTest(rect, local) : RectHandle::None;

        // A resize square always captures. The Move surface captures only when the
        // pointer is over the primary element itself (HitTest through the canvas
        // picker), so clicking an element that overlaps the selection still selects it.
        if (m_Hover == RectHandle::Move)
        {
            uint32_t hit = 0;
            const bool top = Cosmic::UiSystem::HitTest(*ctx.Scene, BandRect(vpSize, bandUv), local, hit) &&
                             hit == static_cast<uint32_t>(static_cast<entt::entity>(ctx.PrimaryEntity()));
            if (!top) m_Hover = RectHandle::None;
        }

        if (m_Hover != RectHandle::None && pressEdge && !io.KeyCtrl)
        {
            Cosmic::Entity prim = ctx.PrimaryEntity();
            if (prim.HasComponent<Cosmic::IDComponent>())
            {
                const auto& rt = prim.GetComponent<Cosmic::RectTransformComponent>();
                m_Dragging   = true;
                m_DragHandle = m_Hover;
                m_PressLocal = local;
                m_StartRect  = rect;
                m_StartMin   = rt.OffsetMin;
                m_StartMax   = rt.OffsetMax;
                m_DragUuid   = (uint64_t)prim.GetComponent<Cosmic::IDComponent>().ID;
            }
        }
        return m_Hover != RectHandle::None;
    }

    void UiRectGizmo::Draw(EditorContext& ctx, uint32_t targetW, uint32_t targetH, const glm::vec4& bandUv)
    {
        if (!ctx.Scene) return;
        const glm::vec2 vpSize{ (float)targetW, (float)targetH };
        UiRect rect; float scale = 1.0f;
        if (!m_Dragging)
        {
            if (!ResolveTarget(ctx, vpSize, bandUv, rect, scale)) return;
        }
        else
            rect = m_Rect;

        const uint32_t w = std::max(1u, targetW), h = std::max(1u, targetH);
        const glm::mat4 proj = glm::ortho(0.0f, (float)w, (float)h, 0.0f, -1.0f, 1.0f);
        Cosmic::RenderCommand::SetDepthTest(false);
        Cosmic::RenderCommand::SetDepthWrite(false);
        Cosmic::RenderCommand::SetBlendMode(Cosmic::RendererAPI::BlendMode::Alpha);
        Cosmic::Renderer2D::PushRenderPass(proj, { 0.0f, 0.0f, (float)w, (float)h });

        const glm::vec4 line{ 1.0f, 0.62f, 0.20f, 1.0f };        // forge accent
        const glm::vec4 fill{ 1.0f, 0.62f, 0.20f, 0.08f };
        const glm::vec4 knob{ 1.0f, 1.0f, 1.0f, 1.0f };
        const glm::vec4 hot { 1.0f, 0.85f, 0.35f, 1.0f };

        const glm::vec2 c = rect.Center();
        Cosmic::Renderer2D::DrawQuad(c, rect.Size(), fill);
        Cosmic::Renderer2D::DrawRect(glm::vec3(c, 0.0f), rect.Size(), line);
        static const RectHandle kAll[] = { RectHandle::N, RectHandle::NE, RectHandle::E, RectHandle::SE,
                                           RectHandle::S, RectHandle::SW, RectHandle::W, RectHandle::NW };
        for (RectHandle hnd : kAll)
        {
            const UiRect hr = UiRectGizmoMath::HandleRect(rect, hnd);
            Cosmic::Renderer2D::DrawQuad(hr.Center(), hr.Size(), (m_Hover == hnd) ? hot : knob);
            Cosmic::Renderer2D::DrawRect(glm::vec3(hr.Center(), 0.0f), hr.Size(), line);
        }
        // The move handle: a small filled square at the centre.
        Cosmic::Renderer2D::DrawQuad(c, glm::vec2(UiRectGizmoMath::kHandleHalf * 1.2f),
                                     (m_Hover == RectHandle::Move) ? hot : line);
        Cosmic::Renderer2D::PopRenderPass();
    }
}
