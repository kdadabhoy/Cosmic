// AnalysisSampleLayer.cpp — the WO-10 / X01 analysis reference project. See the header.
#include "AnalysisSampleLayer.h"

#include "layers/WorkspaceLayer.h"
#include "renderer/Renderer2D.h"
#include "renderer/RenderCommand.h"
#include "graphics/FrameBuffer.h"
#include "utils/FileSystem.h"
#include "utils/ImageIO.h"
#include "core/Log.h"

#include <imgui.h>
#include <implot.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace AnalysisSample
{
    namespace
    {
        // Marker / trail / axes colours (RGBA, 0..1). The self-test and the runner
        // oracle compare the exported PNG's marker pixel against kMarker.
        const glm::vec4 kMarker{ 1.00f, 0.25f, 0.10f, 1.0f };   // (255, 64, 26)
        const glm::vec4 kTrail { 0.20f, 0.80f, 1.00f, 1.0f };
        const glm::vec4 kPath  { 0.35f, 0.40f, 0.50f, 1.0f };
        const glm::vec4 kAxes  { 0.60f, 0.60f, 0.60f, 1.0f };
        const glm::u8vec4 kClear{ 26, 26, 26, 255 };             // WorkspaceLayer's viewport clear (0.1)

        constexpr double kMarkerRadiusPx = 6.0;   // marker radius, in viewport pixels
        constexpr double kTrailSeconds   = 2.0;

        glm::u8vec4 ToU8(const glm::vec4& c)
        {
            return { (uint8_t)std::lround(c.r * 255.0f), (uint8_t)std::lround(c.g * 255.0f), (uint8_t)std::lround(c.b * 255.0f), (uint8_t)std::lround(c.a * 255.0f) };
        }
    }

    AnalysisLayer::AnalysisLayer() : Cosmic::Layer("AnalysisSample") {}
    AnalysisLayer::~AnalysisLayer() { SelfTestShutdown(); }

    // ------------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------------
    void AnalysisLayer::OnAttach()
    {
        Cosmic::FileSystem::SetActiveProject("AnalysisSample");
        if (auto* ws = Cosmic::Application::Get().GetWorkspaceLayer())
        {
            ws->DockWindow("Analysis", Cosmic::DockPort::BottomCenter);
            ws->DockWindow("Series",   Cosmic::DockPort::RightTop);
        }
        m_Frame.originX = kOriginX;
        m_Frame.originY = kOriginY;
        m_Loaded = LoadFixtures();
        if (!m_Loaded) CS_ERROR("AnalysisSample: fixture load failed: {0}", m_LoadError);
        else CS_INFO("AnalysisSample: {0} trajectory rows, {1} x {2} series samples, recording at '{3}'",
                     m_Rows.size(), m_Series.size(), m_Series.empty() ? 0 : m_Series[0].size(), m_RecordingDir);
        SelfTestInit();
    }

    void AnalysisLayer::OnDetach()
    {
        SelfTestShutdown();
        m_Player.Unload();
        m_Recorder.reset();
    }

    bool AnalysisLayer::LoadFixtures()
    {
        // 1. F-TRAJECTORY — double rows through the restricted numeric CSV reader.
        const std::string csv = Cosmic::FileSystem::Resolve("project://data/trajectory.csv");
        if (!Trajectory::LoadCsv(csv, m_Rows)) { m_LoadError = "cannot read " + csv; return false; }
        if (m_Rows.size() != (size_t)Trajectory::kSamples) { m_LoadError = "expected 1201 rows, got " + std::to_string(m_Rows.size()); return false; }
        for (int i = 0; i < Trajectory::kSamples; ++i)
        {
            const Trajectory::Row e = Trajectory::At(i);
            const Trajectory::Row& r = m_Rows[(size_t)i];
            const double tol = 1e-12;
            if (std::abs(r.t - e.t) > tol || std::abs(r.x - e.x) > tol * std::max(1.0, std::abs(e.x)) ||
                std::abs(r.y - e.y) > tol * std::max(1.0, std::abs(e.y)) || std::abs(r.vx - e.vx) > tol || std::abs(r.vy - e.vy) > tol * 100.0)
            { m_LoadError = "row " + std::to_string(i) + " does not match the catalog equations"; return false; }
        }
        // The double series in the LOCAL frame: world = origin + x (double), local =
        // world - origin (double). The float display value is taken from THIS, never
        // from a float world value.
        m_T.resize(m_Rows.size()); m_X.resize(m_Rows.size()); m_Y.resize(m_Rows.size()); m_Speed.resize(m_Rows.size());
        for (size_t i = 0; i < m_Rows.size(); ++i)
        {
            const Trajectory::Row& r = m_Rows[i];
            const double worldX = kOriginX + r.x, worldY = kOriginY + r.y;
            m_T[i] = r.t; m_X[i] = worldX - kOriginX; m_Y[i] = worldY - kOriginY;
            m_Speed[i] = std::sqrt(r.vx * r.vx + r.vy * r.vy);
        }

        // 2. F-SERIES-LARGE — generated from the equations + seed in its metadata.
        SeriesLarge::Generate(m_SeriesT, m_Series, m_SeriesMeta);
        if (!SeriesLarge::AllFinite(m_Series)) { m_LoadError = "series has a non-finite sample"; return false; }

        // 3. The float replay path: record the LOCAL coordinates (float) into a v1
        //    recording through the real recorder, then load it into the player.
        m_Recorder = std::make_unique<Cosmic::DataRecorder>();
        const uint32_t id = m_Recorder->Register("trajectory", "x01", { "x", "y", "vx", "vy" });
        m_Recorder->ReserveCapacity(m_Rows.size());
        for (size_t i = 0; i < m_Rows.size(); ++i)
        {
            if (i > 0) m_Recorder->Tick(1.0f / 120.0f);
            m_Recorder->Record(id, { (float)m_X[i], (float)m_Y[i], (float)m_Rows[i].vx, (float)m_Rows[i].vy });
        }
        const std::string exportsRoot = Cosmic::FileSystem::Resolve("user://exports");
        std::error_code ec; fs::create_directories(exportsRoot, ec);
        m_Recorder->Flush(exportsRoot, "trajectory-rec", 120.0f);
        m_Recorder->WaitForFlush();
        m_RecordingDir = (fs::path(exportsRoot) / "trajectory-rec").generic_string();
        if (!m_Player.Load(m_RecordingDir)) { m_LoadError = "DataPlayer cannot load " + m_RecordingDir; return false; }
        m_Player.SetSpeed(1.0f);
        m_ScrubTime = 0.0;
        return true;
    }

    // ------------------------------------------------------------------------
    // Transport
    // ------------------------------------------------------------------------
    void AnalysisLayer::Play()
    {
        if (!m_Loaded) return;
        if (m_Player.GetPosition() >= m_Player.GetDuration()) m_Player.SetPosition(0.0f);
        m_Player.Play(); m_Playing = true;
    }
    void AnalysisLayer::Pause()
    {
        m_Player.Pause(); m_Playing = false;
        m_ScrubTime = (double)m_Player.GetPosition();
    }
    bool AnalysisLayer::IsPlaying() const { return m_Playing && m_Player.IsPlaying(); }
    void AnalysisLayer::Scrub(double seconds)
    {
        if (!m_Loaded) return;
        m_Player.Pause(); m_Playing = false;
        m_ScrubTime = std::clamp(seconds, 0.0, Duration());
        m_Player.SetPosition((float)m_ScrubTime);
    }
    double AnalysisLayer::Position() const { return IsPlaying() ? (double)m_Player.GetPosition() : m_ScrubTime; }
    double AnalysisLayer::Duration() const { return m_Loaded ? (double)Trajectory::At(Trajectory::kSamples - 1).t : 0.0; }

    // ------------------------------------------------------------------------
    // Per frame
    // ------------------------------------------------------------------------
    void AnalysisLayer::OnUpdate(float dt)
    {
        if (m_Loaded && m_Playing)
        {
            m_Player.Tick(dt);
            if (!m_Player.IsPlaying()) { m_Playing = false; m_ScrubTime = (double)m_Player.GetPosition(); }
        }
        RefreshSample();
        RenderScene();
        if (m_ExportPending) CaptureExport();
        SelfTestTick();
    }

    void AnalysisLayer::RefreshSample()
    {
        Sample s;
        if (!m_Loaded) { m_Cur = s; return; }
        const double t = Position();
        s.t = t;
        // Double reference: the equations at t, in the local frame.
        const Trajectory::Row r = Trajectory::AtTime(t);
        s.refX = (kOriginX + r.x) - kOriginX; s.refY = (kOriginY + r.y) - kOriginY;
        s.refVx = r.vx; s.refVy = r.vy; s.refSpeed = std::sqrt(r.vx * r.vx + r.vy * r.vy);
        // The plot's selection: the double series linearly interpolated at t.
        {
            const double u = t * Trajectory::kRateHz;
            size_t i = (size_t)std::clamp((long long)std::floor(u), 0LL, (long long)m_T.size() - 2);
            const double f = std::clamp(u - (double)i, 0.0, 1.0);
            s.plotX = m_X[i] + (m_X[i + 1] - m_X[i]) * f;
            s.plotY = m_Y[i] + (m_Y[i + 1] - m_Y[i]) * f;
            s.plotSpeed = m_Speed[i] + (m_Speed[i + 1] - m_Speed[i]) * f;
        }
        // The float replay path: the marker.
        Cosmic::TelemetryFrame fr;
        if (m_Player.SampleAt("trajectory", (float)t, fr) && fr.values.size() >= 4)
        { s.markerX = fr.values[0]; s.markerY = fr.values[1]; s.markerVx = fr.values[2]; s.markerVy = fr.values[3]; }
        // The float display of the double WORLD value.
        s.localX = m_Frame.ToLocalX(kOriginX + r.x); s.localY = m_Frame.ToLocalY(kOriginY + r.y);
        if (auto fb = Cosmic::Application::Get().GetFrameBuffer())
        {
            s.fbW = fb->GetWidth(); s.fbH = fb->GetHeight();
            if (s.fbW > 0 && s.fbH > 0) s.markerPx = LocalToPixel(s.markerX, s.markerY, s.fbW, s.fbH);
        }
        m_Cur = s;
    }

    glm::mat4 AnalysisLayer::ViewProj(uint32_t w, uint32_t h) const
    {
        // Uniform scale: the x window is fixed, the y window follows the target aspect.
        const double ySpan = (m_View.xmax - m_View.xmin) * (double)h / (double)w;
        return glm::ortho((float)m_View.xmin, (float)m_View.xmax, (float)m_View.ymin, (float)(m_View.ymin + ySpan), -1.0f, 1.0f);
    }

    glm::vec2 AnalysisLayer::LocalToPixel(double x, double y, uint32_t w, uint32_t h) const
    {
        const double ySpan = (m_View.xmax - m_View.xmin) * (double)h / (double)w;
        const double px = (x - m_View.xmin) / (m_View.xmax - m_View.xmin) * (double)w;
        const double py = (1.0 - (y - m_View.ymin) / ySpan) * (double)h;   // top-left origin
        return { (float)px, (float)py };
    }

    void AnalysisLayer::RenderScene()
    {
        if (!m_Loaded) return;
        auto fb = Cosmic::Application::Get().GetFrameBuffer();
        if (!fb) return;
        const uint32_t w = fb->GetWidth(), h = fb->GetHeight();
        if (w == 0 || h == 0) return;
        const double unitsPerPx = (m_View.xmax - m_View.xmin) / (double)w;
        const float r = (float)(kMarkerRadiusPx * unitsPerPx);

        Cosmic::Renderer2D::PushRenderPass(ViewProj(w, h), { 0.0f, 0.0f, (float)w, (float)h });
        // Axes of the local frame.
        Cosmic::Renderer2D::DrawLine({ (float)m_View.xmin, 0.0f, 0.0f }, { (float)m_View.xmax, 0.0f, 0.0f }, kAxes);
        Cosmic::Renderer2D::DrawLine({ 0.0f, (float)m_View.ymin, 0.0f }, { 0.0f, 500.0f, 0.0f }, kAxes);
        // The whole path (faint) from the double series, then the recent trail (bright).
        const double t = m_Cur.t;
        const float dot = (float)(1.5 * unitsPerPx);
        for (size_t i = 1; i < m_X.size(); ++i)
        {
            const bool recent = m_T[i] <= t && m_T[i] >= t - kTrailSeconds;
            Cosmic::Renderer2D::DrawLine({ (float)m_X[i - 1], (float)m_Y[i - 1], 0.0f }, { (float)m_X[i], (float)m_Y[i], 0.0f }, recent ? kTrail : kPath);
            if (recent && (i % 4) == 0)   // a bead every 4 samples makes the trail read at 1 px line width
                Cosmic::Renderer2D::DrawQuad(glm::vec3{ (float)m_X[i], (float)m_Y[i], 0.2f }, { 2.0f * dot, 2.0f * dot }, kTrail);
        }
        // The marker: the float replay sample, drawn on top.
        Cosmic::Renderer2D::DrawCircle(glm::vec3{ m_Cur.markerX, m_Cur.markerY, 0.5f }, { 2.0f * r, 2.0f * r }, kMarker, 1.0f, 0.02f);
        Cosmic::Renderer2D::PopRenderPass();
    }

    // ------------------------------------------------------------------------
    // Export
    // ------------------------------------------------------------------------
    void AnalysisLayer::RequestExport(const std::string& path) { m_ExportPath = path; m_ExportPending = true; }

    void AnalysisLayer::CaptureExport()
    {
        m_ExportPending = false;
        ExportInfo info; info.path = m_ExportPath;
        auto fb = Cosmic::Application::Get().GetFrameBuffer();
        std::vector<uint8_t> rgba; uint32_t w = 0, h = 0;
        // The workspace keeps its viewport framebuffer bound around the client's
        // OnUpdate, and PopRenderPass flushed the scene into it.
        if (!fb || !fb->ReadPixels(0, rgba, w, h) || w == 0 || h == 0)
        {
            CS_ERROR("AnalysisSample: export failed — viewport framebuffer not readable");
            m_LastExport = info; return;
        }
        std::error_code ec; fs::create_directories(fs::path(m_ExportPath).parent_path(), ec);
        info.ok = Cosmic::ImageIO::WritePNG(m_ExportPath, (int)w, (int)h, 4, rgba.data());
        info.width = w; info.height = h;
        info.markerPx = LocalToPixel(m_Cur.markerX, m_Cur.markerY, w, h);
        info.markerColor = ToU8(kMarker); info.clearColor = kClear;
        info.xmin = m_View.xmin; info.xmax = m_View.xmax; info.ymin = m_View.ymin;
        info.ymax = m_View.ymin + (m_View.xmax - m_View.xmin) * (double)h / (double)w;
        info.markerRadiusPx = kMarkerRadiusPx;
        auto at = [&](int x, int y) -> glm::u8vec4
        {
            x = std::clamp(x, 0, (int)w - 1); y = std::clamp(y, 0, (int)h - 1);
            const size_t i = ((size_t)y * w + (size_t)x) * 4;
            return { rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3] };
        };
        info.pixelAtMarker = at((int)std::floor(info.markerPx.x), (int)std::floor(info.markerPx.y));
        info.pixelFar = at(2, 2);
        if (info.ok) CS_INFO("AnalysisSample: exported {0} ({1}x{2}), marker at px ({3}, {4})", m_ExportPath, w, h, info.markerPx.x, info.markerPx.y);
        m_LastExport = info;
    }

    // ------------------------------------------------------------------------
    // UI
    // ------------------------------------------------------------------------
    void AnalysisLayer::OnImGuiRender()
    {
        if (ImGui::Begin("Analysis"))
        {
            if (!m_Loaded)
            {
                ImGui::TextColored({ 1, 0.3f, 0.3f, 1 }, "Fixture load failed: %s", m_LoadError.c_str());
            }
            else
            {
                if (ImGui::Button(IsPlaying() ? "Pause" : "Play")) { if (IsPlaying()) Pause(); else Play(); }
                ImGui::SameLine();
                if (ImGui::Button("Stop")) Scrub(0.0);
                ImGui::SameLine();
                if (ImGui::Button("Export PNG"))
                {
                    char name[64]; std::snprintf(name, sizeof(name), "trajectory-%.4fs.png", m_Cur.t);
                    RequestExport((fs::path(Cosmic::FileSystem::Resolve("user://exports")) / name).generic_string());
                }
                double t = Position(); const double lo = 0.0, hi = Duration();
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderScalar("##scrub", ImGuiDataType_Double, &t, &lo, &hi, "t = %.4f s")) Scrub(t);
                ImGui::Text("t = %.6f s   reference (double): x %.6f  y %.6f  |v| %.4f m/s", m_Cur.t, m_Cur.refX, m_Cur.refY, m_Cur.refSpeed);
                ImGui::Text("marker (float replay): x %.6f  y %.6f   delta vs plot: %.2e / %.2e m",
                            m_Cur.markerX, m_Cur.markerY, (double)m_Cur.markerX - m_Cur.plotX, (double)m_Cur.markerY - m_Cur.plotY);
                ImGui::Text("world origin %.0f m: displayed local x %.6f (double-subtract-then-float)", kOriginX, m_Cur.localX);
                if (m_LastExport.ok) ImGui::Text("last export: %s (%ux%u)", m_LastExport.path.c_str(), m_LastExport.width, m_LastExport.height);

                if (ImPlot::BeginPlot("Position", ImVec2(-1, 220)))
                {
                    ImPlot::SetupAxes("t (s)", "m");
                    ImPlot::PlotLine("x", m_T.data(), m_X.data(), (int)m_T.size());
                    ImPlot::PlotLine("y", m_T.data(), m_Y.data(), (int)m_T.size());
                    const double tt = m_Cur.t;
                    ImPlot::PlotInfLines("t", &tt, 1);
                    ImPlotSpec sel; sel.Marker = ImPlotMarker_Circle; sel.MarkerSize = 5.0f;
                    sel.MarkerFillColor = ImVec4(kMarker.r, kMarker.g, kMarker.b, 1.0f); sel.MarkerLineColor = sel.MarkerFillColor;
                    const double px = m_Cur.plotX, py = m_Cur.plotY;
                    ImPlot::PlotScatter("selected x", &tt, &px, 1, sel);
                    ImPlot::PlotScatter("selected y", &tt, &py, 1, sel);
                    ImPlot::EndPlot();
                }
                if (ImPlot::BeginPlot("Speed", ImVec2(-1, 160)))
                {
                    ImPlot::SetupAxes("t (s)", "m/s");
                    ImPlot::PlotLine("|v|", m_T.data(), m_Speed.data(), (int)m_T.size());
                    const double tt = m_Cur.t, sp = m_Cur.plotSpeed;
                    ImPlot::PlotInfLines("t", &tt, 1);
                    ImPlotSpec sel; sel.Marker = ImPlotMarker_Circle; sel.MarkerSize = 5.0f;
                    sel.MarkerFillColor = ImVec4(kMarker.r, kMarker.g, kMarker.b, 1.0f); sel.MarkerLineColor = sel.MarkerFillColor;
                    ImPlot::PlotScatter("selected", &tt, &sp, 1, sel);
                    ImPlot::EndPlot();
                }
            }
        }
        ImGui::End();

        if (ImGui::Begin("Series"))
        {
            if (m_Loaded)
            {
                ImGui::Text("F-SERIES-LARGE: %d samples x %d channels @ %.0f Hz, seed 0x%llX", SeriesLarge::kSamples, SeriesLarge::kChannels, SeriesLarge::kRateHz, (unsigned long long)SeriesLarge::kSeed);
                if (ImPlot::BeginPlot("Series (100,000 x 8)", ImVec2(-1, -1)))
                {
                    ImPlot::SetupAxes("t (s)", "value");
                    for (int c = 0; c < SeriesLarge::kChannels; ++c)
                        ImPlot::PlotLine(SeriesLarge::Name(c), m_SeriesT.data(), m_Series[(size_t)c].data(), SeriesLarge::kSamples);
                    ImPlot::EndPlot();
                }
            }
        }
        ImGui::End();
    }
}
