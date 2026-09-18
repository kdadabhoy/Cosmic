// AnalysisSample.cpp — plugin entry points of the WO-10 / X01 analysis reference
// project: the two exports every Cosmic runtime plugin provides. The host
// (CosmicApp.exe, renamed by the packager) hands over its ImGui/ImPlot contexts,
// then asks for the layer it will mount in its WorkspaceLayer.
#include "AnalysisSampleLayer.h"

#include <imgui.h>
#include <implot.h>

extern "C"
{
    __declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context)
    {
        ImGui::SetCurrentContext(context.ImGuiCtx);
        ImPlot::SetCurrentContext(context.ImPlotCtx);
    }

    __declspec(dllexport) Cosmic::Layer* CreatePluginLayer()
    {
        return new AnalysisSample::AnalysisLayer();
    }
}
