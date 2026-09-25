// WO10ClockFixture.cpp — WO-10 (2D stability): the clock-probe runtime plugin.
//
// A real project DLL (loaded by the real Application through the real
// WorkspaceLayer, adopting the host's ImGui/ImPlot contexts) whose only job is to
// observe what the production dispatch hands a plugin layer every frame:
//   * OnFixedUpdate(dt) — WorkspaceLayer::OnFixedUpdate forwards the engine's fixed
//     delta multiplied by this layer's LOCAL time scale;
//   * OnUpdate(dt)      — WorkspaceLayer::OnUpdate forwards the engine's scaled
//     variable delta multiplied by the LOCAL scale, after UpdateLayerTime(ts) has
//     advanced GetLocalTime() by ts * local.
// It records those into the exe-owned WO10ClockReport (pointer via env var) and
// never schedules, sleeps, or touches the clock. COSMIC_WO10_LOCAL_SCALE sets the
// plugin-local scale it applies to itself on attach (default 1) — the N01 rung that
// distinguishes global from plugin-local scaling uses it.
#include "WO10ClockReport.h"

#include <Cosmic.h>
#include "core/Log.h"

#include <imgui.h>
#include <implot.h>

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace
{
    WO10ClockReport* report = nullptr;

    class ClockProbeLayer final : public Cosmic::Layer
    {
    public:
        ClockProbeLayer() : Cosmic::Layer("WO10 clock probe") {}

        void OnAttach() override
        {
            char* value = nullptr; size_t len = 0;
            _dupenv_s(&value, &len, "COSMIC_WO10_LOCAL_SCALE");
            float local = 1.0f;
            if (value) { local = (float)std::atof(value); free(value); }
            SetTimeScale(local);                 // Layer::SetTimeScale — the plugin-LOCAL scale
            report->pluginLocalScale.store(local);
            ++report->attached;
        }

        void OnFixedUpdate(float dt) override
        {
            if (!std::isfinite(dt)) ++report->pluginNonFinite;
            const int n = report->pluginTicksNow.load() + 1;
            report->pluginTicksNow.store(n);
            report->pluginFixedDtSumNow.store(report->pluginFixedDtSumNow.load() + dt);
            if (n == 1) { report->pluginFixedDtMinNow.store(dt); report->pluginFixedDtMaxNow.store(dt); }
            else
            {
                if (dt < report->pluginFixedDtMinNow.load()) report->pluginFixedDtMinNow.store(dt);
                if (dt > report->pluginFixedDtMaxNow.load()) report->pluginFixedDtMaxNow.store(dt);
            }
        }

        void OnUpdate(float dt) override
        {
            const float local = GetLocalTime();
            if (!std::isfinite(dt) || !std::isfinite(local)) ++report->pluginNonFinite;
            if (local < report->pluginLocalTimeNow.load())   // previous frame's value (never reset)
                ++report->localTimeDecreased;
            report->pluginUpdateDtNow.store(dt);
            report->pluginLocalTimeNow.store(local);
            report->pluginUpdatesNow.store(report->pluginUpdatesNow.load() + 1);
        }

        void OnDetach() override { ++report->detached; }
        ~ClockProbeLayer() override { ++report->destroyed; }
    };
}

extern "C"
{
    __declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context)
    {
        ImGui::SetCurrentContext(context.ImGuiCtx);
        ImPlot::SetCurrentContext(context.ImPlotCtx);
    }
    __declspec(dllexport) Cosmic::Layer* CreatePluginLayer()
    {
        char* value = nullptr; size_t len = 0;
        _dupenv_s(&value, &len, "COSMIC_WO10_CLOCK_REPORT");
        if (!value) return nullptr;
        report = reinterpret_cast<WO10ClockReport*>(_strtoui64(value, nullptr, 16));
        free(value);
        if (!report) return nullptr;
        return new ClockProbeLayer();
    }
}
CS_TEST_FIXTURE()   // UX-03: hidden from the Launcher project scan (KI-77)
