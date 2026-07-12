#pragma once

// widgets/PropertyRows.h
//
// ============================================================================
// Starforge — reflected-field widgets (E8).
// ============================================================================
//
// One function maps a Cosmic::Reflect::FieldKind onto an ImGui control, mutates
// the component live while the user drags, and reports enough edit state for the
// Inspector to record a single undo step per edit (capture on activate, commit
// on deactivate-after-edit). No command logic here — the Inspector owns that.
//
// Header-only: it's a thin ImGui/reflection adapter used by one panel.
// ============================================================================

#include <Cosmic.h>
#include "ui/IconsLucide.h"
#include "AssetTypes.h"
#include "PreviewRig.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace Starforge::PropertyRows
{
    using Cosmic::Reflect::FieldDescriptor;
    using Cosmic::Reflect::FieldKind;
    using Cosmic::Reflect::FieldValue;

    // Extra services the rich AssetPath slot (T11) needs, kept OUT of the reflection
    // adapter's core so the plain DrawField/DrawValue path stays context-free. The
    // Inspector/panels build one from EditorContext and pass it in; when absent, an
    // AssetPath field falls back to the legacy text-input + picker.
    struct SlotContext
    {
        PreviewRig*  Preview = nullptr;        // mesh/material thumbnails
        std::string* PendingReveal = nullptr;  // set to a vfs to reveal it in the browser
    };

    struct Result
    {
        bool       Activated = false;   // became active this frame (drag start)
        bool       Committed = false;   // deactivated-after-edit (record undo now)
        bool       Changed   = false;   // edited this frame (already applied live)
        FieldValue PreValue;            // field value at the START of this frame
        FieldValue PostValue;           // field value now
    };

    namespace detail
    {
        inline float StepOr(const FieldDescriptor& f, float fallback)
        {
            return f.Hints.Step > 0.0f ? f.Hints.Step : fallback;
        }

        // T10 — unit suffix appended to a numeric field's display format.
        inline const char* UnitSuffix(Cosmic::Reflect::FieldUnits u)
        {
            using U = Cosmic::Reflect::FieldUnits;
            switch (u)
            {
                case U::Degrees: return "\xC2\xB0";   // ° (UTF-8)
                case U::Meters:  return " m";
                case U::Seconds: return " s";
                default:         return "";
            }
        }

        // Fold ImGui's per-frame item state into the row Result. (The field's Doc
        // is surfaced by the ⓘ marker drawn after the widget — T10 — not here.)
        inline void FinishItem(Result& r, const FieldValue& current, const FieldDescriptor& f)
        {
            r.Activated = ImGui::IsItemActivated();
            r.Committed = ImGui::IsItemDeactivatedAfterEdit();
            r.PostValue = current;
            (void)f;
        }

        // One place mapping an AssetPath's asset-type tag -> a native-dialog filter (H6).
        inline std::vector<Cosmic::FileFilter> FiltersForAssetType(const std::string& type)
        {
            if (type == "mesh")      return { { "3D models", "*.obj;*.fbx;*.stl;*.dae;*.ply;*.gltf;*.glb" }, { "All files", "*.*" } };
            if (type == "material")  return { { "Cosmic material", "*.cmat" }, { "All files", "*.*" } };
            if (type == "texture")   return { { "Images", "*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr" }, { "All files", "*.*" } };
            if (type == "heightmap") return { { "Heightmaps", "*.png;*.r16;*.raw;*.hdr" }, { "All files", "*.*" } };
            if (type == "hdri")      return { { "HDR images", "*.hdr;*.exr" }, { "All files", "*.*" } };
            if (type == "prefab")    return { { "Cosmic prefab", "*.cprefab" }, { "All files", "*.*" } };
            return { { "All files", "*.*" } };
        }

        // The extension set (lower-case, WITH dot) an AssetPath type accepts, for
        // the T11 picker's recursive scan. Empty => match any file.
        inline std::vector<std::string> ExtensionsForAssetType(const std::string& type)
        {
            if (type == "mesh")           return { ".obj", ".fbx", ".stl", ".dae", ".ply", ".gltf", ".glb" };
            if (type == "material")       return { ".cmat" };
            if (type == "texture")        return { ".png", ".jpg", ".jpeg", ".tga", ".bmp" };
            if (type == "heightmap")      return { ".png", ".r16", ".raw", ".hdr" };
            if (type == "hdri")           return { ".hdr", ".exr" };
            if (type == "prefab")         return { ".cprefab" };
            if (type == "voxel_palette")  return { ".cpal" };
            if (type == "voxel_volume")   return { ".cvox" };
            return {};   // unknown → list everything
        }

        // Translate an absolute pick under the active project root to its project://
        // form (so scenes stay portable); otherwise keep the absolute path (the caller
        // may copy it in). Cheap string compare on the resolved project root.
        inline std::string ToProjectRelative(const std::string& absolute)
        {
            std::string root = Cosmic::FileSystem::Resolve("project://");
            for (auto& c : root)     if (c == '\\') c = '/';
            std::string abs = absolute;
            for (auto& c : abs)      if (c == '\\') c = '/';
            if (!root.empty() && abs.size() > root.size() &&
                abs.compare(0, root.size(), root) == 0)
            {
                std::string rel = abs.substr(root.size());
                while (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());
                return "project://" + rel;
            }
            return absolute;
        }
    }

    // Rich AssetPath slot (T11 / gap §5.3): [thumb | name-button → reveal | ▾
    // picker (filtered by the field's asset type) | ✕ clear]; also a drop target
    // for the ASSET_PATH payload. Discrete edits commit immediately (Committed),
    // so the Inspector records one undo step via its usual commit path. `value`
    // holds the new vfs on change.
    inline Result DrawAssetSlot(const FieldDescriptor& f, FieldValue& value, const SlotContext& slot)
    {
        namespace fs = std::filesystem;
        Result r;
        r.PreValue = value;
        const std::string cur = std::get<std::string>(r.PreValue);
        const std::string type = f.Hints.AssetType;
        const bool readOnly = f.HasFlag(Cosmic::Reflect::Field_ReadOnly);

        ImGui::PushID(f.Name.c_str());
        if (readOnly) ImGui::BeginDisabled(true);

        // Lower-case extension of the current value (thumbnail + glyph choice).
        std::string ext;
        if (!cur.empty())
        {
            ext = fs::path(cur).extension().string();
            for (char& c : ext) c = (char)std::tolower((unsigned char)c);
        }

        // Label.
        ImGui::TextUnformatted(f.Name.c_str());
        ImGui::SameLine();

        const float th = ImGui::GetFrameHeight();

        // --- Thumbnail (image / PreviewRig, else a type glyph tile) -----------
        Cosmic::Ref<Cosmic::Texture2D> tex;
        if (!cur.empty())
        {
            const bool isImg = ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                               ext == ".tga" || ext == ".bmp";
            if (isImg)
                tex = Cosmic::AssetLibrary::GetTexture(cur);
            else if (slot.Preview && (ext == ".cmat" ||
                     (ext.size() > 1 && PreviewRig::IsMeshExtension(ext.substr(1)))))
                tex = slot.Preview->Thumbnail(cur);
        }
        if (tex)
        {
            ImGui::Image((ImTextureID)(intptr_t)tex->GetRendererID(), ImVec2(th, th), ImVec2(0, 1), ImVec2(1, 0));
        }
        else
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(th, th));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const AssetTypeInfo& info = AssetTypeForExt(ext);
            const ImU32 col = cur.empty() ? IM_COL32(70, 70, 78, 255) : info.Color;
            dl->AddRectFilled(p, ImVec2(p.x + th, p.y + th), col, 3.0f);
            dl->AddText(ImVec2(p.x + th * 0.22f, p.y + th * 0.12f), IM_COL32(240, 240, 240, 230),
                        cur.empty() ? ICON_LC_SQUARE_DASHED : info.Glyph);
        }
        ImGui::SameLine();

        // --- Name button (click → reveal in browser) + row-wide drop target ---
        const std::string name = cur.empty() ? "(none)" : fs::path(cur).filename().string();
        const float btnW = ImGui::GetFrameHeight();
        const float avail = ImGui::GetContentRegionAvail().x;
        const float nameW = std::max(48.0f, avail - 2.0f * btnW - 2.0f * ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::Button((name + "##name").c_str(), ImVec2(nameW, 0)))
            if (!cur.empty() && slot.PendingReveal) *slot.PendingReveal = cur;
        if (ImGui::IsItemHovered() && !cur.empty())
            ImGui::SetTooltip("%s\n(click to reveal in the Content Browser)", cur.c_str());
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET_PATH"))
            {
                value = FieldValue{ std::string((const char*)pl->Data) };
                r.Changed = true; r.Committed = true;
            }
            ImGui::EndDragDropTarget();
        }

        // --- ▾ picker popup (filtered recursive scan) -------------------------
        ImGui::SameLine();
        if (ImGui::Button(ICON_LC_CHEVRON_DOWN "##pick"))
            ImGui::OpenPopup("##slotpick");
        if (ImGui::BeginPopup("##slotpick"))
        {
            const std::vector<std::string> exts = detail::ExtensionsForAssetType(type);
            ImGui::TextDisabled("Pick a %s", type.empty() ? "asset" : type.c_str());
            ImGui::Separator();
            ImGui::BeginChild("##slotlist", ImVec2(300, 260), false);
            if (ImGui::Selectable("(none)", cur.empty()))
            {
                value = FieldValue{ std::string() };
                r.Changed = true; r.Committed = true;
                ImGui::CloseCurrentPopup();
            }
            std::error_code ec;
            const std::string rootDisk = Cosmic::FileSystem::Resolve("project://");
            for (auto it = fs::recursive_directory_iterator(rootDisk, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                if (ec) break;
                if (!it->is_regular_file(ec)) continue;
                std::string e = it->path().extension().string();
                for (char& c : e) c = (char)std::tolower((unsigned char)c);
                if (!exts.empty() && std::find(exts.begin(), exts.end(), e) == exts.end())
                    continue;
                fs::path rel = fs::relative(it->path(), rootDisk, ec);
                if (ec) continue;
                const std::string vfs = "project://" + rel.generic_string();
                if (ImGui::Selectable(vfs.c_str(), vfs == cur))
                {
                    value = FieldValue{ vfs };
                    r.Changed = true; r.Committed = true;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }

        // --- ✕ clear ----------------------------------------------------------
        ImGui::SameLine();
        if (ImGui::Button(ICON_LC_X "##clear") && !cur.empty())
        {
            value = FieldValue{ std::string() };
            r.Changed = true; r.Committed = true;
        }

        r.PostValue = value;
        if (readOnly) ImGui::EndDisabled();
        ImGui::PopID();
        return r;
    }

    // Draw the widget for one field editing a boxed FieldValue IN PLACE (no
    // component pointer). Used both for reflected components (via DrawField) and
    // for a script's dynamic field-override map (which has no live instance in
    // edit mode). Returns the row Result; `value` holds the new value on Changed.
    // When `slot` is supplied and the field is an AssetPath, the rich T11 slot
    // renders instead of the legacy text input.
    inline Result DrawValue(const FieldDescriptor& f, FieldValue& value, bool mixed,
                            const SlotContext* slot = nullptr)
    {
        if (f.Kind == FieldKind::AssetPath && slot)
            return DrawAssetSlot(f, value, *slot);

        Result r;
        r.PreValue = value;
        const bool readOnly = f.HasFlag(Cosmic::Reflect::Field_ReadOnly);
        if (readOnly) ImGui::BeginDisabled(true);

        // Discrete widgets (checkbox / combo / drag-drop) don't reliably raise
        // IsItemDeactivatedAfterEdit, so they force an immediate commit.
        bool forceCommit = false;

        const char* label = f.Name.c_str();
        ImGui::PushID(label);

        switch (f.Kind)
        {
            case FieldKind::Bool:
            {
                bool v = std::get<bool>(r.PreValue);
                if (ImGui::Checkbox(label, &v)) { value = FieldValue{ v }; r.Changed = true; forceCommit = true; }
                break;
            }
            case FieldKind::Int32:
            {
                int v = std::get<int32_t>(r.PreValue);
                const bool ranged = f.Hints.HasRange && f.Hints.Max > f.Hints.Min;
                bool changed;
                if (ranged && (f.Hints.Max - f.Hints.Min) <= 1000.0f)
                    changed = ImGui::SliderInt(label, &v, (int)f.Hints.Min, (int)f.Hints.Max);
                else
                    changed = ImGui::DragInt(label, &v, detail::StepOr(f, 1.0f),
                                             ranged ? (int)f.Hints.Min : 0,
                                             ranged ? (int)f.Hints.Max : 0);
                if (changed) { value = FieldValue{ (int32_t)v }; r.Changed = true; }
                break;
            }
            case FieldKind::UInt32:
            {
                int v = (int)std::get<uint32_t>(r.PreValue);
                if (ImGui::DragInt(label, &v, detail::StepOr(f, 1.0f), 0,
                                   f.Hints.HasRange ? (int)f.Hints.Max : 0))
                { if (v < 0) v = 0; value = FieldValue{ (uint32_t)v }; r.Changed = true; }
                break;
            }
            case FieldKind::Float:
            {
                float v = std::get<float>(r.PreValue);
                const bool ranged = f.Hints.HasRange && f.Hints.Max > f.Hints.Min;
                // Format carries the unit suffix (° / m / s) — T1/T10.
                char fmt[16];
                std::snprintf(fmt, sizeof(fmt), "%%.4g%s", detail::UnitSuffix(f.Hints.Units));
                // Bounded, human-scale ranges get a slider; wide ranges keep a
                // clamped drag (a slider over 1..100000 is unusable).
                const bool useSlider = ranged && (f.Hints.Max - f.Hints.Min) <= 100.0f;
                bool changed;
                if (useSlider)
                    changed = ImGui::SliderFloat(label, &v, f.Hints.Min, f.Hints.Max, fmt);
                else
                    changed = ImGui::DragFloat(label, &v, detail::StepOr(f, 0.02f),
                                               ranged ? f.Hints.Min : 0.0f,
                                               ranged ? f.Hints.Max : 0.0f, fmt);
                if (changed) { value = FieldValue{ v }; r.Changed = true; }
                break;
            }
            case FieldKind::Vec2:
            {
                glm::vec2 v = std::get<glm::vec2>(r.PreValue);
                if (ImGui::DragFloat2(label, &v.x, detail::StepOr(f, 0.02f)))
                { value = FieldValue{ v }; r.Changed = true; }
                break;
            }
            case FieldKind::Vec3:
            {
                glm::vec3 v = std::get<glm::vec3>(r.PreValue);
                // Heuristic: vec3 fields named "*Color" get a colour swatch (the
                // registry's .Color() only upgrades vec4s).
                const bool asColor = f.Name.size() >= 5 &&
                    f.Name.compare(f.Name.size() - 5, 5, "Color") == 0;
                bool changed = asColor
                    ? ImGui::ColorEdit3(label, &v.x)
                    : ImGui::DragFloat3(label, &v.x, detail::StepOr(f, 0.02f));
                if (changed) { value = FieldValue{ v }; r.Changed = true; }
                break;
            }
            case FieldKind::Vec4:
            {
                glm::vec4 v = std::get<glm::vec4>(r.PreValue);
                if (ImGui::DragFloat4(label, &v.x, detail::StepOr(f, 0.02f)))
                { value = FieldValue{ v }; r.Changed = true; }
                break;
            }
            case FieldKind::Quat:
            {
                glm::quat q = std::get<glm::quat>(r.PreValue);
                float wxyz[4] = { q.w, q.x, q.y, q.z };
                if (ImGui::DragFloat4(label, wxyz, 0.01f, -1.0f, 1.0f, "%.3f"))
                { value = FieldValue{ glm::quat(wxyz[0], wxyz[1], wxyz[2], wxyz[3]) }; r.Changed = true; }
                break;
            }
            case FieldKind::Color:
            {
                glm::vec4 v = std::get<glm::vec4>(r.PreValue);
                if (ImGui::ColorEdit4(label, &v.x))
                { value = FieldValue{ v }; r.Changed = true; }
                break;
            }
            case FieldKind::Enum:
            {
                int32_t cur = std::get<int32_t>(r.PreValue);
                const auto& entries = f.Hints.EnumEntries;
                const char* preview = "?";
                for (const auto& e : entries) if (e.Value == cur) preview = e.Name.c_str();
                if (ImGui::BeginCombo(label, preview))
                {
                    for (const auto& e : entries)
                    {
                        const bool sel = (e.Value == cur);
                        if (ImGui::Selectable(e.Name.c_str(), sel))
                        { value = FieldValue{ (int32_t)e.Value }; r.Changed = true; forceCommit = true; }
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                break;
            }
            case FieldKind::AssetPath:
            case FieldKind::String:
            {
                std::string s = std::get<std::string>(r.PreValue);
                char buf[512];
                std::snprintf(buf, sizeof(buf), "%s", s.c_str());
                const bool asset = (f.Kind == FieldKind::AssetPath);
                if (asset)
                {
                    // Show the field's display name (H10): asset slots used a hidden
                    // "##path" id, so multiple slots (e.g. Terrain's 4 splat textures)
                    // rendered as indistinguishable bare inputs.
                    ImGui::TextUnformatted(label);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(-60.0f);
                }
                if (ImGui::InputText(asset ? "##path" : label, buf, sizeof(buf)))
                { value = FieldValue{ std::string(buf) }; r.Changed = true; }
                if (asset)
                {
                    // Accept a content-browser asset drag (E10 payload).
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                        {
                            value = FieldValue{ std::string((const char*)p->Data) };
                            r.Changed = true; forceCommit = true;
                        }
                        ImGui::EndDragDropTarget();
                    }
                    // "…" native file picker (H6): filter by the slot's asset type;
                    // a pick under the project root becomes a project:// path.
                    ImGui::SameLine();
                    if (ImGui::SmallButton("...##pick"))
                    {
                        Cosmic::FileDialogDesc dlg;
                        dlg.Title      = "Choose asset";
                        dlg.Filters    = detail::FiltersForAssetType(f.Hints.AssetType);
                        dlg.InitialDir = "project://";
                        if (auto picked = Cosmic::FileDialog::Open(dlg))
                        {
                            value = FieldValue{ detail::ToProjectRelative(*picked) };
                            r.Changed = true; forceCommit = true;
                        }
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", f.Hints.AssetType.empty() ? "asset" : f.Hints.AssetType.c_str());
                }
                break;
            }
            case FieldKind::EntityRef:
            {
                uint64_t ref = std::get<uint64_t>(r.PreValue);
                char text[64];
                std::snprintf(text, sizeof(text), "Entity 0x%llx", (unsigned long long)ref);
                ImGui::Button(ref ? text : "(none)", ImVec2(-1.0f, 0.0f));
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ENTITY_UUID"))
                    {
                        value = FieldValue{ *(const uint64_t*)p->Data };
                        r.Changed = true; forceCommit = true;
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine(); ImGui::TextDisabled("%s", label);
                break;
            }
        }

        if (f.Kind != FieldKind::EntityRef)   // buttons above set their own state
            detail::FinishItem(r, value, f);
        else
            r.PostValue = value;

        if (forceCommit) r.Committed = true;

        // T10 — a dimmed ⓘ at the row's end reveals the field's Doc on hover.
        if (!f.Hints.Tooltip.empty())
        {
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextDisabled(ICON_LC_INFO);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", f.Hints.Tooltip.c_str());
        }

        ImGui::PopID();
        if (readOnly) ImGui::EndDisabled();
        (void)mixed;
        return r;
    }

    // Draw the widget for one field of `comp` (a reflected component pointer).
    // When `mixed` is true (multi-select, differing values) the control still
    // writes to `comp` on edit — the Inspector fans it out. Returns the Result.
    inline Result DrawField(const FieldDescriptor& f, void* comp, bool mixed,
                            const SlotContext* slot = nullptr)
    {
        FieldValue v = f.Get(comp);
        Result r = DrawValue(f, v, mixed, slot);
        if (r.Changed) f.Set(comp, v);   // apply live to the component
        return r;
    }
}
