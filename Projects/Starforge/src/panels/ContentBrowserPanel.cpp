// ContentBrowserPanel.cpp — see header.

#include "panels/ContentBrowserPanel.h"

#include "utils/FileSystem.h"
#include "assets/AssetLibrary.h"
#include "graphics/Texture.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#endif

namespace fs = std::filesystem;

namespace Starforge
{
    namespace
    {
        std::string Ext(const fs::path& p)
        {
            std::string e = p.extension().string();
            std::transform(e.begin(), e.end(), e.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            return e;
        }
        bool IsImage(const std::string& e)
        {
            return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga" || e == ".bmp";
        }
        bool IsScene(const std::string& e)  { return e == ".cscene"; }
        bool IsPrefab(const std::string& e) { return e == ".cprefab"; }

        // A4 — tiles the PreviewRig can thumbnail (meshes + materials).
        bool IsThumbable(const std::string& e)
        {
            if (e == ".cmat")
                return true;
            return !e.empty() && PreviewRig::IsMeshExtension(e.substr(1));
        }

        const char* Badge(const std::string& e)
        {
            if (IsScene(e))                      return "SCN";
            if (e == ".cmat")                    return "MAT";
            if (e == ".cprefab")                 return "PFB";
            if (e == ".obj" || e == ".gltf" ||
                e == ".glb" || e == ".fbx" ||
                e == ".stl")                     return "MSH";
            if (IsImage(e))                      return "IMG";
            return "FILE";
        }

#ifdef _WIN32
        std::wstring Widen(const std::string& s)
        {
            const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
            std::wstring w((size_t)n, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
            return w;
        }
        void Recycle(const std::string& diskPath)
        {
            std::wstring w = Widen(fs::path(diskPath).make_preferred().string());
            w.push_back(L'\0');   // double-null terminated list
            SHFILEOPSTRUCTW op = {};
            op.wFunc  = FO_DELETE;
            op.pFrom  = w.c_str();
            op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
            SHFileOperationW(&op);
        }
        void ShowInExplorer(const std::string& diskPath)
        {
            std::wstring arg = L"/select,\"" + Widen(fs::absolute(diskPath).string()) + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
        }
#else
        void Recycle(const std::string& diskPath) { std::error_code ec; fs::remove_all(diskPath, ec); }
        void ShowInExplorer(const std::string&) {}
#endif
    }

    ContentBrowserPanel::~ContentBrowserPanel() = default;

    void ContentBrowserPanel::Reset()
    {
        m_Watcher.Stop();
        m_WatchOn  = false;
        m_Resolved = false;
        m_Preview.clear();
        m_DeleteTarget.clear();
    }

    void ContentBrowserPanel::EnsureResolved()
    {
        if (m_Resolved) return;
        m_Root = fs::path(Cosmic::FileSystem::Resolve("project://"));
        m_Current = m_Root;
        m_Resolved = true;

        std::error_code ec;
        if (fs::exists(m_Root, ec) && !m_WatchOn)
            m_WatchOn = m_Watcher.Watch(m_Root.generic_string(), /*recursive=*/true);
    }

    std::string ContentBrowserPanel::VfsPathOf(const fs::path& p) const
    {
        std::error_code ec;
        fs::path rel = fs::relative(p, m_Root, ec);
        if (ec || rel.empty()) return {};
        return "project://" + rel.generic_string();
    }

    void ContentBrowserPanel::OnImGuiRender(EditorContext& ctx, bool* pOpen)
    {
        ImGui::Begin("Content Browser", pOpen);
        EnsureResolved();

        std::error_code ec;
        if (m_Root.empty() || !fs::exists(m_Root, ec))
        {
            ImGui::TextDisabled("No project assets yet.");
            ImGui::TextWrapped("The asset root appears once the open project has an assets/ "
                               "folder. Right-click here to create scenes/folders.");
            ImGui::End();
            return;
        }

        // --- Hot reload: react to file changes under the root --------------
        for (const Cosmic::FileChange& c : m_Watcher.Poll())
        {
            const std::string vfs = "project://" + c.Path;
            if (c.Kind == Cosmic::FileChangeKind::Modified || c.Kind == Cosmic::FileChangeKind::Added)
            {
                if (IsImage(Ext(c.Path)))
                    Cosmic::AssetLibrary::Reload(vfs);
                if (IsThumbable(Ext(c.Path)))
                    ctx.Preview.Invalidate(vfs);   // A4 — stale thumbnail
            }
        }

        // --- Breadcrumb ----------------------------------------------------
        if (ImGui::Button("Assets")) m_Current = m_Root;
        {
            fs::path rel = fs::relative(m_Current, m_Root, ec);
            if (!ec && rel != ".")
            {
                fs::path walk = m_Root;
                for (const auto& part : rel)
                {
                    walk /= part;
                    ImGui::SameLine(0, 2); ImGui::TextDisabled("/"); ImGui::SameLine(0, 2);
                    if (ImGui::SmallButton(part.string().c_str())) { m_Current = walk; break; }
                }
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("   (drag a file onto an Inspector asset slot)");
        ImGui::Separator();

        // --- Gather + sort -------------------------------------------------
        std::vector<fs::directory_entry> dirs, files;
        for (const auto& entry : fs::directory_iterator(m_Current, ec))
            (entry.is_directory(ec) ? dirs : files).push_back(entry);
        auto byName = [](const fs::directory_entry& a, const fs::directory_entry& b)
        { return a.path().filename().string() < b.path().filename().string(); };
        std::sort(dirs.begin(), dirs.end(), byName);
        std::sort(files.begin(), files.end(), byName);

        // --- Grid ----------------------------------------------------------
        const float cell = 84.0f, pad = 12.0f;
        const int columns = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (cell + pad)));
        int col = 0;
        auto nextCell = [&] { if (++col % columns != 0) ImGui::SameLine(); };

        ImGui::BeginChild("grid");

        for (const auto& d : dirs)
        {
            ImGui::PushID(d.path().string().c_str());
            ImGui::BeginGroup();
            if (ImGui::Button("[ ]", ImVec2(cell, cell)) ) {}
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                m_Current = d.path();
            ImGui::TextWrapped("%s", d.path().filename().string().c_str());
            ImGui::EndGroup();
            ImGui::PopID();
            nextCell();
        }

        for (const auto& f : files)
        {
            const std::string ext = Ext(f.path());
            const std::string vfs = VfsPathOf(f.path());
            ImGui::PushID(f.path().string().c_str());
            ImGui::BeginGroup();

            bool activated = false;
            if (IsImage(ext) && !vfs.empty())
            {
                if (auto tex = Cosmic::AssetLibrary::GetTexture(vfs))
                {
                    ImGui::ImageButton("thumb", (ImTextureID)(intptr_t)tex->GetRendererID(),
                                       ImVec2(cell, cell), ImVec2(0, 1), ImVec2(1, 0));
                    activated = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                }
                else ImGui::Button("IMG", ImVec2(cell, cell));
            }
            else if (IsThumbable(ext) && !vfs.empty())
            {
                // A4 — real mesh/material thumbnails from the shared PreviewRig
                // (rendered a few per frame by the shell's pump; badge until then).
                if (auto tex = ctx.Preview.Thumbnail(vfs))
                    ImGui::ImageButton("thumb", (ImTextureID)(intptr_t)tex->GetRendererID(),
                                       ImVec2(cell, cell), ImVec2(0, 1), ImVec2(1, 0));
                else
                    ImGui::Button(Badge(ext), ImVec2(cell, cell));
                activated = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            }
            else
            {
                if (ImGui::Button(Badge(ext), ImVec2(cell, cell))) {}
                activated = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            }

            // Drag source → Inspector AssetPath slots.
            if (!vfs.empty() && ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("ASSET_PATH", vfs.c_str(), vfs.size() + 1);
                ImGui::TextUnformatted(f.path().filename().string().c_str());
                ImGui::EndDragDropSource();
            }

            // Double-click actions.
            if (activated)
            {
                if (IsScene(ext))        ctx.PendingOpenScene = vfs;
                else if (IsPrefab(ext))  ctx.PendingInstantiatePrefab = vfs;   // E14
                else if (IsImage(ext))   m_Preview = vfs;
            }

            // Per-file context menu.
            if (ImGui::BeginPopupContextItem("file_ctx"))
            {
                if (IsScene(ext) && ImGui::MenuItem("Open Scene")) ctx.PendingOpenScene = vfs;
                if (IsPrefab(ext) && ImGui::MenuItem("Instantiate Prefab")) ctx.PendingInstantiatePrefab = vfs;
                if (ImGui::MenuItem("Show in Explorer")) ShowInExplorer(f.path().string());
                if (ImGui::MenuItem("Delete (Recycle Bin)")) m_DeleteTarget = f.path().string();
                ImGui::EndPopup();
            }

            ImGui::TextWrapped("%s", f.path().filename().string().c_str());
            ImGui::EndGroup();
            ImGui::PopID();
            nextCell();
        }

        if (dirs.empty() && files.empty())
            ImGui::TextDisabled("(empty folder)");

        // Empty-space context menu (create ops).
        if (ImGui::BeginPopupContextWindow("cb_ctx", ImGuiPopupFlags_MouseButtonRight |
                                           ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("New Folder"))
            {
                fs::create_directories(m_Current / "New Folder", ec);
            }
            if (ImGui::MenuItem("New Scene"))
            {
                Cosmic::Ref<Cosmic::Scene> empty = Cosmic::Scene::Create();
                Cosmic::SceneSerializer::Save(*empty, (m_Current / "New Scene.cscene").string());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Show in Explorer")) ShowInExplorer(m_Current.string());
            ImGui::EndPopup();
        }

        ImGui::EndChild();

        // --- Delete confirmation ------------------------------------------
        if (!m_DeleteTarget.empty())
            ImGui::OpenPopup("Delete?");
        if (ImGui::BeginPopupModal("Delete?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Move to Recycle Bin?\n%s",
                        fs::path(m_DeleteTarget).filename().string().c_str());
            ImGui::TextDisabled("(this is not undoable in the editor)");
            ImGui::Separator();
            if (ImGui::Button("Delete", ImVec2(120, 0)))
            {
                Recycle(m_DeleteTarget);
                m_DeleteTarget.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                m_DeleteTarget.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // --- Texture preview ----------------------------------------------
        if (!m_Preview.empty())
            ImGui::OpenPopup("Texture Preview");
        if (ImGui::BeginPopupModal("Texture Preview", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (auto tex = Cosmic::AssetLibrary::GetTexture(m_Preview))
            {
                ImGui::Text("%s  (%ux%u)", m_Preview.c_str(), tex->GetWidth(), tex->GetHeight());
                const float w = 320.0f;
                const float h = w * (float)tex->GetHeight() / (float)std::max(1u, tex->GetWidth());
                ImGui::Image((ImTextureID)(intptr_t)tex->GetRendererID(), ImVec2(w, h),
                             ImVec2(0, 1), ImVec2(1, 0));
            }
            if (ImGui::Button("Close")) { m_Preview.clear(); ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }

        ImGui::End();
    }
}
