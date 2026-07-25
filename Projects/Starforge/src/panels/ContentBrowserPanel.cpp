// ContentBrowserPanel.cpp — see header.

#include "panels/ContentBrowserPanel.h"
#include "EditorPrefs.h"
#include "AssetTypes.h"
#include "ProjectAssets.h"

#include "utils/FileSystem.h"
#include "assets/AssetLibrary.h"
#include "graphics/Texture.h"
#include "graphics/Mesh.h"
#include "graphics/MaterialAsset.h"
#include "audio/AudioEngine.h"
#include "audio/Sound.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include "ui/IconsLucide.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
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
        // A4 — tiles the PreviewRig can thumbnail (meshes + materials).
        bool IsThumbable(const std::string& e)
        {
            if (e == ".cmat")
                return true;
            return !e.empty() && PreviewRig::IsMeshExtension(e.substr(1));
        }

        bool IsAudio(const std::string& e)
        {
            return e == ".wav" || e == ".mp3" || e == ".flac" || e == ".ogg";
        }
        bool IsModel(const std::string& e)
        {
            return e == ".obj" || e == ".fbx" || e == ".gltf" || e == ".glb" ||
                   e == ".stl" || e == ".dae" || e == ".ply";
        }
        bool IsHdr(const std::string& e) { return e == ".hdr" || e == ".exr"; }
        std::string FormatBytes(uint64_t b)
        {
            char buf[32];
            if (b < 1024ull)                       std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)b);
            else if (b < 1024ull * 1024)           std::snprintf(buf, sizeof(buf), "%.1f KiB", b / 1024.0);
            else if (b < 1024ull * 1024 * 1024)    std::snprintf(buf, sizeof(buf), "%.1f MiB", b / (1024.0 * 1024));
            else                                   std::snprintf(buf, sizeof(buf), "%.2f GiB", b / (1024.0 * 1024 * 1024));
            return buf;
        }
        // A symmetric scope of the peak-decimated envelope (T2 CopyPcm).
        void DrawWaveform(const std::vector<float>& env, ImVec2 size)
        {
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##wave", size);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(20, 20, 26, 255));
            const float midY = p0.y + size.y * 0.5f;
            dl->AddLine(ImVec2(p0.x, midY), ImVec2(p0.x + size.x, midY), IM_COL32(60, 60, 72, 255));
            if (!env.empty())
            {
                const float step = size.x / (float)env.size();
                for (size_t i = 0; i < env.size(); ++i)
                {
                    const float a  = std::min(1.0f, std::fabs(env[i]));
                    const float x  = p0.x + (float)i * step;
                    const float hh = a * size.y * 0.47f;
                    dl->AddLine(ImVec2(x, midY - hh), ImVec2(x, midY + hh), IM_COL32(120, 200, 255, 255));
                }
            }
        }

        // A saturated accent lightened toward white (hover) / darkened (active).
        ImU32 Blend(ImU32 c, float toward, float amt)
        {
            ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
            v.x += (toward - v.x) * amt;
            v.y += (toward - v.y) * amt;
            v.z += (toward - v.z) * amt;
            return ImGui::ColorConvertFloat4ToU32(v);
        }

        // A glyph-on-color tile (T5): the type accent as the button fill, the
        // glyph as a readable centered label. Returns true on click.
        bool GlyphTile(const AssetTypeInfo& info, float cell)
        {
            const ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(info.Color);
            const float  lum  = 0.299f * rgba.x + 0.587f * rgba.y + 0.114f * rgba.z;
            const ImU32  text = lum > 0.6f ? IM_COL32(25, 25, 28, 255) : IM_COL32(245, 245, 245, 255);

            ImGui::PushStyleColor(ImGuiCol_Button,        info.Color);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Blend(info.Color, 1.0f, 0.18f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Blend(info.Color, 0.0f, 0.18f));
            ImGui::PushStyleColor(ImGuiCol_Text,          text);
            const bool clicked = ImGui::Button(info.Glyph, ImVec2(cell, cell));
            ImGui::PopStyleColor(4);
            return clicked;
        }

        // A non-colliding "dir/<stem><ext>" ("New Material.cmat", then " (2)"…).
        fs::path UniquePath(const fs::path& dir, const std::string& stem, const std::string& ext)
        {
            std::error_code ec;
            fs::path p = dir / (stem + ext);
            for (int n = 2; fs::exists(p, ec); ++n)
                p = dir / (stem + " (" + std::to_string(n) + ")" + ext);
            return p;
        }

        // Case-insensitive substring test (T4 search).
        bool ContainsCI(const std::string& haystack, const std::string& needle)
        {
            if (needle.empty()) return true;
            auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
            return it != haystack.end();
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

    ContentBrowserPanel::~ContentBrowserPanel()
    {
        // Stop any looping preview voice before teardown (safe post-shutdown).
        if (m_PreviewVoice != Cosmic::InvalidSoundHandle)
            Cosmic::AudioEngine::Stop(m_PreviewVoice);
    }

    void ContentBrowserPanel::LoadPrefs(const Prefs::EditorSettings& s)
    {
        m_TreeWidth   = std::clamp(s.CbTreeWidth, 120.0f, 480.0f);
        m_TileSize    = std::clamp(s.CbTileSize,  48.0f, 160.0f);
        m_ShowPreview = s.CbShowPreview;
    }

    void ContentBrowserPanel::SavePrefs(Prefs::EditorSettings& s) const
    {
        s.CbTreeWidth   = m_TreeWidth;
        s.CbTileSize    = m_TileSize;
        s.CbShowPreview = m_ShowPreview;
    }

    void ContentBrowserPanel::Reset()
    {
        m_Watcher.Stop();
        m_WatchOn  = false;
        m_Resolved = false;
        m_Preview.clear();
        m_DeleteTarget.clear();
        m_History.clear();
        m_HistoryPos = -1;
        m_Search[0] = 0;
        m_RenameEditPath.clear();
        m_Pending = {};
        if (m_PreviewVoice != Cosmic::InvalidSoundHandle)
        {
            Cosmic::AudioEngine::Stop(m_PreviewVoice);
            m_PreviewVoice = Cosmic::InvalidSoundHandle;
        }
        m_SelectedDisk.clear();
        m_PreviewSound.reset();
        m_Waveform.clear();
        m_WaveformFor.clear();
    }

    void ContentBrowserPanel::EnsureResolved()
    {
        if (m_Resolved) return;
        m_Root = fs::path(Cosmic::FileSystem::Resolve("project://"));
        m_Current = m_Root;
        m_History = { m_Root };
        m_HistoryPos = 0;
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

    // ---- Navigation history (T4) ------------------------------------------
    void ContentBrowserPanel::NavigateTo(const fs::path& dir)
    {
        m_Search[0] = 0;                 // navigating always leaves search mode
        if (dir == m_Current) return;

        // Drop any forward history, then push the new location as the new head.
        if (m_HistoryPos >= 0 && m_HistoryPos + 1 < (int)m_History.size())
            m_History.erase(m_History.begin() + (m_HistoryPos + 1), m_History.end());
        m_History.push_back(dir);
        m_HistoryPos = (int)m_History.size() - 1;
        m_Current = dir;
    }

    void ContentBrowserPanel::GoBack()
    {
        if (!CanGoBack()) return;
        --m_HistoryPos;
        m_Current = m_History[m_HistoryPos];
        m_Search[0] = 0;
    }

    void ContentBrowserPanel::GoForward()
    {
        if (!CanGoForward()) return;
        ++m_HistoryPos;
        m_Current = m_History[m_HistoryPos];
        m_Search[0] = 0;
    }

    bool ContentBrowserPanel::HasSubdirectories(const fs::path& dir) const
    {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (e.is_directory(ec))
                return true;
        return false;
    }

    // ---- Rename / move + reference retarget (T6) --------------------------
    void ContentBrowserPanel::BeginRename(const fs::path& disk)
    {
        m_RenameEditPath = disk.string();
        // Edit the stem for files (keep the extension), the whole name for folders.
        std::error_code ec;
        const std::string seed = fs::is_directory(disk, ec)
            ? disk.filename().string() : disk.stem().string();
        std::snprintf(m_RenameBuf, sizeof(m_RenameBuf), "%s", seed.c_str());
        m_RenameFocus = true;
    }

    void ContentBrowserPanel::TryRelocate(EditorContext& ctx, const fs::path& oldDisk, const fs::path& newDisk)
    {
        if (oldDisk == newDisk) return;

        std::error_code ec;
        if (fs::exists(newDisk, ec))
        {
            ctx.Log("[Assets] '" + newDisk.filename().string() + "' already exists — not overwritten.",
                    LogSeverity::Error);
            return;
        }

        const bool isDir  = fs::is_directory(oldDisk, ec);
        const std::string oldVfs = VfsPathOf(oldDisk);
        const std::string newVfs = VfsPathOf(newDisk);

        // Files under the project root may be referenced by scenes/prefabs/mats.
        // Directories are relocated directly (a folder retarget = prefix sweep, a
        // documented follow-up); non-project files likewise just move.
        if (!isDir && !oldVfs.empty() && !newVfs.empty())
        {
            std::vector<std::string> refs = ProjectAssets::FindReferences(oldVfs);
            if (!refs.empty())
            {
                m_Pending = { true, oldDisk.string(), newDisk.string(), oldVfs, newVfs, std::move(refs) };
                return;   // defer the rename+retarget to the confirmation dialog
            }
        }

        fs::rename(oldDisk, newDisk, ec);
        if (ec) ctx.Log("[Assets] Rename/move failed: " + ec.message(), LogSeverity::Error);
        else    ctx.Log("[Assets] " + oldDisk.filename().string() + " → " + newDisk.filename().string() + ".");
    }

    void ContentBrowserPanel::AcceptMoveDrop(EditorContext& ctx, const fs::path& destDir)
    {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH"))
        {
            const std::string srcVfs((const char*)p->Data);
            const fs::path srcDisk = fs::path(Cosmic::FileSystem::Resolve(srcVfs));
            const fs::path newDisk = destDir / srcDisk.filename();
            if (srcDisk.parent_path() != destDir)   // ignore drops onto the same folder
                TryRelocate(ctx, srcDisk, newDisk);
        }
    }

    void ContentBrowserPanel::RevealAsset(const std::string& vfs)
    {
        if (vfs.empty()) return;
        EnsureResolved();
        std::error_code ec;
        const fs::path disk = fs::path(Cosmic::FileSystem::Resolve(vfs));
        if (!fs::exists(disk, ec)) return;
        const fs::path folder = fs::is_directory(disk, ec) ? disk : disk.parent_path();
        NavigateTo(folder);
        Select(disk);
    }

    // ---- Import (T8) ------------------------------------------------------
    void ContentBrowserPanel::ImportFile(EditorContext& ctx, const fs::path& srcDisk)
    {
        std::error_code ec;
        if (!fs::exists(srcDisk, ec) || fs::is_directory(srcDisk, ec))
        {
            ctx.Log("[Assets] Cannot import '" + srcDisk.string() + "'.", LogSeverity::Error);
            return;
        }

        const std::string ext = Ext(srcDisk);

        // Models → the E16 .cmeta import modal (copies into project://models/,
        // spawns multi-mesh parents, writes per-material .cmat). The shell opens it.
        // W7: no importer in the 2D build, so a dropped model is refused rather
        // than raising a request nothing consumes.
        if (IsModel(ext))
        {
#ifndef COSMIC_2D_ONLY
            ctx.PendingImportModel = srcDisk.string();
#else
            ctx.Log("[Content] '" + ext + "' model import is a 3D-engine feature.",
                    LogSeverity::Warn);
#endif
            return;
        }

        // Images / audio / HDR → copy into the CURRENT folder + reload the cache.
        if (IsImage(ext) || IsAudio(ext) || IsHdr(ext))
        {
            fs::path dest = m_Current / srcDisk.filename();
            if (fs::exists(dest, ec))
                dest = UniquePath(m_Current, srcDisk.stem().string(), srcDisk.extension().string());
            fs::copy_file(srcDisk, dest, ec);
            if (ec) { ctx.Log("[Assets] Copy failed: " + ec.message(), LogSeverity::Error); return; }
            const std::string vfs = VfsPathOf(dest);
            if (!vfs.empty()) Cosmic::AssetLibrary::Reload(vfs);
            ctx.Log("[Assets] Imported " + dest.filename().string() + ".");
            return;
        }

        ctx.Log("[Assets] Unsupported import type: " + ext, LogSeverity::Warn);
    }

    // ---- Preview + metadata pane (T7) -------------------------------------
    void ContentBrowserPanel::Select(const fs::path& disk)
    {
        const std::string s = disk.string();
        if (s == m_SelectedDisk) return;
        m_SelectedDisk = s;
        // A new selection stops + drops the previous audio preview.
        if (m_PreviewVoice != Cosmic::InvalidSoundHandle)
        {
            Cosmic::AudioEngine::Stop(m_PreviewVoice);
            m_PreviewVoice = Cosmic::InvalidSoundHandle;
        }
        m_PreviewSound.reset();
        m_Waveform.clear();
        m_WaveformFor.clear();
    }

    void ContentBrowserPanel::DrawPreviewPane(EditorContext& ctx, float height)
    {
        (void)ctx;
        ImGui::BeginChild("##cbPreview", ImVec2(0, height), true);

        std::error_code ec;
        const fs::path disk = fs::path(m_SelectedDisk);
        if (m_SelectedDisk.empty() || !fs::exists(disk, ec))
        {
            ImGui::TextDisabled("Select an asset to preview its contents and metadata.");
            ImGui::EndChild();
            return;
        }

        const std::string ext = Ext(disk);
        const std::string vfs = VfsPathOf(disk);
        const AssetTypeInfo& info = AssetTypeForExt(ext);
        const bool isDir = fs::is_directory(disk, ec);
        const float box  = std::max(72.0f, height - 28.0f);   // square preview area

        // --- Left: the visual preview / player ---------------------------
        ImGui::BeginGroup();
        if (isDir)
        {
            GlyphTile(FolderTypeInfo(), box);
        }
        else if (IsImage(ext) && !vfs.empty())
        {
            if (auto tex = Cosmic::AssetLibrary::GetTexture(vfs))
            {
                const float ar = (float)tex->GetHeight() / (float)std::max(1u, tex->GetWidth());
                ImGui::Image((ImTextureID)(intptr_t)tex->GetRendererID(),
                             ImVec2(box, std::min(box, box * ar)), ImVec2(0, 1), ImVec2(1, 0));
            }
            else GlyphTile(info, box);
        }
        // W7 — the inspect pane's turntable previews (a .cmat sphere, a mesh)
        // both render through PreviewRig's 3D pass, and the mesh leg additionally
        // needs AssetLibrary::GetMesh. Both fall through to the glyph tile in the
        // 2D build; the image and audio previews below are shared and untouched.
#ifndef COSMIC_2D_ONLY
        else if (ext == ".cmat" && !vfs.empty())
        {
            Cosmic::MaterialAsset asset;
            Cosmic::AssetLibrary::LoadMaterialAsset(asset, vfs);
            const uint32_t id = m_PreviewRig.RenderMaterial(asset, (uint32_t)box, (uint32_t)box);
            ImGui::ImageButton("##pvmat", (ImTextureID)(intptr_t)id, ImVec2(box, box), ImVec2(0, 1), ImVec2(1, 0));
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
            { const ImVec2 d = ImGui::GetIO().MouseDelta; m_PreviewRig.Orbit(d.x, d.y); }
            if (ImGui::IsItemHovered())
            {
                if (const float w = ImGui::GetIO().MouseWheel; w != 0.0f) m_PreviewRig.Zoom(w);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) m_PreviewRig.ResetView();
            }
        }
        else if (IsThumbable(ext) && !vfs.empty())   // a mesh
        {
            auto mesh = Cosmic::AssetLibrary::GetMesh(vfs);
            if (!mesh)
            {
                GlyphTile(info, box);
            }
            else
            {
                const uint32_t id = m_PreviewRig.RenderMesh(mesh, nullptr, glm::vec4(0.82f, 0.82f, 0.85f, 1.0f),
                                                            (uint32_t)box, (uint32_t)box);
                ImGui::ImageButton("##pvmesh", (ImTextureID)(intptr_t)id, ImVec2(box, box), ImVec2(0, 1), ImVec2(1, 0));
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
                { const ImVec2 d = ImGui::GetIO().MouseDelta; m_PreviewRig.Orbit(d.x, d.y); }
                if (ImGui::IsItemHovered())
                {
                    if (const float w = ImGui::GetIO().MouseWheel; w != 0.0f) m_PreviewRig.Zoom(w);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) m_PreviewRig.ResetView();
                }
            }
        }
#endif   // COSMIC_2D_ONLY — the .cmat / mesh turntable previews
        else if (IsAudio(ext))
        {
            // Decode the envelope once per selection (device-independent — T2).
            if (m_WaveformFor != vfs)
            {
                m_PreviewSound = Cosmic::Sound::Create(vfs);
                if (m_PreviewSound) m_PreviewSound->CopyPcm(m_Waveform, 256);
                m_WaveformFor = vfs;
            }
            DrawWaveform(m_Waveform, ImVec2(box * 1.6f, box - 28.0f));
            const bool playing = m_PreviewVoice != Cosmic::InvalidSoundHandle &&
                                 Cosmic::AudioEngine::IsPlaying(m_PreviewVoice);
            if (!playing)
            {
                if (ImGui::Button(ICON_LC_MUSIC " Play") && m_PreviewSound)
                    m_PreviewVoice = Cosmic::AudioEngine::PlayLooping(m_PreviewSound);
            }
            else if (ImGui::Button(ICON_LC_X " Stop"))
            {
                Cosmic::AudioEngine::Stop(m_PreviewVoice);
                m_PreviewVoice = Cosmic::InvalidSoundHandle;
            }
        }
        else
        {
            GlyphTile(info, box);
        }
        ImGui::EndGroup();

        // --- Right: metadata chips ---------------------------------------
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted(disk.filename().string().c_str());
        ImGui::TextDisabled("%s", isDir ? "Folder" : info.Name);

        if (!isDir)
        {
            const uint64_t bytes = (uint64_t)fs::file_size(disk, ec);
            if (!ec) ImGui::Text("Disk: %s", FormatBytes(bytes).c_str());

            if (IsImage(ext) && !vfs.empty())
            {
                if (auto tex = Cosmic::AssetLibrary::GetTexture(vfs))
                {
                    ImGui::Text("%u x %u px", tex->GetWidth(), tex->GetHeight());
                    ImGui::Text("GPU: %s", FormatBytes(tex->GetGpuBytes()).c_str());
                }
            }
#ifndef COSMIC_2D_ONLY
            else if (IsThumbable(ext) && ext != ".cmat" && !vfs.empty())
            {
                if (auto mesh = Cosmic::AssetLibrary::GetMesh(vfs))
                {
                    ImGui::Text("Verts: %u", mesh->GetVertexCount());
                    ImGui::Text("Indices: %u", mesh->GetIndexCount());
                    const glm::vec3 e0 = mesh->GetLocalMin(), e1 = mesh->GetLocalMax();
                    ImGui::Text("AABB: %.2f x %.2f x %.2f", e1.x - e0.x, e1.y - e0.y, e1.z - e0.z);
                    ImGui::Text("GPU: %s", FormatBytes(mesh->GetGpuBytes()).c_str());
                }
            }
#endif
            else if (IsAudio(ext) && m_PreviewSound)
            {
                ImGui::Text("Duration: %.2f s", m_PreviewSound->GetDuration());
            }
        }
        ImGui::EndGroup();

        ImGui::EndChild();
    }

    // ---- Left folder tree (T4) --------------------------------------------
    void ContentBrowserPanel::DrawFolderTree(const fs::path& dir, EditorContext& ctx)
    {
        std::error_code ec;
        std::vector<fs::path> subs;
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (e.is_directory(ec))
                subs.push_back(e.path());
        std::sort(subs.begin(), subs.end(), [](const fs::path& a, const fs::path& b)
        { return a.filename().string() < b.filename().string(); });

        for (const fs::path& sub : subs)
        {
            const bool hasKids = HasSubdirectories(sub);
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (sub == m_Current) flags |= ImGuiTreeNodeFlags_Selected;
            if (!hasKids)         flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

            ImGui::PushID(sub.string().c_str());
            const std::string label = std::string(ICON_LC_FOLDER " ") + sub.filename().string();
            const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                NavigateTo(sub);
            if (ImGui::BeginDragDropTarget())   // T6 — drop a tile here to move it in
            {
                AcceptMoveDrop(ctx, sub);
                ImGui::EndDragDropTarget();
            }
            if (open && hasKids)
            {
                DrawFolderTree(sub, ctx);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    // ---- One grid tile (T4) -----------------------------------------------
    void ContentBrowserPanel::DrawEntryTile(const fs::directory_entry& entry,
                                            EditorContext& ctx, float cell, bool isDir)
    {
        const fs::path path = entry.path();
        ImGui::PushID(path.string().c_str());
        ImGui::BeginGroup();

        bool activated = false;

        if (isDir)
        {
            GlyphTile(FolderTypeInfo(), cell);
            activated = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_F2)) BeginRename(path);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) Select(path);
            if (m_SelectedDisk == path.string())
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                    IM_COL32(255, 180, 80, 255), 3.0f, 0, 2.0f);

            if (ImGui::BeginDragDropTarget())   // T6 — drop a tile onto a folder to move it
            {
                AcceptMoveDrop(ctx, path);
                ImGui::EndDragDropTarget();
            }

            if (ImGui::BeginPopupContextItem("dir_ctx"))
            {
                if (ImGui::MenuItem("Open")) NavigateTo(path);
                if (ImGui::MenuItem("Rename")) BeginRename(path);
                if (ImGui::MenuItem("Show in Explorer")) ShowInExplorer(path.string());
                if (ImGui::MenuItem("Delete (Recycle Bin)")) m_DeleteTarget = path.string();
                ImGui::EndPopup();
            }

            if (activated) NavigateTo(path);
        }
        else
        {
            const std::string ext = Ext(path);
            const std::string vfs = VfsPathOf(path);
            const AssetTypeInfo& info = AssetTypeForExt(ext);

            // Real thumbnails win (images, PreviewRig mesh/material); everything
            // else — and any not-yet-rendered thumbnail — draws a glyph-on-color
            // tile from the type table.
            bool drewThumb = false;
            if (IsImage(ext) && !vfs.empty())
            {
                if (auto tex = Cosmic::AssetLibrary::GetTexture(vfs))
                {
                    ImGui::ImageButton("thumb", (ImTextureID)(intptr_t)tex->GetRendererID(),
                                       ImVec2(cell, cell), ImVec2(0, 1), ImVec2(1, 0));
                    drewThumb = true;
                }
            }
            else if (IsThumbable(ext) && !vfs.empty())
            {
                if (auto tex = ctx.Preview.Thumbnail(vfs))
                {
                    ImGui::ImageButton("thumb", (ImTextureID)(intptr_t)tex->GetRendererID(),
                                       ImVec2(cell, cell), ImVec2(0, 1), ImVec2(1, 0));
                    drewThumb = true;
                }
            }
            if (!drewThumb)
                GlyphTile(info, cell);

            activated = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_F2)) BeginRename(path);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) Select(path);
            if (m_SelectedDisk == path.string())
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                    IM_COL32(255, 180, 80, 255), 3.0f, 0, 2.0f);

            // Drag source → Inspector AssetPath slots.
            if (!vfs.empty() && ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("ASSET_PATH", vfs.c_str(), vfs.size() + 1);
                ImGui::TextUnformatted(path.filename().string().c_str());
                ImGui::EndDragDropSource();
            }

            // Double-click routing via the table's open action (unchanged for the
            // three that already acted: Scene / Prefab / Texture).
            if (activated && !vfs.empty())
            {
                switch (info.Open)
                {
                    case AssetOpen::Scene:   ctx.PendingOpenScene = vfs; break;
                    case AssetOpen::Prefab:  ctx.PendingInstantiatePrefab = vfs; break;   // E14
                    case AssetOpen::Texture: m_Preview = vfs; break;
                    default:
                        // M1 — a rigged model double-click opens the Animation Editor.
                        if (info.Editor == AssetOpen::AnimationEditor)
                            ctx.PendingOpenAnimEditor = vfs;
                        // Q1/Q4 — graph documents (.cflow / .cstory) open in the host.
                        else if (info.Editor == AssetOpen::FlowEditor ||
                                 info.Editor == AssetOpen::StoryEditor)
                            ctx.PendingOpenDocument = vfs;
                        break;
                }
            }

            if (ImGui::BeginPopupContextItem("file_ctx"))
            {
                if (info.Open == AssetOpen::Scene && ImGui::MenuItem("Open Scene")) ctx.PendingOpenScene = vfs;
                if (info.Open == AssetOpen::Prefab && ImGui::MenuItem("Instantiate Prefab")) ctx.PendingInstantiatePrefab = vfs;
                if (info.Editor == AssetOpen::AnimationEditor && ImGui::MenuItem("Open in Animation Editor"))
                    ctx.PendingOpenAnimEditor = vfs;   // M1 — route to the AssetEditorHost
                if (info.Editor == AssetOpen::FlowEditor && ImGui::MenuItem("Open in Flow Editor"))
                    ctx.PendingOpenDocument = vfs;     // Q1
                if (info.Editor == AssetOpen::StoryEditor && ImGui::MenuItem("Open in Story Graph Editor"))
                    ctx.PendingOpenDocument = vfs;     // Q4
                if (ImGui::MenuItem("Rename")) BeginRename(path);
                if (ImGui::MenuItem("Show in Explorer")) ShowInExplorer(path.string());
                if (ImGui::MenuItem("Delete (Recycle Bin)")) m_DeleteTarget = path.string();
                ImGui::EndPopup();
            }
        }

        // Label — or an inline rename box for the entry being renamed (T6).
        if (m_RenameEditPath == path.string())
        {
            ImGui::SetNextItemWidth(cell);
            if (m_RenameFocus) { ImGui::SetKeyboardFocusHere(); m_RenameFocus = false; }
            const bool enter = ImGui::InputText("##rename", m_RenameBuf, sizeof(m_RenameBuf),
                                                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (enter || ImGui::IsItemDeactivated())
            {
                const std::string newName = m_RenameBuf;
                const std::string ext = isDir ? std::string() : path.extension().string();
                m_RenameEditPath.clear();
                if (!newName.empty())
                    TryRelocate(ctx, path, path.parent_path() / (newName + ext));
            }
        }
        else
        {
            ImGui::TextWrapped("%s", path.filename().string().c_str());
        }

        ImGui::EndGroup();
        ImGui::PopID();
    }

    // ---- Right grid (T4) --------------------------------------------------
    void ContentBrowserPanel::DrawGrid(EditorContext& ctx)
    {
        std::error_code ec;
        const float cell = m_TileSize, pad = 12.0f;
        const int columns = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (cell + pad)));
        int col = 0;
        auto nextCell = [&] { if (++col % columns != 0) ImGui::SameLine(); };

        const bool searching = m_Search[0] != 0;

        if (searching)
        {
            // Recursive flat filtered view (case-insensitive filename contains).
            const std::string needle = m_Search;
            std::vector<fs::directory_entry> hits;
            for (auto it = fs::recursive_directory_iterator(m_Root, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                if (ec) break;
                if (ContainsCI(it->path().filename().string(), needle))
                    hits.push_back(*it);
            }
            std::sort(hits.begin(), hits.end(), [](const fs::directory_entry& a, const fs::directory_entry& b)
            { return a.path().filename().string() < b.path().filename().string(); });

            if (hits.empty()) { ImGui::TextDisabled("No matches for \"%s\".", m_Search); return; }
            for (const auto& e : hits)
            {
                DrawEntryTile(e, ctx, cell, e.is_directory(ec));
                nextCell();
            }
            return;
        }

        // Current-folder view: directories first, then files, each name-sorted.
        std::vector<fs::directory_entry> dirs, files;
        for (const auto& entry : fs::directory_iterator(m_Current, ec))
            (entry.is_directory(ec) ? dirs : files).push_back(entry);
        auto byName = [](const fs::directory_entry& a, const fs::directory_entry& b)
        { return a.path().filename().string() < b.path().filename().string(); };
        std::sort(dirs.begin(), dirs.end(), byName);
        std::sort(files.begin(), files.end(), byName);

        for (const auto& d : dirs)  { DrawEntryTile(d, ctx, cell, true);  nextCell(); }
        for (const auto& f : files) { DrawEntryTile(f, ctx, cell, false); nextCell(); }

        if (dirs.empty() && files.empty())
            ImGui::TextDisabled("(empty folder)");

        // Empty-space context menu (create ops, driven by the T5 type table).
        if (ImGui::BeginPopupContextWindow("cb_ctx", ImGuiPopupFlags_MouseButtonRight |
                                           ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::BeginMenu(ICON_LC_PLUS " New"))
            {
                if (ImGui::MenuItem(ICON_LC_FOLDER " Folder"))
                    fs::create_directories(UniquePath(m_Current, "New Folder", ""), ec);
                ImGui::Separator();
                for (const CreatableType& ct : CreatableTypes())
                {
                    const std::string label = std::string(ct.Glyph) + " " + ct.Label;
                    if (ImGui::MenuItem(label.c_str()))
                    {
                        const fs::path target =
                            UniquePath(m_Current, std::string("New ") + ct.Label, ct.Ext);
                        if (CreateDefaultAsset(ct.Ext, target.string()))
                            ctx.Log(std::string("[Assets] Created ") + target.filename().string() + ".");
                        else
                            ctx.Log(std::string("[Assets] Failed to create ") + ct.Label + ".",
                                    LogSeverity::Error);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Show in Explorer")) ShowInExplorer(m_Current.string());
            ImGui::EndPopup();
        }
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

        // Inspector asset-slot reveal request (T11).
        if (!ctx.PendingRevealAsset.empty())
        {
            RevealAsset(ctx.PendingRevealAsset);
            ctx.PendingRevealAsset.clear();
        }

        // OS file drops routed by the shell (T8) → import into the current folder.
        if (!ctx.PendingDroppedFiles.empty())
        {
            for (const std::string& p : ctx.PendingDroppedFiles)
                ImportFile(ctx, fs::path(p));
            ctx.PendingDroppedFiles.clear();
        }

        // Mouse back/forward (buttons 4/5) navigate history when the panel is hovered.
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup))
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton(3))) GoBack();
            if (ImGui::IsMouseClicked(ImGuiMouseButton(4))) GoForward();
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

        // --- Toolbar: back/forward · breadcrumb · search · tile size -------
        ImGui::BeginDisabled(!CanGoBack());
        if (ImGui::Button(ICON_LC_ARROW_LEFT "##back")) GoBack();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back (Mouse 4)");
        ImGui::SameLine();
        ImGui::BeginDisabled(!CanGoForward());
        if (ImGui::Button(ICON_LC_ARROW_RIGHT "##fwd")) GoForward();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forward (Mouse 5)");

        // Import (T8): one dialog for models (→ E16 .cmeta modal) + copy-in assets.
        ImGui::SameLine();
        if (ImGui::Button(ICON_LC_IMPORT " Import"))
        {
            Cosmic::FileDialogDesc dlg;
            dlg.Title      = "Import Asset";
            dlg.Filters    = { { "Assets", "*.obj;*.fbx;*.gltf;*.glb;*.stl;*.dae;*.ply;*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr;*.exr;*.wav;*.mp3;*.flac;*.ogg" },
                               { "All files", "*.*" } };
            dlg.InitialDir = "project://";
            if (auto picked = Cosmic::FileDialog::Open(dlg))
                ImportFile(ctx, fs::path(*picked));
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Import a model / image / audio / HDR into this folder");

        // Breadcrumb.
        ImGui::SameLine();
        if (ImGui::Button(ICON_LC_HOUSE " Assets")) NavigateTo(m_Root);
        {
            fs::path rel = fs::relative(m_Current, m_Root, ec);
            if (!ec && rel != ".")
            {
                fs::path walk = m_Root;
                for (const auto& part : rel)
                {
                    walk /= part;
                    ImGui::SameLine(0, 2); ImGui::TextDisabled("/"); ImGui::SameLine(0, 2);
                    if (ImGui::SmallButton(part.string().c_str())) { NavigateTo(walk); break; }
                }
            }
        }

        // Search + tile-size slider + preview toggle, right-aligned.
        {
            const float eyeW = 30.0f, sliderW = 110.0f, searchW = 180.0f;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            float rightX = ImGui::GetContentRegionMax().x - eyeW - sliderW - searchW - 2.0f * spacing;
            if (rightX > ImGui::GetCursorPosX()) { ImGui::SameLine(); ImGui::SetCursorPosX(rightX); }
            else ImGui::SameLine();
            ImGui::SetNextItemWidth(searchW);
            ImGui::InputTextWithHint("##cbSearch", ICON_LC_SEARCH " Search all…", m_Search, sizeof(m_Search));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(sliderW);
            ImGui::SliderFloat("##cbTile", &m_TileSize, 48.0f, 160.0f, "%.0f px");
            ImGui::SameLine();
            if (ImGui::Button(m_ShowPreview ? ICON_LC_EYE : ICON_LC_EYE_OFF)) m_ShowPreview = !m_ShowPreview;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle preview pane");
        }
        ImGui::Separator();

        // --- Two-pane: folder tree | tile grid (+ optional preview pane) ----
        const float previewH = m_ShowPreview ? 200.0f : 0.0f;
        const float paneH = std::max(60.0f, ImGui::GetContentRegionAvail().y - previewH);

        ImGui::BeginChild("##cbTree", ImVec2(m_TreeWidth, paneH), true);
        {
            ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_SpanAvailWidth;
            if (m_Current == m_Root) rootFlags |= ImGuiTreeNodeFlags_Selected;
            if (ImGui::Selectable(ICON_LC_FOLDER_OPEN " Assets", (rootFlags & ImGuiTreeNodeFlags_Selected) != 0))
                NavigateTo(m_Root);
            if (ImGui::BeginDragDropTarget())   // T6 — drop a tile onto the root to move it up
            {
                AcceptMoveDrop(ctx, m_Root);
                ImGui::EndDragDropTarget();
            }
            DrawFolderTree(m_Root, ctx);
        }
        ImGui::EndChild();

        // Splitter handle (persists m_TreeWidth).
        ImGui::SameLine();
        ImGui::InvisibleButton("##cbSplit", ImVec2(6.0f, paneH));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive())
            m_TreeWidth = std::clamp(m_TreeWidth + ImGui::GetIO().MouseDelta.x, 120.0f, 480.0f);
        ImGui::SameLine();

        ImGui::BeginChild("##cbGrid", ImVec2(0, paneH), true);
        DrawGrid(ctx);
        ImGui::EndChild();

        // --- Preview + metadata pane (T7) ----------------------------------
        if (m_ShowPreview)
            DrawPreviewPane(ctx, previewH - ImGui::GetStyle().ItemSpacing.y);

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

        // --- Rename/move reference-retarget confirmation (T6) --------------
        if (m_Pending.Active)
            ImGui::OpenPopup("Update References?");
        if (ImGui::BeginPopupModal("Update References?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Rename %s  →  %s",
                        fs::path(m_Pending.OldDisk).filename().string().c_str(),
                        fs::path(m_Pending.NewDisk).filename().string().c_str());
            ImGui::TextDisabled("%zu file(s) reference this asset and will be updated:",
                                m_Pending.Refs.size());
            ImGui::BeginChild("##refs", ImVec2(440, 130), true);
            for (const std::string& r : m_Pending.Refs)
                ImGui::BulletText("%s", r.c_str());
            ImGui::EndChild();
            ImGui::TextDisabled("(rename is not undoable in the editor)");
            ImGui::Separator();
            if (ImGui::Button("Rename & Update", ImVec2(160, 0)))
            {
                std::error_code rec;
                fs::rename(m_Pending.OldDisk, m_Pending.NewDisk, rec);
                if (rec)
                {
                    ctx.Log("[Assets] Rename failed: " + rec.message(), LogSeverity::Error);
                }
                else
                {
                    auto changed = ProjectAssets::RetargetPath(m_Pending.OldVfs, m_Pending.NewVfs);
                    Cosmic::AssetLibrary::Reload(m_Pending.OldVfs);   // evict the stale cache slot
                    ctx.Log("[Assets] Renamed + retargeted " + std::to_string(changed.size()) +
                            " file(s) (reload scenes to see updates).");
                }
                m_Pending = {};
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                m_Pending = {};   // nothing was renamed — cancel touches nothing
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::End();
    }
}
