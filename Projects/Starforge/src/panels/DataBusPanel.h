#pragma once

// panels/DataBusPanel.h — AP-03 (App Platform): the DataBus panel.
//
// In Play: a live table over the editor-owned play bus (channel, value, age,
// producer, Open — the producer service's CS_SERVICE site through SourceLocator).
// In edit mode: a preview table the user fills, backed by a StarforgeApp-owned
// PREVIEW DataBus that RenderViewport passes to UiSystem::Render with
// preview = true, so bound widgets show authored values while arranging a screen.

#include "../EditorContext.h"
#include "../SourceLocator.h"

#include "data/DataBus.h"

#include <string>

namespace Starforge
{
    class DataBusPanel
    {
    public:
        // `live` is the play bus (read-only here), `preview` the editor's preview
        // bus (edited here). `playing` picks the table.
        void OnImGuiRender(EditorContext& ctx, bool* pOpen, const std::string& projectRoot,
                           const Cosmic::DataBus& live, Cosmic::DataBus& preview, bool playing);

        // The preview-table edit the self-test drives (E08 exercises Open from here).
        static void SetPreview(Cosmic::DataBus& preview, const std::string& channel, const std::string& text);

        // Resolve a live channel's producer source (the "Open" column).
        static SourceHit ProducerHit(const std::string& projectRoot, const std::string& channel, const Cosmic::DataBus& live);

    private:
        char m_NewChannel[96] = "";
        char m_NewValue[64]   = "0";
        char m_Filter[96]     = "";
    };
}
