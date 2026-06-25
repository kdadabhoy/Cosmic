// Widgets.cpp — see Widgets.h.

#include "ui/Widgets.h"
#include "ui/Fonts.h"
#include "ui/ThemeManager.h"
#include "ui/Theme.h"
#include "ui/IconsLucide.h"
#include "core/Application.h"
#include "core/Window.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace Cosmic
{
	namespace UI
	{
		namespace
		{
			ImVec4 Lighten(const ImVec4& c, float amt)
			{
				return ImVec4(std::min(c.x * (1.0f + amt), 1.0f),
				              std::min(c.y * (1.0f + amt), 1.0f),
				              std::min(c.z * (1.0f + amt), 1.0f), c.w);
			}
			ImVec4 Darken(const ImVec4& c, float amt)
			{
				const float k = 1.0f - amt;
				return ImVec4(c.x * k, c.y * k, c.z * k, c.w);
			}
			float Luminance(const ImVec4& c)
			{
				return 0.299f * c.x + 0.587f * c.y + 0.114f * c.z;
			}
		}

		void StatCard(const char* id, const char* icon, const char* label,
		              const char* value, const char* sub, const ImVec4& accent,
		              const ImVec2& size, const ImVec4& valueColor)
		{
			ImVec2 sz = size;
			if (sz.x <= 0.0f) sz.x = 180.0f;
			if (sz.y <= 0.0f) sz.y = 92.0f;

			const ImGuiStyle& style = ImGui::GetStyle();
			const float rounding = style.ChildRounding > 0.0f ? style.ChildRounding : 8.0f;

			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding);
			ImGui::BeginChild(id, sz, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImVec2 p0 = ImGui::GetWindowPos();
			const ImVec2 p1 = ImVec2(p0.x + sz.x, p0.y + sz.y);

			// Left accent bar (rounded on the left to match the card corners).
			dl->AddRectFilled(p0, ImVec2(p0.x + 4.0f, p1.y),
			                  ImGui::ColorConvertFloat4ToU32(accent), rounding, ImDrawFlags_RoundCornersLeft);

			// Header row: optional icon + dimmed label, both at the small size.
			Fonts::Push("Roboto-Medium", Fonts::SizeSmall);
			if (icon && *icon && Fonts::HasIcons())
			{
				ImGui::TextColored(accent, "%s", icon);
				ImGui::SameLine(0.0f, 6.0f);
			}
			ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
			ImGui::TextUnformatted(label ? label : "");
			ImGui::PopStyleColor();
			Fonts::Pop();

			// Big crisp value (real font scaling, not SetWindowFontScale).
			const bool customVal = valueColor.w > 0.0f;
			if (customVal) ImGui::PushStyleColor(ImGuiCol_Text, valueColor);
			Fonts::Push("Roboto-Bold", 26.0f);
			ImGui::TextUnformatted(value ? value : "");
			Fonts::Pop();
			if (customVal) ImGui::PopStyleColor();

			if (sub && *sub)
				ImGui::TextDisabled("%s", sub);

			ImGui::EndChild();
			ImGui::PopStyleVar();
		}

		bool ToggleSwitch(const char* label, bool* v)
		{
			ImGuiStyle& style = ImGui::GetStyle();
			const float h = ImGui::GetFrameHeight();
			const float w = h * 1.8f;
			const ImVec2 p = ImGui::GetCursorScreenPos();

			ImGui::InvisibleButton(label, ImVec2(w, h));
			const bool clicked = ImGui::IsItemClicked();
			if (clicked && v) *v = !*v;
			const bool on = v ? *v : false;
			const bool hovered = ImGui::IsItemHovered();

			// Smoothly animate the knob between off (0) and on (1).
			ImGuiStorage* st = ImGui::GetStateStorage();
			const ImGuiID id = ImGui::GetID(label);
			const float target = on ? 1.0f : 0.0f;
			float t = st->GetFloat(id, target);
			float speed = ImGui::GetIO().DeltaTime * 12.0f;
			if (speed > 1.0f) speed = 1.0f;
			t += (target - t) * speed;
			st->SetFloat(id, t);

			const ImVec4 offC = style.Colors[ImGuiCol_FrameBg];
			const ImVec4 onC  = ThemeManager::Accent();
			const ImVec4 mix(offC.x + (onC.x - offC.x) * t,
			                 offC.y + (onC.y - offC.y) * t,
			                 offC.z + (onC.z - offC.z) * t, 1.0f);

			ImDrawList* dl = ImGui::GetWindowDrawList();
			const float r = h * 0.5f;
			dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::ColorConvertFloat4ToU32(mix), r);
			if (hovered)
				dl->AddRect(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImGuiCol_Border), r);
			const float cx = p.x + r + t * (w - 2.0f * r);
			dl->AddCircleFilled(ImVec2(cx, p.y + r), r - 2.5f, IM_COL32(255, 255, 255, 255));

			// Visible label = text before any "##" id marker.
			const char* hh = std::strstr(label, "##");
			if (hh != label)
			{
				ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
				if (hh) ImGui::TextUnformatted(label, hh);
				else    ImGui::TextUnformatted(label);
			}
			return clicked;
		}

		void SectionHeader(const char* icon, const char* text)
		{
			const ImVec4 accent = ThemeManager::Accent();
			if (icon && *icon && Fonts::HasIcons())
			{
				ImGui::TextColored(accent, "%s", icon);
				ImGui::SameLine(0.0f, 8.0f);
			}
			Fonts::Push("Roboto-Bold", Fonts::SizeBody + 1.0f);
			ImGui::TextUnformatted(text ? text : "");
			Fonts::Pop();
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}

		bool IconButton(const char* str_id, const char* icon, const char* tooltip, const ImVec2& size)
		{
			ImVec2 s = size;
			if (s.x <= 0.0f || s.y <= 0.0f)
			{
				const float h = ImGui::GetFrameHeight();
				s = ImVec2(h, h);
			}

			ImGui::PushID(str_id);
			const bool pressed = ImGui::Button(icon ? icon : "?", s);
			ImGui::PopID();

			if (tooltip && *tooltip && ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", tooltip);
			return pressed;
		}

		bool AccentButton(const char* label, const ImVec2& size)
		{
			const ImVec4 a = ThemeManager::Accent();
			const ImVec4 txt = Luminance(a) > 0.6f ? ImVec4(0.05f, 0.07f, 0.09f, 1.0f)
			                                       : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
			ImGui::PushStyleColor(ImGuiCol_Button,        a);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Lighten(a, 0.15f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Darken(a, 0.15f));
			ImGui::PushStyleColor(ImGuiCol_Text,          txt);
			const bool pressed = ImGui::Button(label, size);
			ImGui::PopStyleColor(4);
			return pressed;
		}

		void ThemeSelector()
		{
			const std::string current = ThemeManager::CurrentName();
			const float       rowH    = ImGui::GetFrameHeight();
			const float       swatch  = rowH * 0.6f;
			const float       pad     = (rowH - swatch) * 0.5f;
			ImDrawList*       dl      = ImGui::GetWindowDrawList();

			for (const Theme& t : ThemeManager::All())
			{
				ImGui::PushID(t.name.c_str());

				const ImVec2 p = ImGui::GetCursorScreenPos();
				const bool   selected = (t.name == current);
				if (ImGui::Selectable("##theme_row", selected, 0, ImVec2(0.0f, rowH)))
					ThemeManager::Apply(t.name);

				// Accent swatch + theme name drawn over the full-width selectable.
				dl->AddRectFilled(ImVec2(p.x + pad, p.y + pad),
				                  ImVec2(p.x + pad + swatch, p.y + pad + swatch),
				                  ImGui::ColorConvertFloat4ToU32(t.accent), 3.0f);
				dl->AddText(ImVec2(p.x + rowH + 4.0f, p.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f),
				            ImGui::GetColorU32(ImGuiCol_Text), t.name.c_str());

				ImGui::PopID();
			}
		}

		void WindowControls()
		{
			Window& win = Application::Get().GetWindow();

			const float  h     = ImGui::GetFrameHeight();
			const ImVec2 btn   = ImVec2(h * 1.4f, h);
			const float  total = btn.x * 3.0f;
			const float  avail = ImGui::GetContentRegionAvail().x;
			if (avail > total)
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - total));

			// Flat, chrome-style buttons: transparent until hovered, square edges.
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);

			if (IconButton("##win_min", ICON_LC_MINUS, "Minimize", btn))
				win.Minimize();
			ImGui::SameLine(0.0f, 0.0f);

			const bool maxed = win.IsWindowMaximized();
			if (IconButton("##win_max", maxed ? ICON_LC_COPY : ICON_LC_SQUARE,
			               maxed ? "Restore" : "Maximize", btn))
				win.ToggleMaximize();
			ImGui::SameLine(0.0f, 0.0f);

			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.18f, 0.18f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.65f, 0.12f, 0.12f, 1.0f));
			if (IconButton("##win_close", ICON_LC_X, "Close", btn))
				win.Close();
			ImGui::PopStyleColor(2);

			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
		}
	}
}
