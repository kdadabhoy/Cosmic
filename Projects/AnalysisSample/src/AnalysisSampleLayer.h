#pragma once
// AnalysisSampleLayer.h — the WO-10 / X01 analysis reference project (2D stability).
//
// A plain Cosmic runtime plugin (one Layer, hosted by the engine's WorkspaceLayer)
// that consumes the two synthetic fixtures (AnalysisFixtures.h) through the
// engine's EXISTING primitives only:
//   * F-TRAJECTORY (1,201 double rows) is loaded from project://data/trajectory.csv
//     with the restricted numeric CSV reader (DataExport::LoadCSV), verified
//     against the catalog equations, and placed in a scientific world frame with a
//     1e11 m origin offset — the DOUBLE source of truth;
//   * the LOCAL (origin-relative) coordinates are recorded through DataRecorder into
//     a v1 recording and played back with DataPlayer — the FLOAT telemetry/replay
//     path that drives the animated marker and trail (play / pause / scrub);
//   * position and speed (and the 100,000 x 8 F-SERIES-LARGE channels, generated
//     from the equations + seed in their metadata) are plotted with ImPlot from the
//     double arrays;
//   * the marker/trail scene is drawn with Renderer2D into the workspace viewport
//     framebuffer, and "Export PNG" reads that framebuffer back (FrameBuffer::
//     ReadPixels) and writes it with ImageIO::WritePNG, recording the marker's
//     pixel position from the projection.
// A self-test state machine (COSMIC_X01_SELFTEST=<result.json>) drives load -> play
// -> pause -> scrub at exact and midpoint times -> export -> finish through the
// same transport functions the UI buttons call, asserting numerically inside the
// app and writing a JSON result the runner wrapper checks again OUT OF PROCESS.
//
// Compatibility specimen only (D-9km): synthetic fixtures, no real to-9km schema;
// the 1e11 offset tests THIS sample's conversion, not an engine-wide promise.
#include <Cosmic.h>
#include "telemetry/DataRecorder.h"
#include "telemetry/DataPlayer.h"

#include "AnalysisFixtures.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace AnalysisSample
{
    class AnalysisLayer final : public Cosmic::Layer
    {
    public:
        AnalysisLayer();
        ~AnalysisLayer() override;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(float dt) override;
        void OnImGuiRender() override;

        // ---- Transport (what the UI buttons call; what the self-test drives) ----
        void   Play();
        void   Pause();
        bool   IsPlaying() const;
        void   Scrub(double seconds);          // exact seek; the authoritative time is kept in double
        double Position() const;               // the replay head (double; from the player's float)
        double Duration() const;

        // ---- The current sample, refreshed every frame ----
        struct Sample
        {
            double t = 0.0;                    // the time the marker/plot are showing
            // The double reference (the equations at t) and the plot's selection
            // (the double series linearly interpolated at t — a chord between rows).
            double refX = 0, refY = 0, refVx = 0, refVy = 0, refSpeed = 0;
            double plotX = 0, plotY = 0, plotSpeed = 0;
            // The float replay path (DataPlayer::GetFrame): what the marker draws.
            float  markerX = 0, markerY = 0, markerVx = 0, markerVy = 0;
            // The float display frame of the DOUBLE world value (1e11 + x) - 1e11.
            float  localX = 0, localY = 0;
            // The marker's pixel in the viewport framebuffer (top-left origin), from
            // the projection this frame used.
            glm::vec2 markerPx{ 0, 0 };
            uint32_t  fbW = 0, fbH = 0;
        };
        const Sample& Current() const { return m_Cur; }

        // ---- Export ----
        struct ExportInfo
        {
            bool        ok = false;
            std::string path;
            uint32_t    width = 0, height = 0;
            glm::vec2   markerPx{ 0, 0 };           // continuous pixel (top-left origin)
            glm::u8vec4 markerColor{ 0, 0, 0, 0 };  // what the marker was drawn with
            glm::u8vec4 clearColor{ 0, 0, 0, 0 };   // the viewport clear
            glm::u8vec4 pixelAtMarker{ 0, 0, 0, 0 };
            glm::u8vec4 pixelFar{ 0, 0, 0, 0 };
            double      xmin = 0, xmax = 0, ymin = 0, ymax = 0;   // the projection window (local units)
            double      markerRadiusPx = 0;
        };
        // Requests a capture of the NEXT frame's scene into `path`; result via LastExport().
        void RequestExport(const std::string& path);
        const ExportInfo& LastExport() const { return m_LastExport; }
        bool ExportPending() const { return m_ExportPending; }

        // ---- Data access for the self-test / UI ----
        const std::vector<Trajectory::Row>& Rows() const { return m_Rows; }
        const SeriesLarge::Meta& SeriesMeta() const { return m_SeriesMeta; }
        const std::vector<std::vector<double>>& Series() const { return m_Series; }
        const LocalFrame& Frame() const { return m_Frame; }
        bool  Loaded() const { return m_Loaded; }
        const std::string& LoadError() const { return m_LoadError; }
        const std::string& RecordingDir() const { return m_RecordingDir; }
        std::string SeriesMetaJson() const { return SeriesLarge::MetaJson(m_SeriesMeta); }

        static constexpr double kOriginX = 1e11;   // the scientific world origin (m)
        static constexpr double kOriginY = 1e11;

    private:
        bool LoadFixtures();
        void RefreshSample();
        void RenderScene();
        void CaptureExport();

        // Projection of the local frame into the viewport framebuffer.
        struct View { double xmin = -10.0, xmax = 310.0, ymin = -10.0, ymax = 140.0; } m_View;
        glm::mat4 ViewProj(uint32_t w, uint32_t h) const;
        glm::vec2 LocalToPixel(double x, double y, uint32_t w, uint32_t h) const;

        // Double sources.
        std::vector<Trajectory::Row>       m_Rows;
        std::vector<double>                m_T, m_X, m_Y, m_Speed;       // plot arrays (local frame, double)
        std::vector<double>                m_SeriesT;
        std::vector<std::vector<double>>   m_Series;
        SeriesLarge::Meta                  m_SeriesMeta;
        LocalFrame                         m_Frame;

        // Float replay path.
        std::unique_ptr<Cosmic::DataRecorder> m_Recorder;
        Cosmic::DataPlayer                    m_Player;
        std::string                           m_RecordingDir;
        double                                m_ScrubTime = 0.0;   // authoritative while not playing
        bool                                  m_Playing = false;

        Sample      m_Cur;
        bool        m_Loaded = false;
        std::string m_LoadError;

        bool        m_ExportPending = false;
        std::string m_ExportPath;
        ExportInfo  m_LastExport;

        // Self-test (X01SelfTest.cpp).
        struct SelfTest;
        SelfTest* m_SelfTest = nullptr;
        void SelfTestInit();
        void SelfTestTick();
        void SelfTestShutdown();
    };
}
