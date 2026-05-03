#include "TelemetryProject.h"
#include <imgui.h>
#include <windows.h> // Required for GetCurrentProcessorNumber


namespace Workspace
{
	TelemetryProject::TelemetryProject()
	{
		m_DataPoints.reserve(500);
		m_Log.reserve(8192);
		// Initial scan for ports
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

					m_Log += "[RAW]: " + line + "\n";
					if (m_Log.size() > 10000)
						m_Log.erase(0, 2000);

					try
					{
						float val = std::stof(line);
						m_DataPoints.push_back(val);
						if (m_DataPoints.size() > 500)
							m_DataPoints.erase(m_DataPoints.begin());
					}
					catch (...) {}
				}
			}
		}
	}

	void TelemetryProject::OnImGuiRender()
	{
		ImGui::Begin("Serial Settings");

		if (ImGui::Button("Refresh Ports"))
		{
			m_AvailablePorts = Cosmic::SerialPort::GetAvailablePorts();
			// Safety: Reset index if it's now out of bounds
			if (m_SelectedPortIndex >= m_AvailablePorts.size())
				m_SelectedPortIndex = 0;
		}

		// Port Selection Dropdown
		const char* current_port = m_AvailablePorts.empty() ? "No Ports Found" :
			(m_SelectedPortIndex < m_AvailablePorts.size() ? m_AvailablePorts[m_SelectedPortIndex].c_str() : "Error");

		if (ImGui::BeginCombo("COM Port", current_port))
		{
			for (int n = 0; n < m_AvailablePorts.size(); n++)
			{
				const bool is_selected = (m_SelectedPortIndex == n);
				if (ImGui::Selectable(m_AvailablePorts[n].c_str(), is_selected))
					m_SelectedPortIndex = n;
			}
			ImGui::EndCombo();
		}

		// Baud Rate Selection Dropdown
		std::string current_baud = std::to_string(m_BaudRates[m_SelectedBaudIndex]);
		if (ImGui::BeginCombo("Baud Rate", current_baud.c_str()))
		{
			for (int n = 0; n < m_BaudRates.size(); n++)
			{
				const bool is_selected = (m_SelectedBaudIndex == n);
				if (ImGui::Selectable(std::to_string(m_BaudRates[n]).c_str(), is_selected))
					m_SelectedBaudIndex = n;
			}
			ImGui::EndCombo();
		}

		ImGui::Separator();

		if (!m_Serial.IsOpen())
		{
			ImGui::BeginDisabled(m_AvailablePorts.empty());
			if (ImGui::Button("Connect to ESP32", ImVec2(-1, 0)))
			{
				m_Serial.Open(m_AvailablePorts[m_SelectedPortIndex], (uint32_t)m_BaudRates[m_SelectedBaudIndex]);
			}
			ImGui::EndDisabled();
		}
		else
		{
			ImGui::TextColored(ImVec4(0, 1, 0, 1), "STATUS: Connected to %s", m_AvailablePorts[m_SelectedPortIndex].c_str());
			if (ImGui::Button("Disconnect", ImVec2(-1, 0)))
				m_Serial.Close();
		}
		ImGui::End();

		// --- WINDOW 2: SERIAL MONITOR ---
		ImGui::Begin("Serial Monitor");
		if (ImGui::Button("Clear Output")) m_Log.clear();
		ImGui::SameLine();
		ImGui::TextDisabled("| Total Bytes: %zu", m_Log.size());
		ImGui::Separator();

		ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
		ImGui::TextUnformatted(m_Log.c_str());

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


	TelemetryProject::~TelemetryProject()
	{
		m_Serial.Close(); // This sets m_Connected to false and joins the thread
	}
}