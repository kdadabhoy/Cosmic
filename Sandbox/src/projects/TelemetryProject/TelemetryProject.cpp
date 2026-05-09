#include "TelemetryProject.h"
#include <imgui.h>
#include <implot.h>
#include <windows.h> 
#include <algorithm> 

namespace Workspace
{
	TelemetryProject::TelemetryProject()
	{
		// Increased history size for better visualization
		m_DataPoints.reserve(1000);
		m_Log.reserve(8192);
		m_AvailablePorts = Cosmic::SerialPort::GetAvailablePorts();
	}

	void TelemetryProject::OnUpdate(float ts)
	{
		static bool loggedMain = false;
		if (!loggedMain)
		{
			DWORD winThreadId = GetCurrentThreadId();
			int core = GetCurrentProcessorNumber();
			m_Log += "[ENGINE THREAD] Running with ID: " + std::to_string(winThreadId) + " on Core: " + std::to_string(core) + "\n";
			loggedMain = true;
		}

		if (m_Serial.IsOpen())
		{
			std::string newData = m_Serial.FlushBuffer();
			if (!newData.empty())
			{
				m_AccumulatedString += newData;

				size_t pos;
				while ((pos = m_AccumulatedString.find('\n')) != std::string::npos)
				{
					std::string line = m_AccumulatedString.substr(0, pos);
					m_AccumulatedString.erase(0, pos + 1);

					// Clean up line endings (\r\n handling)
					line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());

					// 1. Log update
					m_Log += "[LOG]: " + line + "\n";
					if (m_Log.size() > 100000) m_Log.erase(0, 20000);

					// 2. Robust Parsing
					size_t lastSpace = line.find_last_of(' ');
					std::string numericPart = (lastSpace != std::string::npos) ? line.substr(lastSpace + 1) : line;

					// Only attempt parse if string looks like a number
					if (!numericPart.empty() && (isdigit(numericPart[0]) || numericPart[0] == '-' || numericPart[0] == '.'))
					{
						try
						{
							float val = std::stof(numericPart);
							m_DataPoints.push_back(val);
							m_TotalPointsProcessed++;

							if (m_DataPoints.size() > 1000)
								m_DataPoints.erase(m_DataPoints.begin());
						}
						catch (...) { /* Ignore "Noise" or malformed packets */ }
					}
				}
			}
		}
	}

	void TelemetryProject::OnImGuiRender()
	{
		// --- WINDOW 1: SERIAL SETTINGS ---
		ImGui::Begin("Serial Settings");
		if (ImGui::Button("Refresh Ports"))
		{
			m_AvailablePorts = Cosmic::SerialPort::GetAvailablePorts();
			if (m_SelectedPortIndex >= m_AvailablePorts.size()) m_SelectedPortIndex = 0;
		}

		const char* current_port = m_AvailablePorts.empty() ? "No Ports Found" :
			(m_SelectedPortIndex < m_AvailablePorts.size() ? m_AvailablePorts[m_SelectedPortIndex].c_str() : "Error");

		if (ImGui::BeginCombo("COM Port", current_port))
		{
			for (int n = 0; n < (int)m_AvailablePorts.size(); n++)
			{
				if (ImGui::Selectable(m_AvailablePorts[n].c_str(), m_SelectedPortIndex == n))
					m_SelectedPortIndex = n;
			}
			ImGui::EndCombo();
		}

		std::string current_baud = std::to_string(m_BaudRates[m_SelectedBaudIndex]);
		if (ImGui::BeginCombo("Baud Rate", current_baud.c_str()))
		{
			for (int n = 0; n < (int)m_BaudRates.size(); n++)
			{
				if (ImGui::Selectable(std::to_string(m_BaudRates[n]).c_str(), m_SelectedBaudIndex == n))
					m_SelectedBaudIndex = n;
			}
			ImGui::EndCombo();
		}

		ImGui::Separator();

		if (!m_Serial.IsOpen())
		{
			ImGui::BeginDisabled(m_AvailablePorts.empty());
			if (ImGui::Button("Connect to ESP32", ImVec2(-1, 0)))
				m_Serial.Open(m_AvailablePorts[m_SelectedPortIndex], (uint32_t)m_BaudRates[m_SelectedBaudIndex]);
			ImGui::EndDisabled();
		}
		else
		{
			ImGui::TextColored(ImVec4(0, 1, 0, 1), "STATUS: Connected");
			if (ImGui::Button("Disconnect", ImVec2(-1, 0))) m_Serial.Close();
		}
		ImGui::End();

		// --- WINDOW 2: SERIAL MONITOR ---
		ImGui::Begin("Serial Monitor");

		if (ImGui::Button("Copy All"))
			ImGui::SetClipboardText(m_Log.c_str());

		ImGui::SameLine();
		if (ImGui::Button("Clear Output")) m_Log.clear();

		ImGui::SameLine();
		ImGui::TextDisabled("| Total Bytes: %zu", m_Log.size());

		ImGui::Separator();

		ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
		ImGui::TextUnformatted(m_Log.c_str());
		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
		ImGui::EndChild();
		ImGui::End();

		// --- WINDOW 3: VISUALIZATION (ImPlot) ---
		ImGui::Begin("Live Plot");
		if (!m_DataPoints.empty())
		{
			ImGui::Text("Latest: %.3f", m_DataPoints.back());
			ImGui::SameLine();
			ImGui::TextDisabled("| Buffer: %zu/1000", m_DataPoints.size());

			// Set plot height to 250 to match your previous setup
			if (ImPlot::BeginPlot("##LiveData", ImVec2(-1, 250)))
			{
				double x_min = (double)m_TotalPointsProcessed - m_DataPoints.size();
				double x_max = (double)m_TotalPointsProcessed;



				ImPlot::SetupAxes("Samples", "Value");
				ImPlot::SetupAxisLimits(ImAxis_Y1, -10.0, 110.0, ImGuiCond_Once);

				ImPlot::PlotLine("Sensor Input", m_DataPoints.data(), (int)m_DataPoints.size(), 1.0, x_min);

				ImPlot::EndPlot();
			}

			ImGui::TextDisabled("Right-click for options, scroll to zoom, middle-click to pan.");
		}
		else
		{
			ImGui::Text("Waiting for numerical data...");
		}
		ImGui::End();
	}

	TelemetryProject::~TelemetryProject()
	{
		m_Serial.Close();
	}
}