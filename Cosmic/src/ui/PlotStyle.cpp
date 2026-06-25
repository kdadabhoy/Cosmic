// PlotStyle.cpp — see PlotStyle.h.

#include "ui/PlotStyle.h"

#include <imgui.h>
#include <implot.h>

namespace Cosmic
{
	namespace UI
	{
		void ApplyPlotStyle(const Theme& theme)
		{
			if (ImPlot::GetCurrentContext() == nullptr)
				return; // ImPlot not initialised yet

			ImPlotStyle& ps = ImPlot::GetStyle();

			//ps.LineWeight = 1.8f; // apparently doesnt work anymore
			//ps.FillAlpha       = 0.25f;   // apparently doesnt work anymore // gradient-ish shaded fills under lines
			ps.PlotBorderSize  = 0.0f;    // no hard frame; we blend into the panel
			ps.MinorAlpha      = 0.18f;
			ps.PlotPadding     = ImVec2(8.0f, 6.0f);
			ps.LabelPadding    = ImVec2(5.0f, 4.0f);
			ps.LegendPadding   = ImVec2(8.0f, 8.0f);

			const ImVec4* c = theme.colors;
			const ImVec4 border = c[ImGuiCol_Border];
			const ImVec4 grid(border.x, border.y, border.z, 0.30f);

			ImVec4* pc = ps.Colors;
			pc[ImPlotCol_FrameBg]      = ImVec4(0, 0, 0, 0);                 // inherit panel bg
			pc[ImPlotCol_PlotBg]       = ImVec4(0, 0, 0, 0);
			pc[ImPlotCol_PlotBorder]   = ImVec4(0, 0, 0, 0);
			pc[ImPlotCol_AxisText]     = c[ImGuiCol_TextDisabled];
			pc[ImPlotCol_AxisGrid]     = grid;
			pc[ImPlotCol_AxisTick]     = grid;
			pc[ImPlotCol_AxisBgHovered]= c[ImGuiCol_ButtonHovered];
			pc[ImPlotCol_AxisBgActive] = c[ImGuiCol_ButtonActive];
			pc[ImPlotCol_LegendBg]     = c[ImGuiCol_PopupBg];
			pc[ImPlotCol_LegendBorder] = border;
			pc[ImPlotCol_LegendText]   = c[ImGuiCol_Text];
			pc[ImPlotCol_TitleText]    = c[ImGuiCol_Text];
			pc[ImPlotCol_InlayText]    = c[ImGuiCol_Text];
			pc[ImPlotCol_Selection]    = ImVec4(theme.accent.x, theme.accent.y, theme.accent.z, 0.35f);
			pc[ImPlotCol_Crosshairs]   = ImVec4(c[ImGuiCol_Text].x, c[ImGuiCol_Text].y, c[ImGuiCol_Text].z, 0.50f);
		}
	}
}
