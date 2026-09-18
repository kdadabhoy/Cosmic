#pragma once
// WO07UiOracle.h — WO-07 L05 (2D stability): the per-action Dear ImGui balance oracle
// shared by the SF_Telem host (tests/WO07UiCyclesFixture.cpp) and the editor host
// (Projects/Starforge/src/L05EditorSelfTest.cpp).
//
// KI-1 proved a whole-frame "no crash" check misses stack imbalance in Release: ImGui
// 1.92 RECOVERS a leaked push at End/EndChild/EndFrame, so the stacks read zero by the
// time a frame is over. Three Release-safe readings catch it anyway, and every scripted
// action is judged on all three:
//   1. the recovered-error callback (g.ErrorCallback): ImGui calls it for EVERY error it
//      recovers from — "Missing PopStyleColor()", "Calling PopID() too many times!",
//      "Missing End()" ... — in Debug and Release alike, naming the window;
//   2. an EndFramePre context hook: the stack depths at the START of EndFrame, i.e. after
//      every layer's OnImGuiRender but BEFORE EndFrame's own recovery pass — a nonzero
//      colour / style-var / font / popup / group depth there is a leak some window's End()
//      already hid;
//   3. the depths read AT THE WIDGET (the KI-1 chip probe / around a layer's render call).
// It also pins the ImGui + ImPlot context pointers and the current font, so a context or
// input-context regression shows up as a per-action failure, not a later crash.
//
// Every module has its own copy of ImGui (static lib) but the CONTEXT is one shared
// object: the callback/hook function pointers below live in the module that installs
// them, so Uninstall() MUST run before that module is unloaded.
#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace CosmicTest
{
    struct UiOracle
    {
        // ---- installed state ----
        ImGuiContext*  ctx    = nullptr;
        ImPlotContext* plot   = nullptr;
        ImGuiID        hookId = 0;
        ImFont*        font   = nullptr;

        // ---- running counters (read after each action) ----
        std::atomic<int>  recoveredErrors{0};    // every g.ErrorCallback invocation
        std::atomic<int>  endFrameLeaks{0};      // EndFramePre readings with any nonzero depth
        std::atomic<int>  contextDrift{0};
        char lastError[256] = {};
        char lastLeak[128]  = {};

        static void OnError(ImGuiContext* g, void* user, const char* msg)
        {
            auto* o = static_cast<UiOracle*>(user);
            ++o->recoveredErrors;
            const char* w = (g && g->CurrentWindow) ? g->CurrentWindow->Name : "(none)";
            std::snprintf(o->lastError, sizeof(o->lastError), "%s: %s", w, msg ? msg : "");
        }
        static void OnEndFramePre(ImGuiContext* g, ImGuiContextHook* hook)
        {
            auto* o = static_cast<UiOracle*>(hook->UserData);
            const int color = g->ColorStack.Size, styleVar = g->StyleVarStack.Size, font = g->FontStack.Size;
            const int popup = g->BeginPopupStack.Size, group = g->GroupStack.Size;
            const int id = g->CurrentWindow ? g->CurrentWindow->IDStack.Size : 1;   // implicit fallback window: 1
            // ImGui 1.92 pushes the frame's default font in NewFrame (UpdateFontsNewFrame) and
            // pops it in EndFrame AFTER this hook: a depth of exactly 1 is the balanced state.
            if (color || styleVar || font != 1 || popup || group || id != 1 || g->CurrentWindowStack.Size != 1)
            {
                ++o->endFrameLeaks;
                std::snprintf(o->lastLeak, sizeof(o->lastLeak), "EndFramePre C%d S%d F%d P%d G%d ID%d W%d",
                              color, styleVar, font, popup, group, id, g->CurrentWindowStack.Size);
            }
        }

        void Install()
        {
            ctx  = ImGui::GetCurrentContext();
            plot = ImPlot::GetCurrentContext();
            font = ImGui::GetFont();
            ctx->ErrorCallback = &UiOracle::OnError;
            ctx->ErrorCallbackUserData = this;
            ImGuiContextHook hook;
            hook.Type = ImGuiContextHookType_EndFramePre;
            hook.Callback = &UiOracle::OnEndFramePre;
            hook.UserData = this;
            hookId = ImGui::AddContextHook(ctx, &hook);
        }
        void Uninstall()
        {
            if (!ctx) return;
            if (ctx->ErrorCallback == &UiOracle::OnError) { ctx->ErrorCallback = nullptr; ctx->ErrorCallbackUserData = nullptr; }
            if (hookId) { ImGui::RemoveContextHook(ctx, hookId); hookId = 0; }
            ctx = nullptr;
        }

        // Call once per frame from the owner: the context/font pins.
        void CheckContexts()
        {
            if (ImGui::GetCurrentContext() != ctx || ImPlot::GetCurrentContext() != plot || !ImGui::GetFont())
                ++contextDrift;
        }

        // A snapshot of the counters, to diff around one scripted action.
        struct Snap { int errors, leaks, drift; };
        Snap Take() const { return { recoveredErrors.load(), endFrameLeaks.load(), contextDrift.load() }; }
        // Describe what changed since `s` (empty string == the action was clean).
        std::string Judge(const Snap& s) const
        {
            std::string why;
            if (recoveredErrors != s.errors) why += std::string("imgui-error(") + lastError + ") ";
            if (endFrameLeaks   != s.leaks)  why += std::string("end-frame-leak(") + lastLeak + ") ";
            if (contextDrift    != s.drift)  why += "context-drift ";
            return why;
        }
    };
}
