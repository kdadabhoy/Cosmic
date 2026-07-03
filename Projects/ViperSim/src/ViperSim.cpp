// ViperSim.cpp — see header.

#include "ViperSim.h"
#include "screens/FlightScreen.h"
#include "screens/ReplayScreen.h"
#include "screens/TuningScreen.h"
#include "screens/EnergyScreen.h"
#include "screens/TransitionScreen.h"

#include "layers/WorkspaceLayer.h"
#include "ui/ThemeManager.h"       // accent colour for the homescreen tiles
#include "ui/Overlay.h"            // UI::Text

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace Viper
{
	ViperSim::ViperSim() : Cosmic::Layer("ViperSim") {}

	void ViperSim::OnAttach()
	{
		CS_INFO("ViperSim attached — P0–P7 workbench.");
		Cosmic::FileSystem::SetActiveProject("ViperSim");

		// Config load waits for the active-project VFS set just above.
		m_Hub.LoadConfig();

		m_Flight     = std::make_unique<FlightScreen>(m_Hub);
		m_Replay     = std::make_unique<ReplayScreen>(m_Hub);
		m_Tuning     = std::make_unique<TuningScreen>(m_Hub);
		m_Energy     = std::make_unique<EnergyScreen>(m_Hub);
		m_Transition = std::make_unique<TransitionScreen>(m_Hub);
		m_Flight->OnAttach();
		m_Replay->OnAttach();
		m_Tuning->OnAttach();
		m_Energy->OnAttach();
		m_Transition->OnAttach();

		SetScreen(SCREEN_HOME);
	}

	void ViperSim::OnDetach()
	{
		if (m_Flight)     m_Flight->OnDetach();
		if (m_Replay)     m_Replay->OnDetach();
		if (m_Tuning)     m_Tuning->OnDetach();
		if (m_Energy)     m_Energy->OnDetach();
		if (m_Transition) m_Transition->OnDetach();
		m_Flight.reset();
		m_Replay.reset();
		m_Tuning.reset();
		m_Energy.reset();
		m_Transition.reset();
	}

	void ViperSim::SetScreen(Screen s)
	{
		m_Screen = s;
		// Dock layout re-applies automatically (DockStateKey changes).
	}

	int ViperSim::DockStateKey() const
	{
		return static_cast<int>(m_Screen);
	}

	void ViperSim::OnUpdate(float ts)
	{
		// Data screens sample their history every frame regardless of focus so
		// traces have no gaps; only the focused 3D screen renders the viewport.
		if (m_Tuning)     m_Tuning->OnUpdate(ts);
		if (m_Energy)     m_Energy->OnUpdate(ts);
		if (m_Transition) m_Transition->OnUpdate(ts);

		switch (m_Screen)
		{
		case SCREEN_FLIGHT: if (m_Flight) m_Flight->OnUpdate(ts); break;
		case SCREEN_REPLAY: if (m_Replay) m_Replay->OnUpdate(ts); break;
		default: break;
		}
	}

	void ViperSim::OnFixedUpdate(float dt)
	{
		// The sim runs on ANY screen — flying continues while you tune gains
		// or watch the Energy screen (shared-hub pattern).
		m_Hub.Step(dt);
	}

	// =========================================================================
	// Dock layout per screen (re-applied when the screen changes).
	// =========================================================================
	void ViperSim::ApplyDockLayout()
	{
		auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
		if (!ws) { m_AppliedDock = DockStateKey(); return; }

		ws->ClearDockWindows();

		if (m_Screen == SCREEN_HOME)
		{
			// Homescreen is a single docked window in the central node (SF_Telem
			// pattern). A floating fullscreen window would render BEHIND the
			// engine's dockspace host and be fully occluded; docking keeps it
			// under the persistent title bar and the lone tab is auto-hidden.
			ws->SetViewportVisible(false);
			ws->DockWindow("Home", Cosmic::DockPort::Center);
			m_AppliedDock = DockStateKey();
			return;
		}

		ws->SetEdgeRatios(0.22f, 0.20f, 0.07f, 0.22f);   // slim top bar row
		ws->DockWindow("ViperSim Top Bar", Cosmic::DockPort::TopCenter);

		switch (m_Screen)
		{
		case SCREEN_FLIGHT:
		case SCREEN_REPLAY:
			// 3D screens render into the engine's FBO viewport — keep the
			// central node as the Viewport tab and dock the inspector left.
			ws->SetViewportVisible(true);
			ws->DockWindow("Project Inspector Top", Cosmic::DockPort::LeftTop);
			break;

		case SCREEN_TUNING:     ws->SetViewportVisible(false); ws->DockWindow("Tuning",     Cosmic::DockPort::Center); break;
		case SCREEN_ENERGY:     ws->SetViewportVisible(false); ws->DockWindow("Energy",     Cosmic::DockPort::Center); break;
		case SCREEN_TRANSITION: ws->SetViewportVisible(false); ws->DockWindow("Transition", Cosmic::DockPort::Center); break;
		default: break;
		}

		m_AppliedDock = DockStateKey();
	}

	// =========================================================================
	// UI
	// =========================================================================

	void ViperSim::OnImGuiRender()
	{
		if (m_AppliedDock != DockStateKey())
			ApplyDockLayout();

		if (m_Screen == SCREEN_HOME)
		{
			DrawHomescreen();
			return;
		}

		DrawTopPanel();

		switch (m_Screen)
		{
		case SCREEN_FLIGHT:     if (m_Flight)     m_Flight->OnImGuiRender();     break;
		case SCREEN_REPLAY:     if (m_Replay)     m_Replay->OnImGuiRender();     break;
		case SCREEN_TUNING:     if (m_Tuning)     m_Tuning->OnImGuiRender();     break;
		case SCREEN_ENERGY:     if (m_Energy)     m_Energy->OnImGuiRender();     break;
		case SCREEN_TRANSITION: if (m_Transition) m_Transition->OnImGuiRender(); break;
		default: break;
		}
	}

	void ViperSim::DrawTopPanel()
	{
		ImGui::Begin("ViperSim Top Bar");
		if (ImGui::Button(ICON_LC_HOME "  Home"))
			SetScreen(SCREEN_HOME);
		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();

		const char* names[SCREEN_COUNT] = { "Home", "Flight", "Tuning", "Energy", "Transition", "Replay" };
		for (int i = SCREEN_FLIGHT; i < SCREEN_COUNT; ++i)
		{
			ImGui::SameLine();
			const bool active = (m_Screen == i);
			if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
			if (ImGui::Button(names[i]))
				SetScreen(static_cast<Screen>(i));
			if (active) ImGui::PopStyleColor();
		}
		ImGui::End();
	}

	// =========================================================================
	// Homescreen — tile menu docked into the central node (see ApplyDockLayout).
	// =========================================================================
	void ViperSim::DrawHomescreen()
	{
		ImGui::Begin("Home", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec4 accent  = Cosmic::ThemeManager::Accent();
		const ImU32  accentU = ImGui::ColorConvertFloat4ToU32(accent);
		const ImU32  textU   = ImGui::GetColorU32(ImGuiCol_Text);
		const ImU32  subU    = ImGui::GetColorU32(ImGuiCol_TextDisabled);

		ImFont* iconFont  = ImGui::GetFont();                                 // default font carries the Lucide glyphs
		ImFont* titleFont = Cosmic::UI::Fonts::Get("Roboto-Bold", 22.0f);
		ImFont* headFont  = Cosmic::UI::Fonts::Get("Roboto-Bold", 34.0f);

		const ImVec2 avail  = ImGui::GetContentRegionAvail();
		const ImVec2 origin = ImGui::GetCursorScreenPos();

		// ---- Heading ----
		Cosmic::UI::Text(dl, ImVec2(origin.x + avail.x * 0.5f, origin.y + 28.0f),
		                 textU, "ViperSim", headFont, 34.0f, Cosmic::UI::Align::TopCenter);
		Cosmic::UI::Text(dl, ImVec2(origin.x + avail.x * 0.5f, origin.y + 72.0f),
		                 subU, "Dual-motor tailsitter workbench", titleFont, 16.0f, Cosmic::UI::Align::TopCenter);
		if (auto cfg = m_Hub.GetConfig())
		{
			char info[256];
			std::snprintf(info, sizeof(info), "%s  -  %.2f kg  -  %s",
				cfg->Get<std::string>("meta.name", "viper").c_str(),
				m_Hub.GetBodyParams().mass_kg,
				cfg->GetSource().c_str());
			Cosmic::UI::Text(dl, ImVec2(origin.x + avail.x * 0.5f, origin.y + 96.0f),
			                 subU, info, nullptr, 13.0f, Cosmic::UI::Align::TopCenter);
		}

		// ---- Tile grid (rows of 3, centered; planned screens are dimmed) ----
		struct TileDef { Screen screen; const char* icon; const char* title; const char* blurb; bool live; };
		static const TileDef kTiles[] = {
			{ SCREEN_FLIGHT,     ICON_LC_PLANE_TAKEOFF,      "Flight",     "Fly it - gates G1-G3, HIL", true },
			{ SCREEN_REPLAY,     ICON_LC_HISTORY,            "Replay",     "Scrub recordings",          true },
			{ SCREEN_TUNING,     ICON_LC_SLIDERS_HORIZONTAL, "Tuning",     "Gains + step response",     true },
			{ SCREEN_ENERGY,     ICON_LC_BATTERY_CHARGING,   "Energy",     "Power & endurance",         true },
			{ SCREEN_TRANSITION, ICON_LC_REFRESH_CW,         "Transition", "VTOL <-> cruise machine",   true },
		};
		const int kCount = static_cast<int>(sizeof(kTiles) / sizeof(kTiles[0]));

		const int   kCols = 3;
		const float gap   = 24.0f;
		const float gridW = std::min(avail.x * 0.86f, 1000.0f);
		const float tileW = (gridW - gap * (kCols - 1)) / static_cast<float>(kCols);
		const float tileH = 150.0f;
		const int   rows  = (kCount + kCols - 1) / kCols;
		const float gridH = rows * tileH + (rows - 1) * gap;
		const float offY  = origin.y + std::max(130.0f, (avail.y - gridH) * 0.5f);

		for (int i = 0; i < kCount; ++i)
		{
			const TileDef& t = kTiles[i];
			const int row       = i / kCols;
			const int colsInRow = std::min(kCols, kCount - row * kCols);
			const float rowW    = colsInRow * tileW + (colsInRow - 1) * gap;
			const float rowX    = origin.x + (avail.x - rowW) * 0.5f;
			const ImVec2 p0(rowX + (i % kCols) * (tileW + gap), offY + row * (tileH + gap));
			const ImVec2 p1(p0.x + tileW, p0.y + tileH);

			ImGui::SetCursorScreenPos(p0);
			ImGui::PushID(i);
			bool clicked = false, hov = false;
			if (t.live)
			{
				clicked = ImGui::InvisibleButton("##tile", ImVec2(tileW, tileH));
				hov     = ImGui::IsItemHovered();
			}
			else
			{
				ImGui::Dummy(ImVec2(tileW, tileH));   // reserves space, no click
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Planned for a later phase.");
			}
			ImGui::PopID();

			const ImU32 base   = ImGui::GetColorU32(hov ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
			const ImU32 border = hov ? accentU : ImGui::GetColorU32(ImGuiCol_Border);
			dl->AddRectFilled(p0, p1, base, 12.0f);
			dl->AddRect(p0, p1, border, 12.0f, 0, hov ? 2.5f : 1.0f);

			const float cx = (p0.x + p1.x) * 0.5f;
			Cosmic::UI::Text(dl, ImVec2(cx, p0.y + tileH * 0.28f), t.live ? accentU : subU, t.icon,  iconFont,  40.0f, Cosmic::UI::Align::Center);
			Cosmic::UI::Text(dl, ImVec2(cx, p0.y + tileH * 0.58f), t.live ? textU   : subU, t.title, titleFont, 22.0f, Cosmic::UI::Align::Center);
			Cosmic::UI::Text(dl, ImVec2(cx, p0.y + tileH * 0.78f), subU,                    t.blurb, nullptr,   14.0f, Cosmic::UI::Align::Center);

			if (clicked) SetScreen(t.screen);
		}

		ImGui::End();
	}

	void ViperSim::OnEvent(Cosmic::Event& e)
	{
		switch (m_Screen)
		{
		case SCREEN_FLIGHT: if (m_Flight) m_Flight->OnEvent(e); break;
		case SCREEN_REPLAY: if (m_Replay) m_Replay->OnEvent(e); break;
		default: break;
		}
	}
}

// =============================================================================
// Required C-linkage DLL entry points — do not rename or remove
// =============================================================================
extern "C"
{
	__declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context)
	{
		ImGui::SetCurrentContext(context.ImGuiCtx);
		ImPlot::SetCurrentContext(context.ImPlotCtx);
	}

	__declspec(dllexport) Cosmic::Layer* CreatePluginLayer()
	{
		return new Viper::ViperSim();
	}
}
