#include "TemplateThemeShowcaseLayer.h"

#include <cmath>
#include <cstring>

namespace Workspace
{
	namespace UI = Cosmic::UI;
	using Cosmic::ThemeManager;
	using Cosmic::Theme;
	using Cosmic::FileSystem;

	TemplateThemeShowcaseLayer::TemplateThemeShowcaseLayer()
		: Cosmic::Layer("Theme Studio")
	{
	}

	void TemplateThemeShowcaseLayer::OnAttach()
	{
		// Pick up any themes the user saved previously for this project.
		ThemeManager::LoadFolder(FileSystem::Resolve("project://themes"));
		BeginEditFrom(ThemeManager::CurrentName());
	}

	void TemplateThemeShowcaseLayer::BeginEditFrom(const std::string& themeName)
	{
		if (const Theme* t = ThemeManager::Find(themeName))
			m_Edit = *t;
		else
			m_Edit = ThemeManager::CaptureCurrentStyle(themeName.empty() ? "Custom" : themeName);
		m_Editing = false;
	}

	// -------------------------------------------------------------------------
	void TemplateThemeShowcaseLayer::OnImGuiRender()
	{
		ImGui::Begin(THEME_STUDIO_WINDOW);

		DrawThemePicker();
		ImGui::Separator();
		ImGui::Spacing();

		const float editorWidth = 340.0f;
		ImGui::BeginChild("##theme_editor", ImVec2(editorWidth, 0.0f), true);
		DrawEditor();
		ImGui::EndChild();

		ImGui::SameLine();

		ImGui::BeginChild("##theme_preview", ImVec2(0.0f, 0.0f), true);
		DrawPreviewGallery();
		ImGui::EndChild();

		ImGui::End();
	}

	// -------------------------------------------------------------------------
	void TemplateThemeShowcaseLayer::DrawThemePicker()
	{
		UI::SectionHeader(ICON_LC_SWATCH_BOOK, "Themes");

		const std::string current = ThemeManager::CurrentName();
		ImGui::TextUnformatted("Active:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(260.0f);
		if (ImGui::BeginCombo("##theme_combo", current.c_str()))
		{
			const auto& all = ThemeManager::All();
			for (const Theme& t : all)
			{
				const bool selected = (t.name == current);
				if (ImGui::Selectable(t.name.c_str(), selected))
				{
					ThemeManager::Apply(t.name);
					BeginEditFrom(t.name);
				}
				if (selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		ImGui::SameLine();
		if (UI::IconButton("edit_current", ICON_LC_PENCIL, "Load this theme into the editor"))
			BeginEditFrom(ThemeManager::CurrentName());
	}

	// -------------------------------------------------------------------------
	void TemplateThemeShowcaseLayer::DrawEditor()
	{
		UI::SectionHeader(ICON_LC_SLIDERS_HORIZONTAL, "Editor");
		ImGui::TextDisabled("Tweaks preview live. Save to add a new theme.");
		ImGui::Spacing();

		bool changed = false;

		changed |= ImGui::ColorEdit3("Accent", &m_Edit.accent.x, ImGuiColorEditFlags_NoInputs);

		if (ImGui::CollapsingHeader("Style", ImGuiTreeNodeFlags_DefaultOpen))
		{
			changed |= ImGui::SliderFloat("Window rounding", &m_Edit.style.WindowRounding, 0.0f, 14.0f);
			changed |= ImGui::SliderFloat("Child rounding",  &m_Edit.style.ChildRounding,  0.0f, 14.0f);
			changed |= ImGui::SliderFloat("Frame rounding",  &m_Edit.style.FrameRounding,  0.0f, 12.0f);
			changed |= ImGui::SliderFloat("Tab rounding",    &m_Edit.style.TabRounding,    0.0f, 12.0f);
			changed |= ImGui::SliderFloat("Grab rounding",   &m_Edit.style.GrabRounding,   0.0f, 12.0f);
			changed |= ImGui::SliderFloat("Window border",   &m_Edit.style.WindowBorderSize, 0.0f, 2.0f);
			changed |= ImGui::SliderFloat("Frame border",    &m_Edit.style.FrameBorderSize,  0.0f, 2.0f);
			changed |= ImGui::SliderFloat2("Window padding", &m_Edit.style.WindowPadding.x, 0.0f, 20.0f);
			changed |= ImGui::SliderFloat2("Frame padding",  &m_Edit.style.FramePadding.x,  0.0f, 16.0f);
			changed |= ImGui::SliderFloat2("Item spacing",   &m_Edit.style.ItemSpacing.x,   0.0f, 20.0f);
		}

		if (ImGui::CollapsingHeader("Colors"))
		{
			// Compact swatch + name rows (the name is the editor label and stays
			// visible). Click a swatch to open the full picker. Using NoInputs
			// keeps the long list readable in the narrow editor column.
			for (int i = 0; i < ImGuiCol_COUNT; ++i)
			{
				ImGui::PushID(i);
				changed |= ImGui::ColorEdit4(ImGui::GetStyleColorName(i), &m_Edit.colors[i].x,
				                             ImGuiColorEditFlags_NoInputs |
				                             ImGuiColorEditFlags_AlphaPreviewHalf);
				ImGui::PopID();
			}
		}

		if (changed)
			ThemeManager::ApplyTheme(m_Edit); // live preview

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputText("##new_name", m_NewName, sizeof(m_NewName));
		if (UI::AccentButton(ICON_LC_SAVE "  Save as new theme", ImVec2(-1.0f, 0.0f)))
		{
			if (m_NewName[0] != '\0')
			{
				m_Edit.name    = m_NewName;
				m_Edit.builtIn = false;
				ThemeManager::Register(m_Edit);

				const std::string path =
					FileSystem::Resolve(std::string("project://themes/") + m_NewName + ".ctheme");
				if (ThemeManager::SaveToFile(m_Edit, path))
					m_LastSavedPath = path;

				ThemeManager::Apply(m_Edit.name);
			}
		}

		if (!m_LastSavedPath.empty())
			ImGui::TextDisabled(ICON_LC_CIRCLE_CHECK "  Saved: %s", m_LastSavedPath.c_str());
	}

	// -------------------------------------------------------------------------
	void TemplateThemeShowcaseLayer::DrawPreviewGallery()
	{
		const ImVec4 accent = ThemeManager::Accent();

		// Typography
		UI::SectionHeader(ICON_LC_TYPE, "Typography");
		Cosmic::UI::Fonts::Push("Roboto-Bold", Cosmic::UI::Fonts::SizeHeading);
		ImGui::TextUnformatted("Heading — Cosmic Engine");
		Cosmic::UI::Fonts::Pop();
		ImGui::TextUnformatted("Body text: the quick brown fox jumps over the lazy dog.");
		ImGui::TextDisabled("Secondary / disabled text");

		// Controls
		UI::SectionHeader(ICON_LC_MOUSE_POINTER_CLICK, "Controls");
		ImGui::Button("Button");
		ImGui::SameLine();
		UI::AccentButton("Primary");
		ImGui::SameLine();
		UI::IconButton("preview_settings", ICON_LC_SETTINGS, "Settings");
		ImGui::SameLine();
		UI::IconButton("preview_refresh", ICON_LC_REFRESH_CW, "Refresh");

		ImGui::Checkbox("Checkbox", &m_Check);
		UI::ToggleSwitch("Live capture", &m_ToggleA);
		UI::ToggleSwitch("Armed", &m_ToggleB);
		ImGui::SetNextItemWidth(220.0f);
		ImGui::SliderFloat("Slider", &m_Slider, 0.0f, 1.0f);
		ImGui::SetNextItemWidth(220.0f);
		const char* items[] = { "Option A", "Option B", "Option C" };
		ImGui::Combo("Combo", &m_Combo, items, IM_ARRAYSIZE(items));

		// Cards
		UI::SectionHeader(ICON_LC_LAYOUT_DASHBOARD, "Cards");
		UI::StatCard("sc_rpm",  ICON_LC_GAUGE,       "Weapon RPM",  "12,480", "avg 9,120  max 13,002", accent, ImVec2(190.0f, 92.0f));
		ImGui::SameLine();
		UI::StatCard("sc_volt", ICON_LC_ZAP,         "Bus Voltage", "24.6 V", "min 22.1  max 25.0",    accent, ImVec2(190.0f, 92.0f));
		ImGui::SameLine();
		UI::StatCard("sc_temp", ICON_LC_THERMOMETER, "ESC Temp",    "47 C",   "avg 39  max 52",        accent, ImVec2(190.0f, 92.0f));

		// Tabs + table
		UI::SectionHeader(ICON_LC_TABLE, "Tabs & table");
		if (ImGui::BeginTabBar("##preview_tabs"))
		{
			if (ImGui::BeginTabItem(ICON_LC_ACTIVITY "  Live"))
			{
				ImGui::TextWrapped("Tab content uses the active theme's tab + window colours.");
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem(ICON_LC_TABLE "  Data"))
			{
				if (ImGui::BeginTable("##preview_table", 3,
					ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
				{
					ImGui::TableSetupColumn("Channel");
					ImGui::TableSetupColumn("Value");
					ImGui::TableSetupColumn("Unit");
					ImGui::TableHeadersRow();
					const char* rows[][3] = {
						{ "RPM", "12,480", "rpm" },
						{ "Voltage", "24.6", "V" },
						{ "Current", "31.2", "A" },
					};
					for (auto& r : rows)
					{
						ImGui::TableNextRow();
						for (int c = 0; c < 3; ++c)
						{
							ImGui::TableSetColumnIndex(c);
							ImGui::TextUnformatted(r[c]);
						}
					}
					ImGui::EndTable();
				}
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		// Plot
		UI::SectionHeader(ICON_LC_CHART_LINE, "Plot");
		if (ImPlot::BeginPlot("##preview_plot", ImVec2(-1.0f, 170.0f)))
		{
			static float xs[256], y1[256], y2[256];
			const float t = (float)ImGui::GetTime();
			for (int i = 0; i < 256; ++i)
			{
				xs[i] = (float)i;
				y1[i] = std::sin(i * 0.05f + t) * 0.8f;
				y2[i] = std::cos(i * 0.04f + t * 0.7f) * 0.5f;
			}
			ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, 0);
			// ImPlot v1.0 styles series via the colormap (per-item SetNextLineStyle
			// was obsoleted); the lines pick up the theme-synced plot style.
			ImPlot::PlotLine("signal", xs, y1, 256);
			ImPlot::PlotLine("reference", xs, y2, 256);
			ImPlot::EndPlot();
		}
	}
}
