#include "TelemetryProject.h"
#include <imgui.h>

namespace Workspace
{
	TelemetryProject::TelemetryProject()
	{
		m_DataPoints.reserve(500);
		m_Log.reserve(8192); // Pre-allocate some space for text
	}

	void TelemetryProject::OnUpdate(float ts)
	{
		if (m_Serial.IsOpen())
		{
			std::string newData = m_Serial.FlushBuffer();
			if (!newData.empty())
			{
				m_AccumulatedString += newData;

				// Parser: Looks for the end of a line (\n)
				size_t pos;
				while ((pos = m_AccumulatedString.find('\n')) != std::string::npos)
				{
					std::string line = m_AccumulatedString.substr(0, pos);
					m_AccumulatedString.erase(0, pos + 1);

					// 1. Add to the Serial Monitor Log
					m_Log += "[RAW]: " + line + "\n";

					// Keep the log from getting too massive (performance)
					if (m_Log.size() > 10000)
						m_Log.erase(0, 2000);

					// 2. Try to parse for the Graph
					try
					{
						float val = std::stof(line);
						m_DataPoints.push_back(val);
						if (m_DataPoints.size() > 500)
							m_DataPoints.erase(m_DataPoints.begin());
					}
					catch (...)
					{
						// If the line isn't a number (like a "Hello World" msg), just skip parsing
					}
				}
			}
		}
	}

	void TelemetryProject::OnImGuiRender()
	{
		// --- WINDOW 1: CONNECTION ---
		ImGui::Begin("Serial Settings");
		ImGui::InputText("Port Name", m_PortName, 16);
		ImGui::InputInt("Baud Rate", &m_BaudRate);

		if (!m_Serial.IsOpen())
		{
			if (ImGui::Button("Connect to ESP32", ImVec2(-1, 0)))
				m_Serial.Open(m_PortName, (uint32_t)m_BaudRate);
		}
		else
		{
			ImGui::TextColored(ImVec4(0, 1, 0, 1), "STATUS: Connected to %s", m_PortName);
			if (ImGui::Button("Disconnect", ImVec2(-1, 0)))
				m_Serial.Close();
		}
		ImGui::End();

		// --- WINDOW 2: ARDUINO-STYLE SERIAL MONITOR ---
		ImGui::Begin("Serial Monitor");
		if (ImGui::Button("Clear Output")) m_Log.clear();
		ImGui::SameLine();
		ImGui::TextDisabled("| Total Bytes: %zu", m_Log.size());
		ImGui::Separator();

		// Use a child window for the scrollable text area
		ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

		ImGui::TextUnformatted(m_Log.c_str());

		// Logic to stay at the bottom (Auto-scroll)
		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
			ImGui::SetScrollHereY(1.0f);

		ImGui::EndChild();
		ImGui::End();

		// --- WINDOW 3: VISUALIZATION ---
		ImGui::Begin("Live Plot");
		if (!m_DataPoints.empty())
		{
			ImGui::Text("Latest Value: %.3f", m_DataPoints.back());
			ImGui::PlotLines("##Data", m_DataPoints.data(), (int)m_DataPoints.size(),
				0, nullptr, FLT_MIN, FLT_MAX, ImVec2(ImGui::GetContentRegionAvail().x, 200));
		}
		else
		{
			ImGui::Text("Waiting for numerical data...");
		}
		ImGui::End();
	}
}