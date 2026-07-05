#pragma once

// ConsolePanel.h — editor console.
//
// SKELETON: shows EditorContext::ConsoleLines (editor-emitted messages only).
//   TODO(E6):  an engine log ring-buffer sink feeds CS_INFO/WARN/ERROR here
//              with severity colors + filters.
//   TODO(E12): BuildRunner streams cmake/compiler output here (clickable
//              file:line errors).

#include "EditorContext.h"

namespace Starforge
{
    class ConsolePanel
    {
    public:
        // Draws the "Console" window. Dock binding happens in StarforgeApp.
        void OnImGuiRender(EditorContext& ctx, bool* pOpen = nullptr);

    private:
        bool m_AutoScroll = true;
        // Severity filter toggles (H7) — engine + editor lines share the panel now.
        bool m_ShowInfo  = true;
        bool m_ShowWarn  = true;
        bool m_ShowError = true;
    };
}
