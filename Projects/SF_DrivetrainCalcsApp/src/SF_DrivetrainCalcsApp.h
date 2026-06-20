#pragma once

// SF_DrivetrainCalcsApp.h
//
// ============================================================================
// SF_DrivetrainCalcsApp — interactive Shear Force drivetrain calculator
// ============================================================================
//
// A single Cosmic plugin layer that turns the old command-line drivetrain
// tool into a live prototyping bench:
//
//   * Type wheel sizes / pulley counts / battery + motor specs into ImGui
//     panels — the spin-up simulation re-runs instantly (auto-recompute).
//   * Watch Speed / Acceleration / Force / Torque / Distance curves update
//     live in ImPlot, with the rear axle optionally overlaid for comparison.
//   * Read off the headline numbers (top speed, peak g, traction limit,
//     launch behaviour) and a front/rear velocity-sync check.
//   * Export the full time series to a CSV (same config-header format as the
//     original tool).
//   * A to-scale side schematic of the two wheels + pulleys is drawn in the
//     viewport so geometry changes are visible at a glance.
//
// All physics lives in DrivetrainModel.h; this file is purely the front-end.
// ============================================================================

#include <Cosmic.h>
#include "DrivetrainModel.h"

#include <string>
#include <glm/glm.hpp>

namespace Workspace
{
    class SF_DrivetrainCalcsApp : public Cosmic::Layer
    {
    public:
        SF_DrivetrainCalcsApp();
        virtual ~SF_DrivetrainCalcsApp() override = default;

        virtual void OnAttach()                override;
        virtual void OnDetach()                override;
        virtual void OnUpdate(float ts)        override;
        virtual void OnImGuiRender()           override;
        virtual void OnEvent(Cosmic::Event& e) override;

    private:
        // --- ImGui windows ---
        void DrawInspectorTop();    // docked sidebar: headline KPIs + recompute + sync
        void DrawInputsWindow();    // all editable parameters
        void DrawPlotsWindow();     // ImPlot performance curves
        void DrawResultsWindow();   // detailed metrics table + CSV export
        void DrawExplorersWindow(); // tabbed sweeps (wheel dia + pulley ratios)
        void DrawFeasibilityTab();  // tangential-velocity "is it possible" check
        void DrawWheelSweepTab();   // metrics vs wheel diameter
        void DrawPulleyRatioTab();  // metrics vs pulley teeth (+/-5 around current)
        void DrawLiftPlannerTab();      // target a clearance gain; list whole-tooth re-sync options
        void DrawSamePulleyRangeTab();  // clearance range achievable with pulleys untouched

        // --- Helpers ---
        void Recompute();                       // re-run the sim from m_Cfg
        bool ExportCSV(std::string fileName);   // write the config-header CSV
        void RenderSchematic();                 // to-scale wheel/pulley diagram
        void DrawSchematicLabels();             // numeric labels over the viewport

        // One performance curve (front + optional rear overlay).
        void PlotChannel(const char* title, const char* yLabel,
                         const std::vector<double>& frontX,
                         const std::vector<double>& frontY,
                         const std::vector<double>& rearX,
                         const std::vector<double>& rearY,
                         float height);

    private:
        DrivetrainConfig m_Cfg;            // current inputs
        SimOutput        m_Sim;            // latest simulation result
        bool             m_Dirty   = true; // inputs changed since last Recompute
        bool             m_AutoRun = true; // recompute on every edit

        // Baseline snapshot — Results highlights what changed against it.
        DrivetrainConfig m_Baseline;
        SimOutput        m_BaselineSim;
        bool             m_HasBaseline = false;

        bool m_ShowRear   = true;          // overlay the rear axle on the charts
        bool m_FillCurves = true;          // shade the front curve area

        // --- Feasibility (tangential velocity) ---
        double m_FeasTolPct = 1.0;         // max allowed front/rear surface-speed gap

        // --- Wheel diameter sweep ---
        int    m_SweepAxle    = 0;         // 0 = front, 1 = rear
        double m_SweepDiaMin  = 1.0;       // in
        double m_SweepDiaMax  = 6.0;       // in
        double m_SweepDiaStep = 0.25;      // in

        // --- Pulley ratio explorer ---
        int    m_PulleyAxle = 0;           // 0 = front, 1 = rear
        int    m_PulleyVary = 0;           // 0 = wheel pulley, 1 = motor pulley
        int    m_PulleySpan = 5;           // +/- teeth around current

        // --- Lift planner ---
        double m_LiftTarget       = 0.5;   // target ground-clearance increase (always stored in inches)
        bool   m_LiftIncludeMotor = false; // also offer motor-pulley swaps (changes speed more)
        bool   m_LiftUnitMM       = false; // input/display the lift target in mm instead of inches

        // Dock node of "Project Inspector Top" — every other panel tabs into it.
        unsigned int m_LeftDockId = 0;

        // CSV export
        char        m_ExportName[96] = "drivetrain_run";
        std::string m_ExportStatus;        // last export result message

        // Viewport schematic
        Cosmic::OrthographicCameraController m_Camera{ 1280.0f / 720.0f };
        // World-space anchors cached each frame so the ImGui label pass can
        // project them and place text (the renderer has no text primitive).
        glm::vec2 m_FrontWheelWorld = { 0.0f, 0.0f };
        glm::vec2 m_RearWheelWorld  = { 0.0f, 0.0f };
        float     m_FrontWheelDia   = 0.0f;
        float     m_RearWheelDia    = 0.0f;
        bool      m_SchematicValid  = false;
    };

} // namespace Workspace
