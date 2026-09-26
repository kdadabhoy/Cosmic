// AssetEditorHost.cpp — see AssetEditorHost.h.

#include "AssetEditorHost.h"
#include "EditorContext.h"

#include <imgui.h>

namespace Starforge
{
    IAssetEditor* AssetEditorHost::Open(const std::string& vfsPath, const Factory& make,
                                        bool* showFlag)
    {
        // One instance per path — re-focus an already-open document.
        for (auto& d : m_Docs)
        {
            if (d.Editor->Path() == vfsPath)
            {
                m_FocusPath = vfsPath;
                m_WantFocus = true;
                if (showFlag) *showFlag = true;
                return d.Editor.get();
            }
        }

        std::unique_ptr<IAssetEditor> ed = make ? make() : nullptr;
        if (!ed)
            return nullptr;

        IAssetEditor* raw = ed.get();
        m_Docs.push_back({ std::move(ed), m_NextId++ });
        m_FocusPath = vfsPath;
        m_WantFocus = true;
        if (showFlag) *showFlag = true;
        return raw;
    }

    bool AssetEditorHost::AnyDirty() const
    {
        for (const auto& d : m_Docs)
            if (d.Editor->Dirty())
                return true;
        return false;
    }

    IAssetEditor* AssetEditorHost::Find(const std::string& vfsPath) const
    {
        for (const auto& d : m_Docs)
            if (d.Editor->Path() == vfsPath)
                return d.Editor.get();
        return nullptr;
    }

    bool AssetEditorHost::Close(const std::string& vfsPath)
    {
        if (!Find(vfsPath))
            return false;
        Remove(vfsPath);
        return true;
    }

    uint32_t AssetEditorHost::TabId(const std::string& vfsPath) const
    {
        for (const auto& d : m_Docs)
            if (d.Editor->Path() == vfsPath)
                return d.Id;
        return 0;
    }

    bool AssetEditorHost::ShouldDraw(bool showFlag) const
    {
        return showFlag;
    }

    void AssetEditorHost::LogDirty(EditorContext& ctx, const char* why) const
    {
        for (const auto& d : m_Docs)
            if (d.Editor->Dirty())
                ctx.Log(std::string("[Editors] ") + why + " drops the unsaved changes in \"" +
                        d.Editor->Title() + "\" (" + d.Editor->Path() + ").", LogSeverity::Warn);
    }

    void AssetEditorHost::OnUpdate(EditorContext& ctx, float ts)
    {
        for (auto& d : m_Docs)
            d.Editor->OnUpdate(ctx, ts);
    }

    void AssetEditorHost::Remove(const std::string& path)
    {
        for (auto it = m_Docs.begin(); it != m_Docs.end(); ++it)
        {
            if (it->Editor->Path() == path)
            {
                m_Docs.erase(it);
                return;
            }
        }
    }

    void AssetEditorHost::OnImGuiRender(EditorContext& ctx, bool* open)
    {
        // UX-01 (KI-66): a usable first-use size (floating case; the built-in presets dock
        // it at Center) and focus on the frame after Open() added / re-focused a document.
        ImGui::SetNextWindowSize(ImVec2(1100.0f, 680.0f), ImGuiCond_FirstUseEver);
        if (m_WantFocus)
        {
            ImGui::SetNextWindowFocus();
            m_WantFocus = false;
        }
        if (!ImGui::Begin("Editors", open, ImGuiWindowFlags_NoScrollbar))
        {
            ImGui::End();
            return;
        }

        if (m_Docs.empty())
        {
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            ImGui::TextDisabled("No flow or story document open.");
            ImGui::TextWrapped("Double-click a .cflow or .cstory in the Content Browser (or use "
                               "Screens ▸ Flow graph) to open it as a document here.");
            ImGui::End();
            return;
        }

        std::string closeRequest;   // path whose tab ✕ was clicked this frame

        const ImGuiTabBarFlags barFlags = ImGuiTabBarFlags_Reorderable
                                        | ImGuiTabBarFlags_AutoSelectNewTabs
                                        | ImGuiTabBarFlags_TabListPopupButton
                                        | ImGuiTabBarFlags_FittingPolicyScroll;
        if (ImGui::BeginTabBar("##editor-docs", barFlags))
        {
            for (size_t i = 0; i < m_Docs.size(); ++i)
            {
                IAssetEditor* doc = m_Docs[i].Editor.get();
                const uint32_t id = m_Docs[i].Id;
                bool tabOpen = true;

                ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
                if (doc->Dirty())
                    flags |= ImGuiTabItemFlags_UnsavedDocument;
                if (!m_FocusPath.empty() && doc->Path() == m_FocusPath)
                    flags |= ImGuiTabItemFlags_SetSelected;

                // UX-01 (KI-70): the document's own stable id, never its index, keys the tab
                // and its content — closing one document must not re-key another.
                const std::string label = std::string(doc->Icon()) + " " + doc->Title()
                                        + "###doc" + std::to_string(id);

                if (ImGui::BeginTabItem(label.c_str(), &tabOpen, flags))
                {
                    ImGui::PushID((int)id);
                    doc->OnImGuiRender(ctx);
                    ImGui::PopID();
                    ImGui::EndTabItem();
                }

                if (!tabOpen)
                    closeRequest = doc->Path();
            }
            m_FocusPath.clear();
            ImGui::EndTabBar();
        }

        ImGui::End();

        // Resolve a tab-✕ click: dirty docs raise a prompt, clean ones just close.
        if (!closeRequest.empty())
        {
            IAssetEditor* doc = Find(closeRequest);
            if (doc)
            {
                if (doc->Dirty())
                {
                    m_PromptClosePath = closeRequest;
                    ImGui::OpenPopup("Close Document##editorhost");
                }
                else
                {
                    Remove(closeRequest);
                }
            }
        }

        // Close-with-save prompt (deferred, outside the tab bar).
        if (ImGui::BeginPopupModal("Close Document##editorhost", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            IAssetEditor* doc = Find(m_PromptClosePath);

            if (!doc)
            {
                ImGui::CloseCurrentPopup();
                m_PromptClosePath.clear();
            }
            else
            {
                ImGui::Text("Save changes to \"%s\" before closing?", doc->Title().c_str());
                ImGui::Spacing();
                if (ImGui::Button("Save", ImVec2(110, 0)))
                {
                    const bool ok = doc->Save(ctx);
                    if (ok)
                    {
                        const std::string p = m_PromptClosePath;
                        m_PromptClosePath.clear();
                        Remove(p);
                        ImGui::CloseCurrentPopup();
                    }
                    else
                    {
                        ctx.Log("[Editors] Save failed — document kept open.", LogSeverity::Error);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Discard", ImVec2(110, 0)))
                {
                    const std::string p = m_PromptClosePath;
                    m_PromptClosePath.clear();
                    Remove(p);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(110, 0)))
                {
                    m_PromptClosePath.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
    }
}
