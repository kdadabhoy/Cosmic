// ContentBrowserPanel.cpp — see header.

#include "panels/ContentBrowserPanel.h"

#include "utils/FileSystem.h"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace Starforge
{
    void ContentBrowserPanel::OnImGuiRender(EditorContext& ctx)
    {
        (void)ctx;
        ImGui::Begin("Content Browser");

        // Resolve the asset root once. project:// must be resolved in THIS DLL
        // (app-side VFS rule, plan §0.5).
        if (!m_Resolved)
        {
            m_Root = fs::path(Cosmic::FileSystem::Resolve("project://"));
            m_Current = m_Root;
            m_Resolved = true;
        }

        if (m_Root.empty() || !fs::exists(m_Root))
        {
            ImGui::TextDisabled("No project assets yet.");
            ImGui::TextWrapped("The asset root appears when the open project has an "
                               "assets/ folder (project scaffolding lands with E6; "
                               "import with E16).");
            ImGui::End();
            return;
        }

        // --- Breadcrumb ---------------------------------------------------------
        if (ImGui::Button("Assets"))
            m_Current = m_Root;
        fs::path rel = fs::relative(m_Current, m_Root);
        if (rel != ".")
        {
            fs::path walk = m_Root;
            for (const auto& part : rel)
            {
                walk /= part;
                ImGui::SameLine(0.0f, 2.0f);
                ImGui::TextDisabled("/");
                ImGui::SameLine(0.0f, 2.0f);
                if (ImGui::SmallButton(part.string().c_str()))
                {
                    m_Current = walk;
                    break;
                }
            }
        }
        ImGui::Separator();

        // --- Listing (folders first, then files; alphabetical) -------------------
        std::vector<fs::directory_entry> dirs, files;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(m_Current, ec))
            (entry.is_directory(ec) ? dirs : files).push_back(entry);
        auto byName = [](const fs::directory_entry& a, const fs::directory_entry& b)
                      { return a.path().filename() < b.path().filename(); };
        std::sort(dirs.begin(), dirs.end(), byName);
        std::sort(files.begin(), files.end(), byName);

        for (const auto& d : dirs)
        {
            const std::string label = "[dir]  " + d.path().filename().string();
            if (ImGui::Selectable(label.c_str(), false,
                                  ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                m_Current = d.path();
        }
        for (const auto& f : files)
        {
            // TODO(E10): icons, thumbnails, drag source, double-click actions.
            ImGui::Selectable(f.path().filename().string().c_str());
        }

        if (dirs.empty() && files.empty())
            ImGui::TextDisabled("(empty)");

        ImGui::End();
    }
}
