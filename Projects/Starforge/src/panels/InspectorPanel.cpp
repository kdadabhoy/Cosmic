// InspectorPanel.cpp — see header.

#include "panels/InspectorPanel.h"
#include "commands/EditorCommands.h"
#include "widgets/PropertyRows.h"
#include "TelemetryRecording.h"
#include "ui/IconsLucide.h"
#include "scene/SceneSerializer.h"   // T12 — reflected-struct copy/paste clipboard

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace Cosmic;
using Cosmic::Reflect::FieldDescriptor;
using Cosmic::Reflect::FieldValue;
using Cosmic::Reflect::TypeDescriptor;

namespace Starforge
{
    namespace
    {
        const entt::id_type kTagId = entt::type_hash<TagComponent>::value();

        // Case-insensitive substring test (T9 property search).
        bool ContainsCI(const std::string& hay, const std::string& needle)
        {
            if (needle.empty()) return true;
            auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
            return it != hay.end();
        }

        // Components common to EVERY selected entity (intersection), sorted by
        // category then name for a stable layout.
        std::vector<const TypeDescriptor*> CommonComponents(EditorContext& ctx)
        {
            std::vector<const TypeDescriptor*> out;
            if (ctx.Selection.empty() || !ctx.Scene) return out;

            auto& reg = ctx.Scene->GetRegistry();
            auto& registry = Reflect::GetRegistry();
            for (const TypeDescriptor* d : registry.ComponentsOf(reg, ctx.Selection.front()))
            {
                bool inAll = true;
                for (size_t i = 1; i < ctx.Selection.size() && inAll; ++i)
                    inAll = d->Has && d->Has(reg, ctx.Selection[i]);
                if (inAll) out.push_back(d);
            }
            std::sort(out.begin(), out.end(), [](const TypeDescriptor* a, const TypeDescriptor* b)
            {
                if (a->Category != b->Category) return a->Category < b->Category;
                return a->Name < b->Name;
            });
            return out;
        }

        // T12 — the component-values clipboard (reflected-JSON, keyed by type name).
        std::string s_ClipType;
        std::string s_ClipJson;

        // T12 — copy every reflected field from `src` onto `comp` (the primary),
        // recording one undoable commit per changed field (fanned to the selection
        // by CommitFieldEdit). Used by both Paste Values and Reset to Defaults.
        void ApplyComponentValues(EditorContext& ctx, const TypeDescriptor& desc,
                                  void* comp, const void* src, const std::string& label)
        {
            for (const FieldDescriptor& f : desc.Fields)
            {
                if (f.HasFlag(Cosmic::Reflect::Field_NoSerialize))
                    continue;
                FieldValue newVal = f.Get(src);
                FieldValue oldVal = f.Get(comp);
                if (oldVal == newVal)
                    continue;
                f.Set(comp, newVal);   // apply to the primary; CommitFieldEdit fans out + records
                Commands::CommitFieldEdit(ctx, label, desc.TypeId, f.Name, oldVal, newVal);
            }
        }

        // T9 — does the component (its name or any visible field) match the filter?
        bool ComponentMatchesFilter(const TypeDescriptor& d, const std::string& filter)
        {
            if (ContainsCI(d.Name, filter)) return true;
            for (const FieldDescriptor& f : d.Fields)
                if (!f.HasFlag(Cosmic::Reflect::Field_HideInInspector) && ContainsCI(f.Name, filter))
                    return true;
            return false;
        }

        // Does `field` of type `typeId` differ across the selection?
        bool FieldMixed(EditorContext& ctx, entt::id_type typeId, const FieldDescriptor& f)
        {
            if (ctx.Selection.size() < 2) return false;
            const TypeDescriptor* d = Reflect::GetRegistry().Find(typeId);
            if (!d) return false;
            auto& reg = ctx.Scene->GetRegistry();
            FieldValue first;
            bool have = false;
            for (entt::entity h : ctx.Selection)
            {
                void* comp = d->Get(reg, h);
                if (!comp) continue;
                FieldValue v = f.Get(comp);
                if (!have) { first = v; have = true; }
                else if (v != first) return true;
            }
            return false;
        }
    }

    bool InspectorPanel::NameMatches(const std::string& name) const
    {
        return ContainsCI(name, m_Search);
    }

    void InspectorPanel::OnImGuiRender(EditorContext& ctx, bool* pOpen)
    {
        ImGui::Begin("Inspector", pOpen);

        if (!ctx.HasSelection() || !ctx.PrimaryEntity())
        {
            ImGui::TextDisabled("Select an entity in the Hierarchy.");
            ImGui::End();
            return;
        }

        // T15 — live-Play affordance: tint value backgrounds while playing; edits
        // apply live but push no undo (they die with the Stop snapshot-restore).
        const bool live = ctx.Playing;
        if (live)
        {
            ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0.20f, 0.12f, 0.03f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.28f, 0.17f, 0.05f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(0.36f, 0.22f, 0.06f, 1.0f));
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.30f, 1.0f),
                               ICON_LC_PLAY "  Live — edits are temporary (no undo while playing)");
            ImGui::Separator();
        }

        if (ctx.Selection.size() > 1)
        {
            ImGui::TextDisabled("%d entities selected — editing common components.",
                                (int)ctx.Selection.size());
            ImGui::Separator();
        }

        DrawName(ctx);

        // T9 — property search: narrows to matching fields/components across all
        // components (incl. script fields); clearing restores the full layout.
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##inspSearch", ICON_LC_SEARCH " Search properties…",
                                 m_Search, sizeof(m_Search));
        ImGui::Separator();

        for (const TypeDescriptor* d : CommonComponents(ctx))
        {
            if (d->TypeId == kTagId)   // shown as the Name row above
                continue;
            if (d->Name == "NativeScript")   // E11 — bespoke class picker + fields;
            {                                //       filters its own dynamic fields (T9)
                DrawScriptComponent(ctx, *d);
                continue;
            }
            if (SearchActive() && !ComponentMatchesFilter(*d, m_Search))
                continue;             // no name/field hit — hide the whole component
            DrawComponent(ctx, *d);
        }

        ImGui::Separator();
        DrawAddComponent(ctx);

        if (live)
            ImGui::PopStyleColor(3);

        ImGui::End();
    }

    void InspectorPanel::DrawName(EditorContext& ctx)
    {
        Entity primary = ctx.PrimaryEntity();
        auto* tag = ctx.Scene->GetRegistry().try_get<TagComponent>((entt::entity)primary);
        if (!tag) return;

        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s", tag->Tag.c_str());
        ImGui::SetNextItemWidth(-1.0f);
        const bool edited = ImGui::InputText("##name", buf, sizeof(buf));
        if (ImGui::IsItemActivated()) { m_ActiveBefore = FieldValue{ std::string(tag->Tag) }; m_HasActive = true; }
        if (edited) tag->Tag = buf;   // live
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            FieldValue after{ std::string(tag->Tag) };
            FieldValue before = m_HasActive ? m_ActiveBefore : after;
            if (before != after)
                Commands::CommitFieldEdit(ctx, "Rename", kTagId, "Tag", before, after);
            m_HasActive = false;
        }
    }

    void InspectorPanel::DrawComponent(EditorContext& ctx, const TypeDescriptor& desc)
    {
        Entity primary = ctx.PrimaryEntity();
        auto& reg = ctx.Scene->GetRegistry();
        void* comp = desc.Get(reg, (entt::entity)primary);

        // Stable UUID for telemetry marks (E20) — single-select only.
        const uint64_t uuid = (ctx.Selection.size() == 1 && primary &&
                               primary.HasComponent<Cosmic::IDComponent>())
            ? (uint64_t)primary.GetComponent<Cosmic::IDComponent>().ID : 0;

        ImGui::PushID((int)desc.TypeId);
        // T9 — a search auto-opens matching component headers.
        const bool compNameHit = NameMatches(desc.Name);

        // T12 — a header enable checkbox for any component exposing an "Enabled"
        // field (drawn before the header so SetNextItemOpen still targets it).
        const FieldDescriptor* enabledField = desc.FindField("Enabled");
        if (enabledField && comp)
        {
            bool en = std::get<bool>(enabledField->Get(comp));
            if (ImGui::Checkbox("##enabled", &en))
            {
                enabledField->Set(comp, FieldValue{ en });   // apply to primary
                Commands::CommitFieldEdit(ctx, (en ? "Enable " : "Disable ") + desc.Name,
                                          desc.TypeId, "Enabled", FieldValue{ !en }, FieldValue{ en });
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable / disable this component");
            ImGui::SameLine();
        }

        if (SearchActive())
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        const bool open = ImGui::CollapsingHeader(desc.Name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

        // Header right-click: copy/paste/reset (T12) + remove. Copy/paste/reset
        // work on any component (incl. Transform); Remove is gated (Transform
        // can't be removed) and is single-select only.
        bool removeRequested = false;
        const bool removable = desc.TypeId != entt::type_hash<TransformComponent>::value();
        if (ImGui::BeginPopupContextItem("comp_ctx"))
        {
            if (ImGui::MenuItem("Copy Component") && comp)
            {
                s_ClipType = desc.Name;
                s_ClipJson = SceneSerializer::SaveReflectedToString(desc.TypeId, comp);
            }
            const bool canPaste = (s_ClipType == desc.Name) && !s_ClipJson.empty();
            if (ImGui::MenuItem("Paste Values", nullptr, false, canPaste) && comp)
            {
                entt::registry tmp;
                entt::entity   te  = tmp.create();
                void*          src = desc.Add(tmp, te);
                if (src && SceneSerializer::LoadReflectedFromString(desc.TypeId, src, s_ClipJson))
                    ApplyComponentValues(ctx, desc, comp, src, "Paste " + desc.Name);
            }
            if (ImGui::MenuItem("Reset to Defaults") && comp)
            {
                entt::registry tmp;
                entt::entity   te  = tmp.create();
                void*          def = desc.Add(tmp, te);   // default-constructed
                if (def)
                    ApplyComponentValues(ctx, desc, comp, def, "Reset " + desc.Name);
            }
            if (removable)
            {
                ImGui::Separator();
                if (ImGui::MenuItem("Remove Component")) removeRequested = true;
            }
            ImGui::EndPopup();
        }

        if (open && comp)
        {
            if (desc.Fields.empty())
                ImGui::TextDisabled("(no editable properties yet)");

            for (const FieldDescriptor& f : desc.Fields)
            {
                if (f.HasFlag(Cosmic::Reflect::Field_HideInInspector))
                    continue;
                // T9 — when searching, show all fields of a name-matched component;
                // otherwise only the fields whose own name matches.
                if (SearchActive() && !compNameHit && !NameMatches(f.Name))
                    continue;
                const bool mixed = FieldMixed(ctx, desc.TypeId, f);
                PropertyRows::SlotContext slot{ &ctx.Preview, &ctx.PendingRevealAsset };
                PropertyRows::Result res = PropertyRows::DrawField(f, comp, mixed, &slot);

                // Right-click a numeric field to (un)mark it for telemetry (E20).
                if (uuid && Telemetry::IsRecordable(f.Kind))
                {
                    ImGui::PushID(&f);
                    if (ImGui::BeginPopupContextItem("rec_ctx"))
                    {
                        const bool rec = Telemetry::IsRecorded(ctx, uuid, desc.TypeId, f.Name);
                        if (ImGui::MenuItem(rec ? "Stop Recording" : "Record for telemetry", nullptr, rec))
                            Telemetry::ToggleRecorded(ctx, uuid, desc.TypeId, f);
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                    if (Telemetry::IsRecorded(ctx, uuid, desc.TypeId, f.Name))
                    {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "REC");
                    }
                }

                if (res.Activated) { m_ActiveBefore = res.PreValue; m_HasActive = true; }
                if (res.Committed)
                {
                    FieldValue before = m_HasActive ? m_ActiveBefore : res.PreValue;
                    if (!(before == res.PostValue))
                        Commands::CommitFieldEdit(ctx, "Edit " + desc.Name + "." + f.Name,
                                                  desc.TypeId, f.Name, before, res.PostValue);
                    m_HasActive = false;
                }
            }

            // Animator clip picker (A2): list the clips inside the model file
            // this animator drives (own or descendant MeshRenderer), and write
            // the picked "file#clip" through the undo stack. The scrub bar is
            // the reflected NormalizedTime row above (Range 0..1); pausing +
            // dragging it re-poses the mesh live in the viewport.
            if (desc.Name == "Animator" && ctx.Selection.size() == 1 && primary)
            {
                auto* an = static_cast<AnimatorComponent*>(comp);

                // The driven model file: this entity's (or a child's) MeshPath,
                // sub-mesh fragment stripped.
                std::string meshSource;
                {
                    std::vector<Entity> stack{ primary };
                    while (!stack.empty() && meshSource.empty())
                    {
                        Entity e = stack.back();
                        stack.pop_back();
                        if (e.HasComponent<MeshRendererComponent>())
                        {
                            const auto& mr = e.GetComponent<MeshRendererComponent>();
                            if (!mr.MeshPath.empty())
                            {
                                std::string base;
                                int         sub = 0;
                                meshSource = Cosmic::MeshImport::SplitSubmeshPath(mr.MeshPath, base, sub)
                                                 ? base : mr.MeshPath;
                            }
                        }
                        if (e.HasComponent<RelationshipComponent>())
                            for (const Cosmic::UUID& c : e.GetComponent<RelationshipComponent>().Children)
                                if (Entity child = ctx.Scene->FindByUUID(c))
                                    stack.push_back(child);
                    }
                }

                if (meshSource.empty())
                {
                    ImGui::TextDisabled("No mesh with a source path under this entity.");
                }
                else
                {
                    const size_t hash = an->ClipPath.find_last_of('#');
                    const std::string current = hash == std::string::npos
                        ? std::string() : an->ClipPath.substr(hash + 1);
                    // The clip set loads (cached) only while the combo is open,
                    // so a broken source can't log per frame.
                    if (ImGui::BeginCombo("Clip", current.empty() ? "(pick a clip)" : current.c_str()))
                    {
                        const std::vector<std::string> names =
                            Cosmic::AssetLibrary::GetAnimationClipNames(meshSource);
                        if (names.empty())
                            ImGui::TextDisabled("No clips in %s", meshSource.c_str());
                        for (const std::string& name : names)
                        {
                            const bool selected = name == current;
                            if (ImGui::Selectable(name.c_str(), selected))
                                Commands::SetField(ctx, primary, desc.TypeId, "ClipPath",
                                                   FieldValue{ meshSource + "#" + name });
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    if (an->ClipRef)
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("%.2fs", an->ClipRef->Duration);
                    }

                    // M6 — crossfade readout (script-driven via Animator().CrossfadeTo;
                    // no authoring here — the full controller graph stays parked).
                    if (!an->NextClipPath.empty() && an->FadeDuration > 0.0f)
                    {
                        const size_t h = an->NextClipPath.find_last_of('#');
                        const std::string next = h == std::string::npos
                            ? an->NextClipPath : an->NextClipPath.substr(h + 1);
                        const float w = std::clamp(an->FadeElapsed / an->FadeDuration, 0.0f, 1.0f);
                        ImGui::TextDisabled(ICON_LC_SHARE_2 " Crossfading → %s", next.c_str());
                        ImGui::ProgressBar(w, ImVec2(-1.0f, 0.0f));
                    }
                }
            }

            // Material slots (M5): a multi-material mesh (its Mesh carries >= 2
            // material slots) gets a "Materials" list — one asset slot per material
            // index. EMPTY MaterialPaths ⇒ the legacy single MaterialPath row above
            // drives the whole mesh (compat). Drops/clears are undoable.
            if (desc.Name == "MeshRenderer" && ctx.Selection.size() == 1 && primary)
            {
                auto* mr = static_cast<MeshRendererComponent*>(comp);
                const uint32_t meshSlots = mr->MeshAsset ? mr->MeshAsset->GetMaterialSlotCount() : 0u;
                const size_t slotCount = std::max((size_t)meshSlots, mr->MaterialPaths.size());
                if (slotCount >= 2)
                {
                    auto stemOf = [](const std::string& p) -> std::string
                    {
                        const size_t s = p.find_last_of("/\\");
                        return s == std::string::npos ? p : p.substr(s + 1);
                    };

                    ImGui::SeparatorText("Materials");
                    for (size_t i = 0; i < slotCount; ++i)
                    {
                        ImGui::PushID((int)(4096 + i));
                        const std::string cur = i < mr->MaterialPaths.size()
                            ? mr->MaterialPaths[i] : std::string();

                        ImGui::Text("Slot %d", (int)i);
                        ImGui::SameLine(72.0f);
                        const std::string shown = cur.empty() ? std::string("(drop a .cmat)")
                                                              : stemOf(cur);
                        ImGui::Button(shown.c_str(), ImVec2(-28.0f, 0.0f));
                        if (ImGui::BeginDragDropTarget())
                        {
                            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                            {
                                const std::string dropped(static_cast<const char*>(p->Data));
                                if (dropped.size() > 5 &&
                                    dropped.compare(dropped.size() - 5, 5, ".cmat") == 0)
                                    Commands::SetMaterialSlot(ctx, primary, i, dropped);
                            }
                            ImGui::EndDragDropTarget();
                        }
                        if (!cur.empty() && ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", cur.c_str());
                        ImGui::SameLine();
                        if (ImGui::SmallButton("x"))
                            Commands::SetMaterialSlot(ctx, primary, i, std::string());
                        ImGui::PopID();
                    }
                    ImGui::TextDisabled("Per-submesh override; an empty slot uses the material above.");
                }
            }

            // "Fit to mesh" (J8): size a box/sphere collider to the sibling mesh's
            // local AABB, single-select only (undoable via CommitFieldEdit).
            if ((desc.Name == "BoxCollider" || desc.Name == "SphereCollider") &&
                ctx.Selection.size() == 1 && primary &&
                primary.HasComponent<MeshRendererComponent>() &&
                primary.GetComponent<MeshRendererComponent>().MeshAsset)
            {
                if (ImGui::SmallButton("Fit to mesh"))
                {
                    const auto& mesh = *primary.GetComponent<MeshRendererComponent>().MeshAsset;
                    const glm::vec3 mn = mesh.GetLocalMin(), mx = mesh.GetLocalMax();
                    const glm::vec3 center = 0.5f * (mn + mx);
                    const glm::vec3 half   = glm::max((mx - mn) * 0.5f, glm::vec3(0.01f));
                    if (desc.Name == "BoxCollider")
                    {
                        auto* c = static_cast<BoxColliderComponent*>(comp);
                        Commands::CommitFieldEdit(ctx, "Fit box collider", desc.TypeId, "HalfExtents",
                                                  FieldValue{ c->HalfExtents }, FieldValue{ half });
                        Commands::CommitFieldEdit(ctx, "Fit box collider", desc.TypeId, "Offset",
                                                  FieldValue{ c->Offset }, FieldValue{ center });
                    }
                    else
                    {
                        auto* c = static_cast<SphereColliderComponent*>(comp);
                        const float r = glm::max(glm::max(half.x, half.y), half.z);
                        Commands::CommitFieldEdit(ctx, "Fit sphere collider", desc.TypeId, "Radius",
                                                  FieldValue{ c->Radius }, FieldValue{ r });
                        Commands::CommitFieldEdit(ctx, "Fit sphere collider", desc.TypeId, "Offset",
                                                  FieldValue{ c->Offset }, FieldValue{ center });
                    }
                }
            }
        }
        ImGui::PopID();

        if (removeRequested && ctx.Selection.size() == 1)
            Commands::RemoveComponent(ctx, primary, desc.TypeId);
    }

    namespace
    {
        // Populate a component's Fields map with the script's default values by
        // spinning up a throwaway instance and pulling them back out. Called when a
        // class is first chosen so the fields display + serialize immediately.
        void SeedScriptDefaults(NativeScriptComponent& nsc)
        {
            nsc.Fields.clear();
            const ScriptDescriptor* sd = ModuleRegistry::Get().FindScript(nsc.ClassName);
            if (!sd || !sd->Factory) return;
            ScriptableEntity* tmp = sd->Factory();
            ScriptHost::PullFields(*sd, tmp, nsc);
            delete tmp;
        }
    }

    void InspectorPanel::DrawScriptComponent(EditorContext& ctx, const TypeDescriptor& desc)
    {
        Entity primary = ctx.PrimaryEntity();
        auto& reg = ctx.Scene->GetRegistry();
        auto* nsc = static_cast<NativeScriptComponent*>(desc.Get(reg, (entt::entity)primary));
        if (!nsc) return;

        // T9 — the script section matches when "script"/the class name matches, or
        // any of its dynamic fields do; the header auto-opens under a search.
        const ScriptDescriptor* sdMatch = nsc->ClassName.empty()
            ? nullptr : ModuleRegistry::Get().FindScript(nsc->ClassName);
        const bool sectionHit = NameMatches("Native Script") || NameMatches("script") ||
                                (!nsc->ClassName.empty() && NameMatches(nsc->ClassName));
        if (SearchActive() && !sectionHit)
        {
            bool anyField = false;
            if (sdMatch)
                for (const auto& sf : sdMatch->Fields.Fields)
                    if (NameMatches(sf.Name)) { anyField = true; break; }
            if (!anyField) return;   // nothing in this script matches — hide it
        }

        ImGui::PushID((int)desc.TypeId);
        if (SearchActive())
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        const bool open = ImGui::CollapsingHeader("Native Script", ImGuiTreeNodeFlags_DefaultOpen);

        bool removeRequested = false;
        if (ImGui::BeginPopupContextItem("script_ctx"))
        {
            if (ImGui::MenuItem("Remove Component")) removeRequested = true;
            ImGui::EndPopup();
        }

        if (open)
        {
            // Class picker from the loaded module's registered scripts.
            const std::vector<std::string> names = ModuleRegistry::Get().ScriptNames();
            const char* preview = nsc->ClassName.empty() ? "(none)" : nsc->ClassName.c_str();
            if (ImGui::BeginCombo("Class", preview))
            {
                if (ImGui::Selectable("(none)", nsc->ClassName.empty()))
                {
                    if (!nsc->ClassName.empty()) { nsc->ClassName.clear(); nsc->Fields.clear(); ctx.MarkDirty(); }
                }
                for (const std::string& n : names)
                {
                    const bool sel = (n == nsc->ClassName);
                    if (ImGui::Selectable(n.c_str(), sel) && n != nsc->ClassName)
                    {
                        nsc->ClassName = n;
                        SeedScriptDefaults(*nsc);
                        ctx.MarkDirty();
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (names.empty())
                ImGui::TextDisabled("No scripts registered — build the project's game module (Ctrl+B).");

            const ScriptDescriptor* sd = nsc->ClassName.empty()
                ? nullptr : ModuleRegistry::Get().FindScript(nsc->ClassName);
            if (!nsc->ClassName.empty() && !sd)
                ImGui::TextDisabled("'%s' is not currently loaded.", nsc->ClassName.c_str());

            if (sd)
            {
                // Backfill any field the map is missing (e.g. loaded before the
                // module, or the script gained a field), then draw each one.
                for (const auto& sf : sd->Fields.Fields)
                    if (nsc->Fields.find(sf.Name) == nsc->Fields.end())
                        SeedScriptDefaults(*nsc);

                for (const auto& sf : sd->Fields.Fields)
                {
                    auto it = nsc->Fields.find(sf.Name);
                    if (it == nsc->Fields.end()) continue;
                    if (SearchActive() && !sectionHit && !NameMatches(sf.Name))
                        continue;   // T9 — only matching script fields
                    PropertyRows::SlotContext slot{ &ctx.Preview, &ctx.PendingRevealAsset };
                    PropertyRows::Result res = PropertyRows::DrawValue(sf, it->second, false, &slot);
                    if (res.Changed || res.Committed) ctx.MarkDirty();
                }
            }
        }
        ImGui::PopID();

        if (removeRequested && ctx.Selection.size() == 1)
            Commands::RemoveComponent(ctx, primary, desc.TypeId);
    }

    void InspectorPanel::DrawAddComponent(EditorContext& ctx)
    {
        const bool single = (ctx.Selection.size() == 1);
        if (!single) ImGui::BeginDisabled(true);

        if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f)))
            ImGui::OpenPopup("add_component");

        if (ImGui::BeginPopup("add_component"))
        {
            Entity primary = ctx.PrimaryEntity();
            auto& reg = ctx.Scene->GetRegistry();

            // Runtime/engine-managed components a user shouldn't hand-add (H10) — hidden
            // behind a toggle. (Only these that are reflected actually appear here.)
            static bool s_ShowInternal = false;
            ImGui::Checkbox("Show internal", &s_ShowInternal);
            ImGui::Separator();
            auto isInternal = [](const std::string& n)
            {
                return n == "Prefab" || n == "Relationship" || n == "ID" || n == "OpaqueComponents";
            };

            // Group registry entries by category; hide ones already present.
            std::map<std::string, std::vector<const TypeDescriptor*>> byCategory;
            for (const auto& [id, desc] : Reflect::GetRegistry().Types())
            {
                if (id == kTagId) continue;   // identity/name, not add-able
                if (desc.Has && desc.Has(reg, (entt::entity)primary)) continue;
                if (!s_ShowInternal && isInternal(desc.Name)) continue;
                byCategory[desc.Category.empty() ? "General" : desc.Category].push_back(&desc);
            }

            for (auto& [cat, list] : byCategory)
            {
                std::sort(list.begin(), list.end(),
                    [](const TypeDescriptor* a, const TypeDescriptor* b) { return a->Name < b->Name; });
                if (ImGui::BeginMenu(cat.c_str()))
                {
                    for (const TypeDescriptor* d : list)
                        if (ImGui::MenuItem(d->Name.c_str()))
                            Commands::AddComponent(ctx, primary, d->TypeId);
                    ImGui::EndMenu();
                }
            }
            ImGui::EndPopup();
        }

        if (!single)
        {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Select a single entity to add components.");
        }
    }
}
