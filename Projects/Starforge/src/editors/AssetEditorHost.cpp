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
            if (d->Path() == vfsPath)
            {
                m_FocusPath = vfsPath;
                if (showFlag) *showFlag = true;
                return d.get();
            }
        }

        std::unique_ptr<IAssetEditor> ed = make ? make() : nullptr;
        if (!ed)
            return nullptr;

        IAssetEditor* raw = ed.get();
        m_Docs.push_back(std::move(ed));
        m_FocusPath = vfsPath;
        if (showFlag) *showFlag = true;
        return raw;
    }

    void AssetEditorHost::OnUpdate(EditorContext& ctx, float ts)
    {
        for (auto& d : m_Docs)
            d->OnUpdate(ctx, ts);
    }

    void AssetEditorHost::Remove(const std::string& path)
    {
        for (auto it = m_Docs.begin(); it != m_Docs.end(); ++it)
        {
            if ((*it)->Path() == path)
            {
                m_Docs.erase(it);
                return;
            }
        }
    }

    void AssetEditorHost::OnImGuiRender(EditorContext& ctx, bool* open)
    {
        if (!ImGui::Begin("Editors", open, ImGuiWindowFlags_NoScrollbar))
        {
            ImGui::End();
            return;
        }

        if (m_Docs.empty())
        {
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            ImGui::TextDisabled("No asset editor open.");
            ImGui::TextWrapped("Double-click a rigged model (or right-click ▸ Open in Animation "
                               "Editor) in the Content Browser to open it as a document here.");
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
                IAssetEditor* doc = m_Docs[i].get();
                bool tabOpen = true;

                ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
                if (doc->Dirty())
                    flags |= ImGuiTabItemFlags_UnsavedDocument;
                if (!m_FocusPath.empty() && doc->Path() == m_FocusPath)
                    flags |= ImGuiTabItemFlags_SetSelected;

                // A per-document ImGui id so identically-titled docs never collide.
                const std::string label = std::string(doc->Icon()) + " " + doc->Title()
                                        + "###doc" + std::to_string(i);

                if (ImGui::BeginTabItem(label.c_str(), &tabOpen, flags))
                {
                    ImGui::PushID((int)i);
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
            IAssetEditor* doc = nullptr;
            for (auto& d : m_Docs)
                if (d->Path() == closeRequest) { doc = d.get(); break; }
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
            IAssetEditor* doc = nullptr;
            for (auto& d : m_Docs)
                if (d->Path() == m_PromptClosePath) { doc = d.get(); break; }

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
