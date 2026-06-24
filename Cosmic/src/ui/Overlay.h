#pragma once

// Overlay.h
// ============================================================================
// Cosmic::UI — header-only ImGui drawing helpers for image overlays and text.
// ============================================================================
//
// These are general-purpose primitives, NOT tied to any one project or widget:
//
//   * ImageFitted  — draw a texture aspect-fitted into a region; get its rect.
//   * Text         — draw a string with a chosen UI font + alignment (the core
//                    reusable text primitive).
//   * TextThick    — font-agnostic faux-bold fallback.
//   * ReadoutBox   — a framed label+value box; a thin *consumer* of Text(), to
//                    show the text system stands on its own.
//
// Header-only and inline on purpose (same pattern as ImGuiThemes.h): it compiles
// into whichever module includes it and uses the shared ImGui context, so it
// works identically inside engine code and inside project DLLs.
//
// Fonts come from Cosmic::UI::Fonts (see Fonts.h) — pass an ImFont* (e.g. a bold
// face) for crisp heavy text, or leave it null to use the default font.
// ============================================================================

#include "core/Core.h"
#include "graphics/Texture.h"
#include "ui/Fonts.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdint>

namespace Cosmic
{
	namespace UI
	{
		// How an anchor point maps onto a drawn element.
		enum class Align
		{
			TopLeft, TopCenter, TopRight,
			CenterLeft, Center, CenterRight,
			BottomLeft, BottomCenter, BottomRight
		};

		// An on-screen rectangle with a normalized-coordinate lookup. `At(nx,ny)`
		// turns image-relative coordinates in [0,1] into screen pixels — which is
		// what makes hand-tuned overlay positions trivial ("0.5,0.2" = top-centre).
		struct Rect
		{
			ImVec2 Min{ 0.0f, 0.0f };
			ImVec2 Max{ 0.0f, 0.0f };

			float  Width()  const { return Max.x - Min.x; }
			float  Height() const { return Max.y - Min.y; }
			ImVec2 Size()   const { return { Max.x - Min.x, Max.y - Min.y }; }
			ImVec2 Center() const { return { (Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f }; }
			ImVec2 At(float nx, float ny) const
			{
				return { Min.x + nx * (Max.x - Min.x), Min.y + ny * (Max.y - Min.y) };
			}
		};

		// Shift `pos` so that the element of the given `size` is anchored there.
		inline ImVec2 AlignPos(ImVec2 pos, ImVec2 size, Align a)
		{
			float x = pos.x, y = pos.y;
			switch (a)
			{
			case Align::TopLeft:                                            break;
			case Align::TopCenter:    x -= size.x * 0.5f;                   break;
			case Align::TopRight:     x -= size.x;                          break;
			case Align::CenterLeft:                       y -= size.y*0.5f; break;
			case Align::Center:       x -= size.x * 0.5f; y -= size.y*0.5f; break;
			case Align::CenterRight:  x -= size.x;        y -= size.y*0.5f; break;
			case Align::BottomLeft:                       y -= size.y;      break;
			case Align::BottomCenter: x -= size.x * 0.5f; y -= size.y;      break;
			case Align::BottomRight:  x -= size.x;        y -= size.y;      break;
			}
			return { x, y };
		}

		// Measure `text` in a specific font/size (falls back to the default font).
		inline ImVec2 MeasureText(ImFont* font, float sizePx, const char* text)
		{
			if (!font) font = Fonts::Default();
			if (!font) return ImGui::CalcTextSize(text);
			if (sizePx <= 0.0f) sizePx = ImGui::GetFontSize();
			return font->CalcTextSizeA(sizePx, FLT_MAX, 0.0f, text);
		}

		// Draw a texture aspect-fitted (letterboxed + centered) into `region`
		// (region.x/y <= 0 means "use the remaining content region"). Reserves the
		// whole region in the ImGui layout and returns the on-screen image rect.
		// UVs are flipped vertically because the engine loads textures bottom-up.
		inline Rect ImageFitted(const Ref<Texture2D>& tex, ImVec2 region = ImVec2(0, 0))
		{
			ImVec2 avail = region;
			if (avail.x <= 0.0f) avail.x = ImGui::GetContentRegionAvail().x;
			if (avail.y <= 0.0f) avail.y = ImGui::GetContentRegionAvail().y;

			const ImVec2 cursor = ImGui::GetCursorScreenPos();
			Rect r;

			if (!tex || tex->GetWidth() == 0 || tex->GetHeight() == 0)
			{
				ImGui::Dummy(avail);
				r.Min = cursor;
				r.Max = { cursor.x + avail.x, cursor.y + avail.y };
				return r;
			}

			const float iw = (float)tex->GetWidth();
			const float ih = (float)tex->GetHeight();
			const float scale = std::min(avail.x / iw, avail.y / ih);
			const ImVec2 draw = { iw * scale, ih * scale };
			const ImVec2 offset = { (avail.x - draw.x) * 0.5f, (avail.y - draw.y) * 0.5f };

			ImGui::SetCursorScreenPos({ cursor.x + offset.x, cursor.y + offset.y });
			ImGui::Image(static_cast<ImTextureID>(tex->GetRendererID()),
			             draw, ImVec2(0, 1), ImVec2(1, 0));
			r.Min = ImGui::GetItemRectMin();
			r.Max = ImGui::GetItemRectMax();

			// Reserve the full region so following widgets flow below it.
			ImGui::SetCursorScreenPos(cursor);
			ImGui::Dummy(avail);
			return r;
		}

		// The core reusable text primitive: draw `text` at `pos`, anchored per
		// `align`, in the given font/size (null/0 -> default font / its size).
		inline void Text(ImDrawList* dl, ImVec2 pos, ImU32 color, const char* text,
		                 ImFont* font = nullptr, float sizePx = 0.0f,
		                 Align align = Align::TopLeft)
		{
			if (!text || !*text) return;
			if (!font) font = Fonts::Default();
			const float sz = sizePx > 0.0f ? sizePx : ImGui::GetFontSize();
			const ImVec2 ts = MeasureText(font, sz, text);
			const ImVec2 p = AlignPos(pos, ts, align);
			if (font) dl->AddText(font, sz, p, color, text);
			else      dl->AddText(p, color, text);
		}

		// Font-agnostic faux-bold: re-draw the text at small offsets to thicken it.
		// Use only when no real bold face is available — a bold ImFont looks better.
		inline void TextThick(ImDrawList* dl, ImVec2 pos, ImU32 color, const char* text,
		                      ImFont* font = nullptr, float sizePx = 0.0f,
		                      float weight = 1.0f, Align align = Align::TopLeft)
		{
			if (!text || !*text) return;
			if (!font) font = Fonts::Default();
			const float sz = sizePx > 0.0f ? sizePx : ImGui::GetFontSize();
			const ImVec2 ts = MeasureText(font, sz, text);
			const ImVec2 p = AlignPos(pos, ts, align);
			const float w = weight;
			const ImVec2 offs[4] = { { -w, 0 }, { w, 0 }, { 0, -w }, { 0, w } };
			for (const ImVec2& o : offs)
			{
				const ImVec2 q = { p.x + o.x, p.y + o.y };
				if (font) dl->AddText(font, sz, q, color, text); else dl->AddText(q, color, text);
			}
			if (font) dl->AddText(font, sz, p, color, text); else dl->AddText(p, color, text);
		}

		// Visual style for ReadoutBox. Every colour/size is overridable; set
		// ValueFont to a bold face (e.g. Fonts::Get("Roboto-Bold", 26)) for the
		// classic white-box / thick-black-value look.
		struct ReadoutStyle
		{
			ImU32   Fill            = IM_COL32(255, 255, 255, 235);
			ImU32   Border          = IM_COL32(20, 20, 24, 200);
			ImU32   LabelColor      = IM_COL32(70, 75, 90, 255);
			ImU32   ValueColor      = IM_COL32(10, 10, 12, 255);
			float   Rounding        = 6.0f;
			float   BorderThickness = 1.5f;
			ImVec2  Padding         = ImVec2(10.0f, 6.0f);
			float   LabelSize       = 13.0f;
			float   ValueSize       = 26.0f;
			float   Spacing         = 2.0f;   // gap between label and value
			ImFont* LabelFont       = nullptr;
			ImFont* ValueFont       = nullptr;
			bool    FauxBold        = false;  // thicken value when no bold font is loaded
			Align   Anchor          = Align::Center; // how posPx maps onto the box
			ImVec2  MinSize         = ImVec2(0.0f, 0.0f);
		};

		// A framed label-over-value box anchored at posPx. Auto-sizes to content
		// (clamped to MinSize). Returns the box rect. Internally just calls Text()/
		// TextThick(), so it is one consumer of the text system, not the system.
		inline Rect ReadoutBox(ImDrawList* dl, ImVec2 posPx, const char* label,
		                       const char* value, const ReadoutStyle& s = ReadoutStyle())
		{
			ImFont* lf = s.LabelFont ? s.LabelFont : Fonts::Default();
			ImFont* vf = s.ValueFont ? s.ValueFont : Fonts::Default();

			const bool hasLabel = label && *label;
			const bool hasValue = value && *value;
			const ImVec2 ls = hasLabel ? MeasureText(lf, s.LabelSize, label) : ImVec2(0, 0);
			const ImVec2 vs = hasValue ? MeasureText(vf, s.ValueSize, value) : ImVec2(0, 0);

			const float contentW = std::max(ls.x, vs.x);
			const float contentH = ls.y + ((hasLabel && hasValue) ? s.Spacing : 0.0f) + vs.y;

			ImVec2 boxSize = { contentW + s.Padding.x * 2.0f, contentH + s.Padding.y * 2.0f };
			boxSize.x = std::max(boxSize.x, s.MinSize.x);
			boxSize.y = std::max(boxSize.y, s.MinSize.y);

			const ImVec2 tl = AlignPos(posPx, boxSize, s.Anchor);
			Rect r;
			r.Min = tl;
			r.Max = { tl.x + boxSize.x, tl.y + boxSize.y };

			dl->AddRectFilled(r.Min, r.Max, s.Fill, s.Rounding);
			if (s.BorderThickness > 0.0f)
				dl->AddRect(r.Min, r.Max, s.Border, s.Rounding, 0, s.BorderThickness);

			const float cx = (r.Min.x + r.Max.x) * 0.5f;
			float y = r.Min.y + s.Padding.y;

			if (hasLabel)
			{
				Text(dl, ImVec2(cx, y), s.LabelColor, label, lf, s.LabelSize, Align::TopCenter);
				y += ls.y + s.Spacing;
			}
			if (hasValue)
			{
				if (s.FauxBold)
					TextThick(dl, ImVec2(cx, y), s.ValueColor, value, vf, s.ValueSize, 1.0f, Align::TopCenter);
				else
					Text(dl, ImVec2(cx, y), s.ValueColor, value, vf, s.ValueSize, Align::TopCenter);
			}

			return r;
		}

		// A resizable floating window that shows a texture aspect-fitted — a generic
		// "pop-out" for reference images (e.g. a board pinout). The caller owns the
		// open flag; pass &bool so the window's close button works. An optional
		// `caption` is wrapped below the image (e.g. a legend / naming key).
		inline void ImageWindow(const char* title, const Ref<Texture2D>& tex, bool* p_open,
		                        const char* caption = nullptr,
		                        ImVec2 firstSize = ImVec2(460.0f, 720.0f))
		{
			if (!p_open || !*p_open) return;
			ImGui::SetNextWindowSize(firstSize, ImGuiCond_FirstUseEver);
			if (ImGui::Begin(title, p_open))
			{
				const bool hasCaption = caption && *caption;

				if (tex && tex->GetWidth() > 0)
				{
					ImVec2 avail = ImGui::GetContentRegionAvail();
					float  capH  = 0.0f;
					if (hasCaption)
						capH = ImGui::CalcTextSize(caption, nullptr, false, avail.x).y
						     + ImGui::GetStyle().ItemSpacing.y * 3.0f;

					float imgH = avail.y - capH;
					if (imgH < 48.0f) imgH = avail.y;   // tiny window: prioritise the image
					ImageFitted(tex, ImVec2(avail.x, imgH));
				}
				else
				{
					ImGui::TextWrapped("Image not found. Place the file in assets/images and rebuild.");
				}

				if (hasCaption)
				{
					ImGui::Separator();
					ImGui::TextWrapped("%s", caption);
				}
			}
			ImGui::End();
		}
	}
}
