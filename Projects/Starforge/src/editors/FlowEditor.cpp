// editors/FlowEditor.cpp — see FlowEditor.h. Rehost of U6's Flow Graph panel as
// an M1 document (Phase 25 / Q1). Behaviour is unchanged from U6.

#include "editors/FlowEditor.h"
#include "EditorContext.h"
#include "widgets/VariablesPanel.h"   // Q2 — shared blackboard editor

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using namespace Cosmic;

namespace Starforge
{
    namespace
    {
        std::string StemOf(const std::string& path)
        {
            if (path.empty()) return {};
            return fs::path(path).stem().string();
        }

        std::vector<std::string> ListVfs(const std::string& vfsDir, const char* ext)
        {
            std::vector<std::string> out;
            std::error_code ec;
            const fs::path dir = FileSystem::Resolve(vfsDir);
            if (!fs::exists(dir, ec)) return out;
            for (const auto& f : fs::directory_iterator(dir, ec))
            {
                if (!f.is_regular_file(ec) || f.path().extension() != ext) continue;
                out.push_back(vfsDir + "/" + f.path().filename().generic_string());
            }
            std::sort(out.begin(), out.end());
            return out;
        }

        bool VfsExists(const std::string& vfsPath)
        {
            std::error_code ec;
            return !vfsPath.empty() && fs::exists(FileSystem::Resolve(vfsPath), ec);
        }
    }

    // ========================================================================
    // Document lifecycle
    // ========================================================================

    FlowEditor::FlowEditor(std::string vfsPath)
        : m_Path(std::move(vfsPath))
    {
        std::string err;
        if (FlowAsset::Load(m_Asset, m_Path, &err))
        {
            m_Loaded   = true;
            m_SelState = m_Asset.States.empty() ? -1 : 0;
            m_PlaceNodes = true;
            Revalidate();
            ScanKnownSignals();
        }
    }

    std::string FlowEditor::Title() const
    {
        std::string t = StemOf(m_Path);
        return t.empty() ? std::string("Flow") : t;
    }

    bool FlowEditor::SaveFlow(EditorContext& ctx)
    {
        if (!m_Loaded || m_Path.empty())
            return false;
        Revalidate();
        if (!m_Asset.Save(m_Path))
        {
            ctx.Log("[Flow] Save FAILED for " + m_Path, LogSeverity::Error);
            return false;
        }
        m_Dirty = false;
        ctx.Log("[Flow] Saved " + m_Path +
                (m_Problems.empty() ? "" : (" (" + std::to_string(m_Problems.size()) + " warning(s))")));
        return true;
    }

    void FlowEditor::Revalidate()
    {
        m_Problems = m_Asset.Validate();
        m_MissingScene.clear();
        m_Unreachable.clear();

        for (int i = 0; i < (int)m_Asset.States.size(); ++i)
        {
            const FlowState& s = m_Asset.States[i];
            const bool sceneOk = s.Scene.empty() ? s.Overlay : VfsExists(s.Scene);
            if (!sceneOk)
            {
                m_MissingScene.insert(i);
                m_Problems.push_back("State '" + s.Name + "': scene '" + s.Scene +
                                     (s.Scene.empty() ? "(unset)'" : "' not found"));
            }
        }

        std::unordered_set<std::string> reachable;
        std::vector<const FlowState*> queue;
        if (const FlowState* start = m_Asset.Find(m_Asset.Start))
        {
            queue.push_back(start);
            reachable.insert(start->Name);
        }
        while (!queue.empty())
        {
            const FlowState* s = queue.back();
            queue.pop_back();
            for (const FlowTransition& t : s->Transitions)
            {
                if (t.To.empty() || t.To[0] == '@') continue;
                if (reachable.count(t.To)) continue;
                if (const FlowState* n = m_Asset.Find(t.To))
                {
                    reachable.insert(n->Name);
                    queue.push_back(n);
                }
            }
        }
        for (int i = 0; i < (int)m_Asset.States.size(); ++i)
        {
            if (!reachable.count(m_Asset.States[i].Name))
            {
                m_Unreachable.insert(i);
                m_Problems.push_back("State '" + m_Asset.States[i].Name + "' is unreachable from '" +
                                     m_Asset.Start + "'");
            }
        }
    }

    void FlowEditor::ScanKnownSignals()
    {
        std::unordered_set<std::string> found;
        for (const FlowState& s : m_Asset.States)
        {
            for (const FlowTransition& t : s.Transitions)
                if (!t.On.empty()) found.insert(t.On);

            if (s.Scene.empty() || !VfsExists(s.Scene)) continue;
            std::ifstream f(FileSystem::Resolve(s.Scene));
            if (!f) continue;
            nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
            if (j.is_discarded() || !j.contains("entities")) continue;
            for (const auto& e : j["entities"])
            {
                if (!e.contains("components")) continue;
                const auto& comps = e["components"];
                if (comps.contains("UiButton") && comps["UiButton"].contains("Signal") &&
                    comps["UiButton"]["Signal"].is_string())
                    found.insert(comps["UiButton"]["Signal"].get<std::string>());
            }
        }
        found.insert("key:Escape");
        m_KnownSignals.assign(found.begin(), found.end());
        std::sort(m_KnownSignals.begin(), m_KnownSignals.end());
    }

    void FlowEditor::Snapshot()
    {
        m_UndoStack.push_back(m_Asset.SaveToString());
        if (m_UndoStack.size() > 64)
            m_UndoStack.erase(m_UndoStack.begin());
        m_RedoStack.clear();
        m_Dirty = true;
    }

    void FlowEditor::ApplySnapshot(const std::string& json)
    {
        FlowAsset asset;
        if (!FlowAsset::LoadFromString(asset, json))
            return;
        m_Asset = std::move(asset);
        m_SelState = std::min(m_SelState, (int)m_Asset.States.size() - 1);
        m_SelTrans = -1;
        m_Dirty = true;
        m_PlaceNodes = true;
        Revalidate();
    }

    // ========================================================================
    // Document body (no outer Begin/End — the AssetEditorHost owns the tab)
    // ========================================================================

    void FlowEditor::OnImGuiRender(EditorContext& ctx)
    {
        if (!m_Loaded)
        {
            ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.4f, 1.0f),
                               "Could not open flow: %s", m_Path.c_str());
            return;
        }

        DrawToolbar(ctx);
        ImGui::Separator();

        const float inspectorW = 330.0f;
        const float varsW = m_ShowVars ? 240.0f : 0.0f;
        ImGui::BeginChild("flow_canvas_region",
                          ImVec2(ImGui::GetContentRegionAvail().x - inspectorW - varsW - 8.0f, 0.0f),
                          ImGuiChildFlags_None);
        DrawCanvas(ctx);
        ImGui::EndChild();

        if (m_ShowVars)
        {
            ImGui::SameLine();
            ImGui::BeginChild("flow_vars_region", ImVec2(varsW, 0.0f), ImGuiChildFlags_Borders);
            DrawVariablesPanel(ctx);
            ImGui::EndChild();
        }

        ImGui::SameLine();
        ImGui::BeginChild("flow_inspector_region", ImVec2(inspectorW, 0.0f), ImGuiChildFlags_Borders);
        DrawInspector(ctx);
        ImGui::EndChild();
    }

    void FlowEditor::DrawToolbar(EditorContext& ctx)
    {
        if (ImGui::Button("Save"))
            SaveFlow(ctx);

        ImGui::SameLine();
        ImGui::BeginDisabled(m_UndoStack.empty());
        if (ImGui::Button("Undo"))
        {
            m_RedoStack.push_back(m_Asset.SaveToString());
            const std::string s = m_UndoStack.back();
            m_UndoStack.pop_back();
            ApplySnapshot(s);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_RedoStack.empty());
        if (ImGui::Button("Redo"))
        {
            m_UndoStack.push_back(m_Asset.SaveToString());
            const std::string s = m_RedoStack.back();
            m_RedoStack.pop_back();
            ApplySnapshot(s);
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("+ State"))
            ImGui::OpenPopup("flow_new_state");
        if (ImGui::BeginPopup("flow_new_state"))
        {
            ImGui::TextUnformatted("State name");
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputText("##nsname", m_NewStateName, sizeof(m_NewStateName));
            const bool taken = m_Asset.Find(m_NewStateName) != nullptr;
            if (taken)
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "name in use");
            ImGui::BeginDisabled(!m_NewStateName[0] || taken);
            if (ImGui::Button("Add##state"))
            {
                Snapshot();
                FlowState s;
                s.Name = m_NewStateName;
                s.EditorPos = { 60.0f + 40.0f * (float)(m_Asset.States.size() % 8),
                                60.0f + 40.0f * (float)(m_Asset.States.size() % 8) };
                m_Asset.States.push_back(s);
                m_SelState = (int)m_Asset.States.size() - 1;
                m_SelTrans = -1;
                m_PlaceNodes = true;
                Revalidate();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        ImGui::Checkbox("Variables", &m_ShowVars);   // Q2

        ImGui::SameLine();
        if (m_Problems.empty())
            ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1.0f), "valid");
        else
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%d problem(s)",
                               (int)m_Problems.size());
    }

    // ========================================================================
    // Canvas
    // ========================================================================

    void FlowEditor::DrawCanvas(EditorContext& ctx)
    {
        m_Canvas.Begin("flow_graph");

        if (m_PlaceNodes)
        {
            bool allZero = !m_Asset.States.empty();
            for (const FlowState& s : m_Asset.States)
                if (s.EditorPos.x != 0.0f || s.EditorPos.y != 0.0f) { allZero = false; break; }

            for (int i = 0; i < (int)m_Asset.States.size(); ++i)
            {
                const glm::vec2 p = allZero
                    ? glm::vec2(40.0f + 300.0f * (float)(i % 4), 40.0f + 200.0f * (float)(i / 4))
                    : m_Asset.States[i].EditorPos;
                m_Canvas.SetNodePosition(NodeId(i), ImVec2(p.x, p.y));
            }
            float maxX = 0.0f, minY = 40.0f;
            for (const FlowState& s : m_Asset.States)
                maxX = std::max(maxX, s.EditorPos.x);
            m_Canvas.SetNodePosition(kQuitNode, ImVec2(maxX + 340.0f, minY));
            m_PlaceNodes = false;
        }

        const ImVec4 red(1.0f, 0.42f, 0.35f, 1.0f);
        const ImVec4 dim(0.62f, 0.66f, 0.72f, 1.0f);

        for (int i = 0; i < (int)m_Asset.States.size(); ++i)
        {
            FlowState& s = m_Asset.States[i];
            ed::BeginNode(NodeId(i));
            ImGui::PushID((int)NodeId(i));

            ed::BeginPin(InPin(i), ed::PinKind::Input);
            ImGui::TextUnformatted("->");
            ed::EndPin();
            ImGui::SameLine();
            if (s.Name == m_Asset.Start)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.20f, 1.0f), "[start]");
                ImGui::SameLine();
            }
            ImGui::TextUnformatted(s.Name.c_str());
            if (s.Overlay) { ImGui::SameLine(); ImGui::TextColored(dim, "(overlay)"); }

            const bool missing = m_MissingScene.count(i) != 0;
            ImGui::TextColored(missing ? red : dim, "%s",
                               s.Scene.empty() ? (s.Overlay ? "(under-scene)" : "(no scene)")
                                               : StemOf(s.Scene).c_str());
            if (m_Unreachable.count(i))
                ImGui::TextColored(red, "unreachable");

            for (int t = 0; t < (int)s.Transitions.size(); ++t)
            {
                const FlowTransition& tr = s.Transitions[t];
                std::string label = "on " + (tr.On.empty() ? std::string("?") : tr.On);
                if (tr.HasGuard) label += " [if]";
                if (tr.Push)     label += " [push]";
                const bool external = !tr.To.empty() && tr.To[0] == '@';
                if (external) label += " -> " + tr.To;

                ImGui::TextUnformatted(label.c_str());
                ImGui::SameLine();
                ed::BeginPin(OutPin(i, t), ed::PinKind::Output);
                ImGui::TextUnformatted("->");
                ed::EndPin();
            }

            ed::BeginPin(AddPin(i), ed::PinKind::Output);
            ImGui::TextDisabled("+ link");
            ed::EndPin();

            ImGui::PopID();
            ed::EndNode();
        }

        {
            ed::BeginNode(kQuitNode);
            ed::BeginPin(kQuitInPin, ed::PinKind::Input);
            ImGui::TextUnformatted("->");
            ed::EndPin();
            ImGui::SameLine();
            ImGui::TextUnformatted("@quit");
            ImGui::TextColored(dim, "closes the app / stops Play");
            ed::EndNode();
        }

        for (int i = 0; i < (int)m_Asset.States.size(); ++i)
        {
            const FlowState& s = m_Asset.States[i];
            for (int t = 0; t < (int)s.Transitions.size(); ++t)
            {
                const std::string& to = s.Transitions[t].To;
                if (to == "@quit")
                {
                    ed::Link(LinkId(i, t), OutPin(i, t), kQuitInPin,
                             ImVec4(0.9f, 0.5f, 0.4f, 1.0f));
                    continue;
                }
                if (to.empty() || to[0] == '@')
                    continue;
                for (int j = 0; j < (int)m_Asset.States.size(); ++j)
                {
                    if (m_Asset.States[j].Name != to) continue;
                    ed::Link(LinkId(i, t), OutPin(i, t), InPin(j));
                    break;
                }
            }
        }

        NodeCanvas::Edits edits;
        m_Canvas.QueryEdits(edits);

        for (const NodeCanvas::NewLink& l : edits.Created)
        {
            uintptr_t src = l.StartPin, dst = l.EndPin;
            if (IsInPin(src)) std::swap(src, dst);
            if (!(IsOutPin(src) || IsAddPin(src)) || !IsInPin(dst))
                continue;

            const std::string target = (dst == kQuitInPin)
                ? std::string("@quit")
                : m_Asset.States[(int)(dst - 2000000)].Name;

            if (IsAddPin(src))
            {
                const int s = (int)(src - 3000000);
                if (s >= 0 && s < (int)m_Asset.States.size())
                {
                    Snapshot();
                    FlowTransition tr;
                    tr.On = "signal";
                    tr.To = target;
                    m_Asset.States[s].Transitions.push_back(tr);
                    m_SelState = s;
                    m_SelTrans = (int)m_Asset.States[s].Transitions.size() - 1;
                    Revalidate();
                }
            }
            else
            {
                const int s = (int)((src - 4000000) / 512);
                const int t = (int)((src - 4000000) % 512);
                if (s >= 0 && s < (int)m_Asset.States.size() &&
                    t >= 0 && t < (int)m_Asset.States[s].Transitions.size() &&
                    m_Asset.States[s].Transitions[t].To != target)
                {
                    Snapshot();
                    m_Asset.States[s].Transitions[t].To = target;
                    m_SelState = s;
                    m_SelTrans = t;
                    Revalidate();
                }
            }
        }

        if (!edits.DeletedLinks.empty() || !edits.DeletedNodes.empty())
        {
            Snapshot();
            std::vector<std::pair<int, int>> deadTrans;
            for (uintptr_t id : edits.DeletedLinks)
            {
                const int s = (int)((id - 5000000) / 512);
                const int t = (int)((id - 5000000) % 512);
                if (s >= 0 && s < (int)m_Asset.States.size() &&
                    t >= 0 && t < (int)m_Asset.States[s].Transitions.size())
                    deadTrans.push_back({ s, t });
            }
            std::sort(deadTrans.begin(), deadTrans.end(),
                      [](auto& a, auto& b) { return a > b; });
            for (auto& [s, t] : deadTrans)
                m_Asset.States[s].Transitions.erase(m_Asset.States[s].Transitions.begin() + t);

            std::vector<int> deadStates;
            for (uintptr_t id : edits.DeletedNodes)
                if (IsStateNode(id) && StateOfNode(id) < (int)m_Asset.States.size())
                    deadStates.push_back(StateOfNode(id));
            std::sort(deadStates.rbegin(), deadStates.rend());
            for (int s : deadStates)
                m_Asset.States.erase(m_Asset.States.begin() + s);

            m_SelState = m_Asset.States.empty() ? -1
                        : std::min(m_SelState, (int)m_Asset.States.size() - 1);
            m_SelTrans = -1;
            m_PlaceNodes = true;
            Revalidate();
        }

        if (uintptr_t sel = m_Canvas.SelectedNode(); sel != 0 && IsStateNode(sel))
        {
            if (m_SelState != StateOfNode(sel))
            {
                m_SelState = StateOfNode(sel);
                m_SelTrans = -1;
            }
        }
        for (int i = 0; i < (int)m_Asset.States.size(); ++i)
        {
            const ImVec2 p = m_Canvas.GetNodePosition(NodeId(i));
            glm::vec2& stored = m_Asset.States[i].EditorPos;
            if (std::abs(p.x - stored.x) > 0.5f || std::abs(p.y - stored.y) > 0.5f)
            {
                stored = { p.x, p.y };
                m_Dirty = true;
            }
        }

        m_Canvas.End();
        (void)ctx;
    }

    // ========================================================================
    // Inspector
    // ========================================================================

    void FlowEditor::DrawInspector(EditorContext& ctx)
    {
        if (!m_Problems.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "Problems");
            for (const std::string& p : m_Problems)
                ImGui::TextWrapped("- %s", p.c_str());
            ImGui::Separator();
        }

        if (m_SelState < 0 || m_SelState >= (int)m_Asset.States.size())
        {
            ImGui::TextDisabled("Select a state node.");
            return;
        }

        DrawStateInspector(ctx, m_SelState);

        if (m_SelTrans >= 0 && m_SelTrans < (int)m_Asset.States[m_SelState].Transitions.size())
        {
            ImGui::Separator();
            DrawTransitionInspector(ctx, m_SelState, m_SelTrans);
        }
    }

    void FlowEditor::DrawStateInspector(EditorContext& ctx, int stateIdx)
    {
        FlowState& s = m_Asset.States[stateIdx];

        ImGui::TextDisabled("State");

        char name[96];
        std::snprintf(name, sizeof(name), "%s", s.Name.c_str());
        if (ImGui::InputText("Name", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue) &&
            name[0] && s.Name != name && !m_Asset.Find(name))
        {
            Snapshot();
            const std::string oldName = s.Name;
            s.Name = name;
            if (m_Asset.Start == oldName) m_Asset.Start = s.Name;
            for (FlowState& other : m_Asset.States)
                for (FlowTransition& t : other.Transitions)
                    if (t.To == oldName) t.To = s.Name;
            Revalidate();
        }

        const std::vector<std::string> scenes = ListVfs("project://scenes", ".cscene");
        const std::string sceneLabel = s.Scene.empty() ? "(none)" : StemOf(s.Scene);
        if (ImGui::BeginCombo("Scene", sceneLabel.c_str()))
        {
            if (ImGui::Selectable("(none)", s.Scene.empty()) && !s.Scene.empty())
            {
                Snapshot();
                s.Scene.clear();
                Revalidate();
            }
            for (const std::string& sc : scenes)
            {
                const bool sel = (sc == s.Scene);
                if (ImGui::Selectable((StemOf(sc) + "##" + sc).c_str(), sel) && !sel)
                {
                    Snapshot();
                    s.Scene = sc;
                    Revalidate();
                    ScanKnownSignals();
                }
            }
            ImGui::EndCombo();
        }

        bool overlay = s.Overlay;
        if (ImGui::Checkbox("Overlay (keeps the under-scene)", &overlay))
        {
            Snapshot();
            s.Overlay = overlay;
            Revalidate();
        }

        const bool isStart = (m_Asset.Start == s.Name);
        ImGui::BeginDisabled(isStart);
        if (ImGui::Button(isStart ? "Start state" : "Set as Start"))
        {
            Snapshot();
            m_Asset.Start = s.Name;
            Revalidate();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Delete State"))
        {
            Snapshot();
            m_Asset.States.erase(m_Asset.States.begin() + stateIdx);
            m_SelState = m_Asset.States.empty() ? -1
                        : std::min(stateIdx, (int)m_Asset.States.size() - 1);
            m_SelTrans = -1;
            m_PlaceNodes = true;
            Revalidate();
            return;
        }

        ImGui::Separator();
        ImGui::TextDisabled("On Enter");
        int killAction = -1;
        for (int a = 0; a < (int)s.OnEnter.size(); ++a)
        {
            FlowAction& act = s.OnEnter[a];
            ImGui::PushID(a + 100);
            int type = (int)act.ActionType;
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::Combo("##atype", &type, "emit\0setField\0setVar\0"))
            {
                Snapshot();
                act.ActionType = (FlowAction::Type)type;
            }
            ImGui::SameLine();
            if (act.ActionType == FlowAction::Type::Emit)
            {
                char sig[96];
                std::snprintf(sig, sizeof(sig), "%s", act.Signal.c_str());
                ImGui::SetNextItemWidth(-30.0f);
                if (ImGui::InputText("##asig", sig, sizeof(sig),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    Snapshot();
                    act.Signal = sig;
                }
            }
            else if (act.ActionType == FlowAction::Type::SetVar)   // Q2
            {
                char vn[64];
                std::snprintf(vn, sizeof(vn), "%s", act.Var.c_str());
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::InputTextWithHint("##avar", "var", vn, sizeof(vn),
                                             ImGuiInputTextFlags_EnterReturnsTrue))
                { Snapshot(); act.Var = vn; }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("FLOW_VAR"))
                    { Snapshot(); act.Var = std::string(static_cast<const char*>(p->Data)); }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                bool add = act.VarAdd;
                if (ImGui::Checkbox("+=", &add)) { Snapshot(); act.VarAdd = add; }
                ImGui::SameLine();
                double d = act.Value.Number;
                ImGui::SetNextItemWidth(60.0f);
                if (ImGui::InputDouble("##avval", &d, 0, 0, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue))
                { Snapshot(); act.Value = FlowValue::MakeNumber(d); }
            }
            else
            {
                ImGui::TextDisabled("%s.%s.%s", act.Entity.c_str(), act.Component.c_str(),
                                    act.Field.c_str());
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x"))
                killAction = a;
            ImGui::PopID();
        }
        if (killAction >= 0)
        {
            Snapshot();
            s.OnEnter.erase(s.OnEnter.begin() + killAction);
        }
        if (ImGui::SmallButton("+ emit action"))
        {
            Snapshot();
            FlowAction act;
            act.ActionType = FlowAction::Type::Emit;
            act.Signal = "state_entered";
            s.OnEnter.push_back(act);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+ setVar action"))   // Q2
        {
            Snapshot();
            FlowAction act;
            act.ActionType = FlowAction::Type::SetVar;
            act.Var    = m_Asset.Variables.empty() ? std::string("Var") : m_Asset.Variables.front().Name;
            act.VarAdd = true;
            act.Value  = FlowValue::MakeNumber(1.0);
            s.OnEnter.push_back(act);
        }

        ImGui::Separator();
        ImGui::TextDisabled("Transitions");
        for (int t = 0; t < (int)s.Transitions.size(); ++t)
        {
            const FlowTransition& tr = s.Transitions[t];
            const std::string row = "on " + tr.On + " -> " + (tr.To.empty() ? "?" : tr.To);
            if (ImGui::Selectable((row + "##tr" + std::to_string(t)).c_str(), m_SelTrans == t))
                m_SelTrans = t;
        }
        if (ImGui::SmallButton("+ transition"))
        {
            Snapshot();
            FlowTransition tr;
            tr.On = "signal";
            tr.To = m_Asset.Start;
            s.Transitions.push_back(tr);
            m_SelTrans = (int)s.Transitions.size() - 1;
            Revalidate();
        }
        (void)ctx;
    }

    void FlowEditor::DrawTransitionInspector(EditorContext& ctx, int stateIdx, int transIdx)
    {
        FlowState& s = m_Asset.States[stateIdx];
        FlowTransition& tr = s.Transitions[transIdx];

        ImGui::TextDisabled("Transition");

        if (ImGui::BeginCombo("On", tr.On.c_str()))
        {
            for (const std::string& sig : m_KnownSignals)
            {
                const bool sel = (sig == tr.On);
                if (ImGui::Selectable(sig.c_str(), sel) && !sel)
                {
                    Snapshot();
                    tr.On = sig;
                }
            }
            ImGui::EndCombo();
        }
        char on[96];
        std::snprintf(on, sizeof(on), "%s", tr.On.c_str());
        if (ImGui::InputText("On (custom)", on, sizeof(on), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            Snapshot();
            tr.On = on;
        }
        ImGui::TextDisabled("signals, key:<Name>, or timer:<seconds>");

        if (ImGui::BeginCombo("To", tr.To.c_str()))
        {
            for (const FlowState& target : m_Asset.States)
            {
                const bool sel = (target.Name == tr.To);
                if (ImGui::Selectable(target.Name.c_str(), sel) && !sel)
                {
                    Snapshot();
                    tr.To = target.Name;
                    Revalidate();
                }
            }
            if (ImGui::Selectable("@quit", tr.To == "@quit") && tr.To != "@quit")
            {
                Snapshot();
                tr.To = "@quit";
                Revalidate();
            }
            if (ImGui::Selectable("@pop", tr.To == "@pop") && tr.To != "@pop")
            {
                Snapshot();
                tr.To = "@pop";
                Revalidate();
            }
            ImGui::EndCombo();
        }

        bool push = tr.Push;
        if (ImGui::Checkbox("Push (overlay onto the stack)", &push))
        {
            Snapshot();
            tr.Push = push;
        }

        bool hasGuard = tr.HasGuard;
        if (ImGui::Checkbox("Guard (if)", &hasGuard))
        {
            Snapshot();
            tr.HasGuard = hasGuard;
        }
        if (tr.HasGuard)
        {
            DrawFlowGuardFields("trguard", tr.Guard, [this]() { Snapshot(); });
        }

        if (ImGui::Button("Delete Transition"))
        {
            Snapshot();
            s.Transitions.erase(s.Transitions.begin() + transIdx);
            m_SelTrans = -1;
            Revalidate();
        }
        (void)ctx;
    }

    // Q2 — the typed-variables blackboard (shared widget; snapshots for undo).
    void FlowEditor::DrawVariablesPanel(EditorContext& /*ctx*/)
    {
        DrawFlowVariablesPanel("flowvars", m_Asset.Variables, [this]() { Snapshot(); });
    }
}
