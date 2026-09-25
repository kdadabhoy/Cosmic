// WO07PlotFixture.cpp — P01 (2D stability): ImPlot lifetime + known data.
//
// A real project DLL loaded by the real Application. InitializePluginContexts adopts
// the host's ImGui/ImPlot contexts (the ONE supported context model: a plugin never
// creates its own), then every frame the layer draws, under the engine's themed UI:
//   * a time series (F-TRAJECTORY: t=i/120, y=50t-0.5*g*t^2, 1201 samples, linear axes),
//   * an XY plot (x=30t vs y),
//   * a scatter plot (25 known points, circle markers),
//   * a shaded band (y-5 .. y+5),
//   * a three-series plot with a legend,
//   * a log10 Y axis (y=10^(t/2.5)) next to the linear one,
//   * the nonfinite policy (every 7th sample NaN, one +Inf) and an EMPTY series.
// After the auto-fit has settled it asserts, into the exe-owned report:
//   * the current ImGui and ImPlot contexts are exactly the host's;
//   * the fitted axis limits equal the KNOWN data ranges (reference values computed
//     from the closed-form equations in double, never from ImPlot), and nonfinite
//     samples did not move them;
//   * log spacing is equal per decade / linear spacing is equal per step (pixels);
//   * the legend lists exactly the series plotted;
//   * the DRAWN geometry: vertices with the series colour exist at the pixel positions
//     of known samples (line, band and marker) — the rasterizer's actual input;
//   * the ImGui push/pop stacks balance around the plot window;
//   * and a FRONT-BUFFER pixel probe: the colour presented at a known sample's pixel is
//     the line colour (only counted when the window is visible and unoccluded there).
// The plot window is closed and reopened inside every cycle; the host reloads the
// plugin 50 times per process (TransitionToLauncher + reload) — the "reload/reopen 50
// times" of P01 — and applies a different registered theme on every cycle.
#include "WO07PlotReport.h"
#include "WO05NativeWindow.h"

#include <Cosmic.h>
#include "ui/ThemeManager.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>
#include <implot_internal.h>

#include <cmath>
#include <limits>
#include <vector>

namespace
{
    WO07PlotReport* report = nullptr;

    constexpr int    kN = 1201;
    constexpr double kG = 9.80665;

    struct Data
    {
        std::vector<double> t, x, y, yLow, yHigh, yHalf, yQuarter, yLog, yNonfinite, sx, sy;
        double yMax = 0, yMaxFinite = 0, yMinFinite = 0;
        Data()
        {
            t.resize(kN); x.resize(kN); y.resize(kN); yLow.resize(kN); yHigh.resize(kN);
            yHalf.resize(kN); yQuarter.resize(kN); yLog.resize(kN); yNonfinite.resize(kN);
            yMaxFinite = -1e300; yMinFinite = 1e300;
            for (int i = 0; i < kN; ++i)
            {
                const double ti = i / 120.0;                       // F-TRAJECTORY (double reference)
                t[i] = ti; x[i] = 30.0 * ti; y[i] = 50.0 * ti - 0.5 * kG * ti * ti;
                yLow[i] = y[i] - 5.0; yHigh[i] = y[i] + 5.0;
                yHalf[i] = y[i] * 0.5; yQuarter[i] = y[i] * 0.25;
                yLog[i] = std::pow(10.0, ti / 2.5);                 // 1 .. 1e4
                yMax = std::max(yMax, y[i]);
                yNonfinite[i] = (i % 7 == 6) ? std::numeric_limits<double>::quiet_NaN() : y[i];
                if (i == 100) yNonfinite[i] = std::numeric_limits<double>::infinity();
                if (std::isfinite(yNonfinite[i])) { yMaxFinite = std::max(yMaxFinite, yNonfinite[i]); yMinFinite = std::min(yMinFinite, yNonfinite[i]); }
            }
            for (int k = 0; k < 25; ++k) { sx.push_back(k * 12.0); sy.push_back(y[k * 50]); }   // 25 known points
        }
    };
    const Data& D() { static Data d; return d; }

    bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

    // Does the current window's draw list contain a vertex with `col` within `r` px of p?
    bool HasVertexNear(const ImVec2& p, ImU32 col, float r)
    {
        const ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int i = 0; i < dl->VtxBuffer.Size; ++i)
        {
            const ImDrawVert& v = dl->VtxBuffer[i];
            if ((v.col & 0x00FFFFFFu) != (col & 0x00FFFFFFu)) continue;   // RGB match; alpha is the renderer's choice
            const float dx = v.pos.x - p.x, dy = v.pos.y - p.y;
            if (dx * dx + dy * dy <= r * r) return true;
        }
        return false;
    }

    struct Depths { int color, styleVar, font, id; };
    Depths Cap()
    {
        ImGuiContext* g = ImGui::GetCurrentContext();
        Depths d{ g->ColorStack.Size, g->StyleVarStack.Size, g->FontStack.Size, g->CurrentWindow ? g->CurrentWindow->IDStack.Size : 0 };
        return d;
    }

    typedef void (APIENTRY* PFN_glReadBuffer)(unsigned int);
    typedef void (APIENTRY* PFN_glReadPixels)(int, int, int, int, unsigned int, unsigned int, void*);
    typedef void (APIENTRY* PFN_glPixelStorei)(unsigned int, int);
    typedef unsigned int (APIENTRY* PFN_glGetError)();
    typedef void (APIENTRY* PFN_glGetIntegerv)(unsigned int, int*);
    typedef void (APIENTRY* PFN_glBindFramebuffer)(unsigned int, unsigned int);
    typedef void* (WINAPI* PFN_wglGetProcAddress)(const char*);

    class PlotLayer final : public Cosmic::Layer
    {
        int   m_Frame = 0;
        bool  m_HaveProbe = false;
        ImVec2 m_ProbePx{};        // screen-space pixel of the time-series apex sample (previous frame)
        ImU32  m_LineCol = 0;
        const ImVec4 kLine  { 1.0f, 0.35f, 0.10f, 1.0f };
        const ImVec4 kFill  { 0.20f, 0.60f, 1.00f, 1.0f };
        const ImVec4 kMark  { 0.10f, 0.90f, 0.30f, 1.0f };

        void ProbeFrontBufferPixel()
        {
            // Reads the PRESENTED image (front buffer) at the pixel the previous frame
            // plotted the apex sample to; only counted when the window is visible and
            // owns that screen point (pixel ownership), never as a phantom pass.
            if (!m_HaveProbe) return;
            HWND hwnd = WO05NativeWindow(Cosmic::Application::Get().GetWindow());
            POINT pt{ (LONG)m_ProbePx.x, (LONG)m_ProbePx.y };
            if (!hwnd) { ++report->pixelOccluded; ++report->dbgNoHwnd; return; }
            if (!IsWindowVisible(hwnd)) { ++report->pixelOccluded; ++report->dbgNotVisible; return; }
            HWND at = WindowFromPoint(pt);
            if (at != hwnd && GetAncestor(at, GA_ROOT) != hwnd) { ++report->pixelOccluded; ++report->dbgOtherWindow; return; }
            HMODULE gl = GetModuleHandleA("opengl32.dll");
            auto readBuffer = (PFN_glReadBuffer)GetProcAddress(gl, "glReadBuffer");
            auto readPixels = (PFN_glReadPixels)GetProcAddress(gl, "glReadPixels");
            auto pixelStore = (PFN_glPixelStorei)GetProcAddress(gl, "glPixelStorei");
            auto getError   = (PFN_glGetError)GetProcAddress(gl, "glGetError");
            auto getIntegerv = (PFN_glGetIntegerv)GetProcAddress(gl, "glGetIntegerv");
            auto wglGetProc  = (PFN_wglGetProcAddress)GetProcAddress(gl, "wglGetProcAddress");
            auto bindFbo = wglGetProc ? (PFN_glBindFramebuffer)wglGetProc("glBindFramebuffer") : nullptr;
            if (!readBuffer || !readPixels || !pixelStore || !getError || !getIntegerv || !bindFbo) { ++report->pixelOccluded; ++report->dbgNoProc; return; }
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
            const int fbH = (int)(vp->Size.y * scale.y);
            const int px = (int)((m_ProbePx.x - vp->Pos.x) * scale.x);
            const int py = fbH - 1 - (int)((m_ProbePx.y - vp->Pos.y) * scale.y);
            unsigned char rgba[4] = { 0, 0, 0, 0 };
            while (getError() != 0) {}
            // The engine may leave one of its FBOs bound at frame end; the presented image
            // is the DEFAULT framebuffer's front buffer, so bind it for the read and restore.
            int prevRead = 0; getIntegerv(0x8CAA /*READ_FRAMEBUFFER_BINDING*/, &prevRead);
            bindFbo(0x8CA8 /*READ_FRAMEBUFFER*/, 0);
            readBuffer(0x0404 /*FRONT*/);
            pixelStore(0x0D05 /*PACK_ALIGNMENT*/, 1);
            readPixels(px, py, 1, 1, 0x1908 /*RGBA*/, 0x1401 /*UNSIGNED_BYTE*/, rgba);
            readBuffer(0x0405 /*BACK*/);
            bindFbo(0x8CA8 /*READ_FRAMEBUFFER*/, (unsigned)prevRead);
            if (const unsigned err = getError()) { ++report->pixelOccluded; report->dbgGlError = (int)err; return; }
            ++report->pixelChecked;
            report->lastPixelR = rgba[0]; report->lastPixelG = rgba[1]; report->lastPixelB = rgba[2];
            const int er = (int)(kLine.x * 255.0f + 0.5f), eg = (int)(kLine.y * 255.0f + 0.5f), eb = (int)(kLine.z * 255.0f + 0.5f);
            if (std::abs(rgba[0] - er) <= 12 && std::abs(rgba[1] - eg) <= 12 && std::abs(rgba[2] - eb) <= 12) ++report->pixelMatch;
            else ++report->pixelMismatch;
        }

    public:
        PlotLayer() : Cosmic::Layer("WO07 P01 plots") {}

        void OnAttach() override
        {
            // A different registered theme every cycle: the plots render under the real
            // themed UI, not ImGui's defaults.
            const auto& themes = Cosmic::ThemeManager::All();
            if (!themes.empty())
            {
                Cosmic::ThemeManager::Apply(themes[report->cycle % (int)themes.size()].name);
                ++report->themesApplied;
            }
            // Count every recovered ImGui error on the REAL context (Release-safe; this
            // callback lives in this DLL, so OnDetach clears it before we unload).
            ImGui::GetCurrentContext()->ErrorCallback = [](ImGuiContext*, void* user, const char*) { ++static_cast<WO07PlotReport*>(user)->imguiErrors; };
            ImGui::GetCurrentContext()->ErrorCallbackUserData = report;
            ++report->attached;
        }

        void OnUpdate(float) override
        {
            ProbeFrontBufferPixel();   // previous frame's presented pixel (front buffer)
            m_HaveProbe = false;
            ++m_Frame;
            if (m_Frame == 15) ++report->requestUnload;   // this cycle is done: host reloads us
        }

        void OnImGuiRender() override
        {
            if (m_Frame == 8 || m_Frame == 9) return;      // "reopen": close the window for two frames
            if (m_Frame == 10) ++report->reopens;

            if (ImGui::GetCurrentContext() != report->hostImGuiCtx.load() ||
                ImPlot::GetCurrentContext() != report->hostImPlotCtx.load())
                ++report->contextMismatch;

            const Data& d = D();
            const bool inspect = m_Frame >= 4;   // auto-fit applies at EndPlot; limits are settled by now
            const Depths before = Cap();

            ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->Pos.x + 40, ImGui::GetMainViewport()->Pos.y + 60), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(1100, 640), ImGuiCond_Always);
            ImGui::Begin("WO07 P01 plots", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
            ++report->framesPlotted;
            const ImVec2 cell(340, 190);
            ImPlotSpec line; line.LineColor = kLine; line.LineWeight = 4.0f;
            m_LineCol = ImGui::GetColorU32(kLine);

            // ---- 1. time series, linear axes ----
            if (ImPlot::BeginPlot("Time series y(t)", cell))
            {
                ImPlot::SetupAxes("t [s]", "y [m]", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
                ImPlot::PlotLine("y(t)", d.t.data(), d.y.data(), kN, line);
                if (inspect)
                {
                    const ImPlotRect lim = ImPlot::GetPlotLimits();
                    report->fitXMinE6 = (long long)(lim.X.Min * 1e6); report->fitXMaxE6 = (long long)(lim.X.Max * 1e6);
                    report->fitYMinE6 = (long long)(lim.Y.Min * 1e6); report->fitYMaxE6 = (long long)(lim.Y.Max * 1e6);
                    if (!Near(lim.X.Min, 0.0, 1e-6) || !Near(lim.X.Max, 10.0, 1e-6) ||
                        !Near(lim.Y.Min, 0.0, 1e-6) || !Near(lim.Y.Max, d.yMax, 1e-6 + 1e-5 * d.yMax))
                        ++report->limitMismatch;
                    // linear: equal pixel spacing per equal value step
                    const float p10 = ImPlot::PlotToPixels(0.0, 10.0).y, p20 = ImPlot::PlotToPixels(0.0, 20.0).y, p30 = ImPlot::PlotToPixels(0.0, 30.0).y;
                    if (!Near((double)(p10 - p20), (double)(p20 - p30), 1.0)) ++report->logAxisMismatch;
                    // drawn geometry at known samples
                    for (int k : { 0, 300, 612, 900, 1200 })
                    {
                        const ImVec2 p = ImPlot::PlotToPixels(d.t[k], d.y[k]);
                        if (!HasVertexNear(p, m_LineCol, 3.5f)) ++report->vertexMiss;
                    }
                    m_ProbePx = ImPlot::PlotToPixels(d.t[612], d.y[612]);
                    m_HaveProbe = true;
                }
                ImPlot::EndPlot();
            }
            ImGui::SameLine();
            // ---- 2. XY ----
            if (ImPlot::BeginPlot("XY y(x)", cell))
            {
                ImPlot::SetupAxes("x [m]", "y [m]", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
                ImPlot::PlotLine("y(x)", d.x.data(), d.y.data(), kN, line);
                if (inspect)
                {
                    const ImPlotRect lim = ImPlot::GetPlotLimits();
                    if (!Near(lim.X.Min, 0.0, 1e-6) || !Near(lim.X.Max, 300.0, 1e-6) || !Near(lim.Y.Max, d.yMax, 1e-6 + 1e-5 * d.yMax))
                        ++report->limitMismatch;
                    if (!HasVertexNear(ImPlot::PlotToPixels(d.x[600], d.y[600]), m_LineCol, 3.5f)) ++report->vertexMiss;
                }
                ImPlot::EndPlot();
            }
            ImGui::SameLine();
            // ---- 3. scatter ----
            if (ImPlot::BeginPlot("Scatter", cell))
            {
                ImPlot::SetupAxes("x", "y", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
                ImPlotSpec mk; mk.Marker = ImPlotMarker_Circle; mk.MarkerSize = 6.0f; mk.MarkerFillColor = kMark; mk.MarkerLineColor = kMark; mk.LineColor = kMark;
                ImPlot::PlotScatter("pts", d.sx.data(), d.sy.data(), 25, mk);
                if (inspect)
                {
                    const ImPlotRect lim = ImPlot::GetPlotLimits();
                    if (!Near(lim.X.Min, 0.0, 1e-6) || !Near(lim.X.Max, 288.0, 1e-6)) ++report->limitMismatch;
                    const ImU32 mc = ImGui::GetColorU32(kMark);
                    for (int k : { 0, 12, 24 })
                        if (!HasVertexNear(ImPlot::PlotToPixels(d.sx[k], d.sy[k]), mc, 7.0f)) ++report->scatterMiss;
                }
                ImPlot::EndPlot();
            }
            // ---- 4. shaded band ----
            if (ImPlot::BeginPlot("Band y+-5", cell))
            {
                ImPlot::SetupAxes("t", "y", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
                ImPlotSpec fill; fill.FillColor = kFill; fill.FillAlpha = 0.5f; fill.LineColor = kFill;
                ImPlot::PlotShaded("band", d.t.data(), d.yLow.data(), d.yHigh.data(), kN, fill);
                if (inspect)
                {
                    const ImPlotRect lim = ImPlot::GetPlotLimits();
                    if (!Near(lim.Y.Min, -5.0, 1e-6) || !Near(lim.Y.Max, d.yMax + 5.0, 1e-6 + 1e-5 * d.yMax)) ++report->limitMismatch;
                    const ImU32 fc = ImGui::GetColorU32(ImVec4(kFill.x, kFill.y, kFill.z, 0.5f));
                    for (int k : { 100, 612, 1100 })
                    {
                        if (!HasVertexNear(ImPlot::PlotToPixels(d.t[k], d.yLow[k]),  fc, 2.5f)) ++report->shadedMiss;
                        if (!HasVertexNear(ImPlot::PlotToPixels(d.t[k], d.yHigh[k]), fc, 2.5f)) ++report->shadedMiss;
                    }
                }
                ImPlot::EndPlot();
            }
            ImGui::SameLine();
            // ---- 5. multi-series legend ----
            if (ImPlot::BeginPlot("Legend (3 series)", cell))
            {
                ImPlot::SetupAxes("t", "y", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
                ImPlot::PlotLine("A: y",    d.t.data(), d.y.data(),       kN);
                const ImVec4 cA = ImPlot::GetLastItemColor();
                ImPlot::PlotLine("B: y/2",  d.t.data(), d.yHalf.data(),    kN);
                const ImVec4 cB = ImPlot::GetLastItemColor();
                ImPlot::PlotLine("C: y/4",  d.t.data(), d.yQuarter.data(), kN);
                const ImVec4 cC = ImPlot::GetLastItemColor();
                if (inspect)
                {
                    ImPlotPlot* plot = ImPlot::GetCurrentPlot();
                    const int entries = plot ? plot->Items.GetLegendCount() : -1;
                    report->legendEntries = entries;
                    if (entries != 3) ++report->legendMismatch;
                    const ImU32 a = ImGui::GetColorU32(cA), b = ImGui::GetColorU32(cB), c = ImGui::GetColorU32(cC);
                    if (a == b || b == c || a == c) ++report->legendMismatch;   // distinct colormap colours
                    if (!HasVertexNear(ImPlot::PlotToPixels(d.t[612], d.yHalf[612]), b, 3.0f)) ++report->vertexMiss;
                }
                ImPlot::EndPlot();
            }
            ImGui::SameLine();
            // ---- 6. log10 Y axis ----
            if (ImPlot::BeginPlot("Log10 Y", cell))
            {
                ImPlot::SetupAxes("t", "10^(t/2.5)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
                ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
                ImPlot::PlotLine("log", d.t.data(), d.yLog.data(), kN, line);
                if (inspect)
                {
                    const ImPlotRect lim = ImPlot::GetPlotLimits();
                    if (!Near(lim.Y.Min, 1.0, 1e-6) || !Near(lim.Y.Max, 1e4, 1e-6 + 1e-5 * 1e4)) ++report->limitMismatch;
                    const float p1 = ImPlot::PlotToPixels(0.0, 1.0).y, p10 = ImPlot::PlotToPixels(0.0, 10.0).y, p100 = ImPlot::PlotToPixels(0.0, 100.0).y;
                    if (!Near((double)(p1 - p10), (double)(p10 - p100), 1.0)) ++report->logAxisMismatch;   // equal per decade
                    if (!HasVertexNear(ImPlot::PlotToPixels(d.t[600], d.yLog[600]), m_LineCol, 3.5f)) ++report->vertexMiss;
                }
                ImPlot::EndPlot();
            }
            // ---- 7. nonfinite policy (skip NaN/Inf) + empty series ----
            if (ImPlot::BeginPlot("Nonfinite + empty", cell))
            {
                ImPlot::SetupAxes("t", "y", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
                ImPlot::PlotLine("nonfinite", d.t.data(), d.yNonfinite.data(), kN, line);
                ImPlot::PlotLine("empty", d.t.data(), d.y.data(), 0, line);   // zero samples: must be inert
                if (inspect)
                {
                    const ImPlotRect lim = ImPlot::GetPlotLimits();
                    if (!Near(lim.Y.Min, d.yMinFinite, 1e-6) || !Near(lim.Y.Max, d.yMaxFinite, 1e-6 + 1e-5 * d.yMaxFinite) ||
                        !std::isfinite(lim.Y.Min) || !std::isfinite(lim.Y.Max))
                        ++report->nonfiniteLeak;
                }
                ImPlot::EndPlot();
            }
            ImGui::SameLine();
            if (ImPlot::BeginPlot("Only empty", cell))
            {
                ImPlot::SetupAxes("t", "y", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
                ImPlot::PlotLine("empty", d.t.data(), d.y.data(), 0, line);
                if (inspect)
                {
                    const ImPlotRect lim = ImPlot::GetPlotLimits();
                    if (!std::isfinite(lim.X.Min) || !std::isfinite(lim.Y.Max) || lim.X.Min >= lim.X.Max || lim.Y.Min >= lim.Y.Max)
                        ++report->emptySeriesProblem;   // default range must stay a finite, non-degenerate box
                    if (HasVertexNear(ImPlot::PlotToPixels(0.5, 0.5), m_LineCol, 40.0f)) ++report->emptySeriesProblem;
                }
                ImPlot::EndPlot();
            }
            if (inspect) ++report->framesInspected;
            report->drawVertices = ImGui::GetWindowDrawList()->VtxBuffer.Size;
            ImGui::End();

            const Depths after = Cap();
            if (after.color != before.color || after.styleVar != before.styleVar || after.font != before.font || after.id != before.id)
                ++report->stackImbalance;
        }

        void OnDetach() override
        {
            ImGui::GetCurrentContext()->ErrorCallback = nullptr;   // this DLL's code: never leave it installed
            ImGui::GetCurrentContext()->ErrorCallbackUserData = nullptr;
            ++report->detached;
        }
        ~PlotLayer() override { ++report->destroyed; }
    };
}

extern "C"
{
    __declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context)
    {
        // The ONE supported context model: adopt the host's contexts into this module's
        // ImGui/ImPlot globals. Record them (first adoption) and verify every later
        // adoption hands over the same non-null pointers.
        ImGui::SetCurrentContext(context.ImGuiCtx);
        ImPlot::SetCurrentContext(context.ImPlotCtx);
        char* value = nullptr; size_t len = 0;
        _dupenv_s(&value, &len, "COSMIC_WO07_PLOT_REPORT");
        if (value) { report = reinterpret_cast<WO07PlotReport*>(_strtoui64(value, nullptr, 16)); free(value); }
        if (!report) return;
        ++report->adoptions;
        if (!context.ImGuiCtx || !context.ImPlotCtx) ++report->adoptionMismatch;
        if (!report->hostImGuiCtx.load()) { report->hostImGuiCtx = context.ImGuiCtx; report->hostImPlotCtx = context.ImPlotCtx; }
        else if (report->hostImGuiCtx.load() != context.ImGuiCtx || report->hostImPlotCtx.load() != context.ImPlotCtx) ++report->adoptionMismatch;
    }
    __declspec(dllexport) Cosmic::Layer* CreatePluginLayer()
    {
        if (!report) return nullptr;
        return new PlotLayer();
    }
}
CS_TEST_FIXTURE()   // UX-03: hidden from the Launcher project scan (KI-77)
