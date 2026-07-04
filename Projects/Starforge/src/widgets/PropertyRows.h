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

#include <cstdio>
#include <cstring>
#include <string>

namespace Starforge::PropertyRows
{
    using Cosmic::Reflect::FieldDescriptor;
    using Cosmic::Reflect::FieldKind;
    using Cosmic::Reflect::FieldValue;

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

        // Fold ImGui's per-frame item state into the row Result.
        inline void FinishItem(Result& r, void* comp, const FieldDescriptor& f)
        {
            r.Activated = ImGui::IsItemActivated();
            r.Committed = ImGui::IsItemDeactivatedAfterEdit();
            r.PostValue = f.Get(comp);
            if (f.Hints.Tooltip.size() && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", f.Hints.Tooltip.c_str());
        }
    }

    // Draw the widget for one field of `comp`. When `mixed` is true (multi-select
    // with differing values) the control shows a neutral state but still writes
    // to `comp` on edit (the Inspector fans it out). Returns the row Result.
    inline Result DrawField(const FieldDescriptor& f, void* comp, bool mixed)
    {
        Result r;
        r.PreValue = f.Get(comp);
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
                if (ImGui::Checkbox(label, &v)) { f.Set(comp, FieldValue{ v }); r.Changed = true; forceCommit = true; }
                break;
            }
            case FieldKind::Int32:
            {
                int v = std::get<int32_t>(r.PreValue);
                const bool ranged = f.Hints.HasRange;
                if (ImGui::DragInt(label, &v, detail::StepOr(f, 1.0f),
                                   ranged ? (int)f.Hints.Min : 0,
                                   ranged ? (int)f.Hints.Max : 0))
                { f.Set(comp, FieldValue{ (int32_t)v }); r.Changed = true; }
                break;
            }
            case FieldKind::UInt32:
            {
                int v = (int)std::get<uint32_t>(r.PreValue);
                if (ImGui::DragInt(label, &v, detail::StepOr(f, 1.0f), 0,
                                   f.Hints.HasRange ? (int)f.Hints.Max : 0))
                { if (v < 0) v = 0; f.Set(comp, FieldValue{ (uint32_t)v }); r.Changed = true; }
                break;
            }
            case FieldKind::Float:
            {
                float v = std::get<float>(r.PreValue);
                const bool ranged = f.Hints.HasRange;
                if (ImGui::DragFloat(label, &v, detail::StepOr(f, 0.02f),
                                     ranged ? f.Hints.Min : 0.0f,
                                     ranged ? f.Hints.Max : 0.0f, "%.4g"))
                { f.Set(comp, FieldValue{ v }); r.Changed = true; }
                break;
            }
            case FieldKind::Vec2:
            {
                glm::vec2 v = std::get<glm::vec2>(r.PreValue);
                if (ImGui::DragFloat2(label, &v.x, detail::StepOr(f, 0.02f)))
                { f.Set(comp, FieldValue{ v }); r.Changed = true; }
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
                if (changed) { f.Set(comp, FieldValue{ v }); r.Changed = true; }
                break;
            }
            case FieldKind::Vec4:
            {
                glm::vec4 v = std::get<glm::vec4>(r.PreValue);
                if (ImGui::DragFloat4(label, &v.x, detail::StepOr(f, 0.02f)))
                { f.Set(comp, FieldValue{ v }); r.Changed = true; }
                break;
            }
            case FieldKind::Quat:
            {
                glm::quat q = std::get<glm::quat>(r.PreValue);
                float wxyz[4] = { q.w, q.x, q.y, q.z };
                if (ImGui::DragFloat4(label, wxyz, 0.01f, -1.0f, 1.0f, "%.3f"))
                { f.Set(comp, FieldValue{ glm::quat(wxyz[0], wxyz[1], wxyz[2], wxyz[3]) }); r.Changed = true; }
                break;
            }
            case FieldKind::Color:
            {
                glm::vec4 v = std::get<glm::vec4>(r.PreValue);
                if (ImGui::ColorEdit4(label, &v.x))
                { f.Set(comp, FieldValue{ v }); r.Changed = true; }
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
                        { f.Set(comp, FieldValue{ (int32_t)e.Value }); r.Changed = true; forceCommit = true; }
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
                if (asset) ImGui::SetNextItemWidth(-60.0f);
                if (ImGui::InputText(asset ? "##path" : label, buf, sizeof(buf)))
                { f.Set(comp, FieldValue{ std::string(buf) }); r.Changed = true; }
                if (asset)
                {
                    // Accept a content-browser asset drag (E10 payload).
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                        {
                            f.Set(comp, FieldValue{ std::string((const char*)p->Data) });
                            r.Changed = true; forceCommit = true;
                        }
                        ImGui::EndDragDropTarget();
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
                        f.Set(comp, FieldValue{ *(const uint64_t*)p->Data });
                        r.Changed = true; forceCommit = true;
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine(); ImGui::TextDisabled("%s", label);
                break;
            }
        }

        if (f.Kind != FieldKind::EntityRef)   // buttons above set their own state
            detail::FinishItem(r, comp, f);
        else
            r.PostValue = f.Get(comp);

        if (forceCommit) r.Committed = true;

        ImGui::PopID();
        if (readOnly) ImGui::EndDisabled();
        (void)mixed;
        return r;
    }
}
