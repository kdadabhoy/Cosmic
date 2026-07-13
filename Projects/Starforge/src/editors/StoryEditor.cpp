// editors/StoryEditor.cpp — see StoryEditor.h. Starforge Story Graph editor (Q4).

#include "editors/StoryEditor.h"
#include "EditorContext.h"
#include "widgets/VariablesPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace Cosmic;

namespace Starforge
{
    namespace
    {
        std::string StemOf(const std::string& path)
        {
            return path.empty() ? std::string() : fs::path(path).stem().string();
        }

        std::string Truncate(const std::string& s, size_t n)
        {
            if (s.size() <= n) return s;
            return s.substr(0, n) + "…";
        }
    }

    // ========================================================================
    // Document lifecycle
    // ========================================================================

    StoryEditor::StoryEditor(std::string vfsPath)
        : m_Path(std::move(vfsPath))
    {
        std::string err;
        if (StoryGraph::Load(m_Asset, m_Path, &err))
        {
            m_Loaded   = true;
            m_SelNode  = m_Asset.Nodes.empty() ? -1 : 0;
            m_PlaceNodes = true;
            Revalidate();
        }
    }

    std::string StoryEditor::Title() const
    {
        std::string t = StemOf(m_Path);
        return t.empty() ? std::string("Story") : t;
    }

    bool StoryEditor::SaveStory(EditorContext& ctx)
    {
        if (!m_Loaded || m_Path.empty())
            return false;
        Revalidate();
        if (!m_Asset.Save(m_Path))
        {
            ctx.Log("[Story] Save FAILED for " + m_Path, LogSeverity::Error);
            return false;
        }
        m_Dirty = false;
        ctx.Log("[Story] Saved " + m_Path +
                (m_Problems.empty() ? "" : (" (" + std::to_string(m_Problems.size()) + " warning(s))")));
        return true;
    }

    void StoryEditor::Revalidate()
    {
        m_Problems = m_Asset.Validate();
    }

    void StoryEditor::Snapshot()
    {
        m_UndoStack.push_back(m_Asset.SaveToString());
        if (m_UndoStack.size() > 64)
            m_UndoStack.erase(m_UndoStack.begin());
        m_RedoStack.clear();
        m_Dirty = true;
    }

    void StoryEditor::ApplySnapshot(const std::string& json)
    {
        StoryGraph g;
        if (!StoryGraph::LoadFromString(g, json))
            return;
        m_Asset = std::move(g);
        m_SelNode = std::min(m_SelNode, (int)m_Asset.Nodes.size() - 1);
        m_Dirty = true;
        m_PlaceNodes = true;
        Revalidate();
    }

    // ========================================================================
    // Body
    // ========================================================================

    void StoryEditor::OnImGuiRender(EditorContext& ctx)
    {
        if (!m_Loaded)
        {
            ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.4f, 1.0f),
                               "Could not open story: %s", m_Path.c_str());
            return;
        }

        DrawToolbar(ctx);
        ImGui::Separator();

        const float inspectorW = 340.0f;
        const float varsW = m_ShowVars ? 240.0f : 0.0f;
        ImGui::BeginChild("story_canvas_region",
                          ImVec2(ImGui::GetContentRegionAvail().x - inspectorW - varsW - 8.0f, 0.0f),
                          ImGuiChildFlags_None);
        DrawCanvas(ctx);
        ImGui::EndChild();

        if (m_ShowVars)
        {
            ImGui::SameLine();
            ImGui::BeginChild("story_vars_region", ImVec2(varsW, 0.0f), ImGuiChildFlags_Borders);
            DrawVariablesPanel(ctx);
            ImGui::EndChild();
        }

        ImGui::SameLine();
        ImGui::BeginChild("story_inspector_region", ImVec2(inspectorW, 0.0f), ImGuiChildFlags_Borders);
        if (m_Preview) DrawPreview(ctx);
        else           DrawNodeInspector(ctx);
        ImGui::EndChild();
    }

    void StoryEditor::DrawToolbar(EditorContext& ctx)
    {
        if (ImGui::Button("Save")) SaveStory(ctx);

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
        if (ImGui::Button("+ Node"))
            ImGui::OpenPopup("story_new_node");
        if (ImGui::BeginPopup("story_new_node"))
        {
            ImGui::TextUnformatted("Node name");
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputText("##nnname", m_NewNodeName, sizeof(m_NewNodeName));
            const bool taken = m_Asset.Find(m_NewNodeName) != nullptr;
            if (taken) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "name in use");
            ImGui::BeginDisabled(!m_NewNodeName[0] || taken);
            if (ImGui::Button("Add##node"))
            {
                Snapshot();
                StoryNode n;
                n.Name = m_NewNodeName;
                n.EditorPos = { 60.0f + 40.0f * (float)(m_Asset.Nodes.size() % 8),
                                60.0f + 40.0f * (float)(m_Asset.Nodes.size() % 8) };
                if (m_Asset.Start.empty()) m_Asset.Start = n.Name;
                m_Asset.Nodes.push_back(std::move(n));
                m_SelNode = (int)m_Asset.Nodes.size() - 1;
                m_PlaceNodes = true;
                Revalidate();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        ImGui::Checkbox("Variables", &m_ShowVars);

        ImGui::SameLine();
        if (ImGui::Checkbox("Play", &m_Preview))
        {
            if (m_Preview)
            {
                m_PreviewScene = Scene::Create();
                m_Runner.Start(m_Asset, m_PreviewScene.get());
            }
            else
            {
                m_Runner.Stop();
                m_PreviewScene = nullptr;
            }
        }

        ImGui::SameLine();
        if (m_Problems.empty())
            ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1.0f), "valid");
        else
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%d problem(s)", (int)m_Problems.size());
    }

    // ========================================================================
    // Canvas
    // ========================================================================

    void StoryEditor::DrawCanvas(EditorContext& ctx)
    {
        m_Canvas.Begin("story_graph");

        if (m_PlaceNodes)
        {
            bool allZero = !m_Asset.Nodes.empty();
            for (const StoryNode& n : m_Asset.Nodes)
                if (n.EditorPos.x != 0.0f || n.EditorPos.y != 0.0f) { allZero = false; break; }
            for (int i = 0; i < (int)m_Asset.Nodes.size(); ++i)
            {
                const glm::vec2 p = allZero
                    ? glm::vec2(40.0f + 320.0f * (float)(i % 4), 40.0f + 220.0f * (float)(i / 4))
                    : m_Asset.Nodes[i].EditorPos;
                m_Canvas.SetNodePosition(NodeId(i), ImVec2(p.x, p.y));
            }
            float maxX = 0.0f;
            for (const StoryNode& n : m_Asset.Nodes) maxX = std::max(maxX, n.EditorPos.x);
            m_Canvas.SetNodePosition(kEndNode, ImVec2(maxX + 360.0f, 40.0f));
            m_PlaceNodes = false;
        }

        const ImVec4 dim(0.62f, 0.66f, 0.72f, 1.0f);
        const ImVec4 badge(0.55f, 0.75f, 1.0f, 1.0f);

        for (int i = 0; i < (int)m_Asset.Nodes.size(); ++i)
        {
            StoryNode& n = m_Asset.Nodes[i];
            ed::BeginNode(NodeId(i));
            ImGui::PushID((int)NodeId(i));

            ed::BeginPin(InPin(i), ed::PinKind::Input);
            ImGui::TextUnformatted("->");
            ed::EndPin();
            ImGui::SameLine();
            if (n.Name == m_Asset.Start)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.20f, 1.0f), "[start]");
                ImGui::SameLine();
            }
            ImGui::TextUnformatted(n.Name.c_str());
            if (!n.Speaker.empty())
                ImGui::TextColored(dim, "%s", n.Speaker.c_str());
            if (!n.Text.empty())
                ImGui::TextDisabled("\"%s\"", Truncate(n.Text, 26).c_str());

            for (int o = 0; o < (int)n.Options.size(); ++o)
            {
                const StoryOption& opt = n.Options[o];
                std::string label = "» " + (opt.Text.empty() ? std::string("(option)") : Truncate(opt.Text, 18));
                ImGui::TextUnformatted(label.c_str());
                if (opt.HasGuard) { ImGui::SameLine(); ImGui::TextColored(badge, "[if]"); }
                if (opt.Once)     { ImGui::SameLine(); ImGui::TextColored(dim, "[once]"); }
                ImGui::SameLine();
                ed::BeginPin(OptPin(i, o), ed::PinKind::Output);
                ImGui::TextUnformatted("->");
                ed::EndPin();
            }

            ed::BeginPin(AddPin(i), ed::PinKind::Output);
            ImGui::TextDisabled("+ option");
            ed::EndPin();

            ImGui::PopID();
            ed::EndNode();
        }

        {
            ed::BeginNode(kEndNode);
            ed::BeginPin(kEndInPin, ed::PinKind::Input);
            ImGui::TextUnformatted("->");
            ed::EndPin();
            ImGui::SameLine();
            ImGui::TextUnformatted("@end");
            ImGui::TextColored(dim, "the story ends");
            ed::EndNode();
        }

        for (int i = 0; i < (int)m_Asset.Nodes.size(); ++i)
        {
            const StoryNode& n = m_Asset.Nodes[i];
            for (int o = 0; o < (int)n.Options.size(); ++o)
            {
                const std::string& to = n.Options[o].Next;
                if (to == "@end")
                {
                    ed::Link(LinkId(i, o), OptPin(i, o), kEndInPin, ImVec4(0.9f, 0.5f, 0.4f, 1.0f));
                    continue;
                }
                if (to.empty()) continue;
                for (int j = 0; j < (int)m_Asset.Nodes.size(); ++j)
                    if (m_Asset.Nodes[j].Name == to)
                    {
                        ed::Link(LinkId(i, o), OptPin(i, o), InPin(j));
                        break;
                    }
            }
        }

        // Interactions.
        NodeCanvas::Edits edits;
        m_Canvas.QueryEdits(edits);

        for (const NodeCanvas::NewLink& l : edits.Created)
        {
            uintptr_t src = l.StartPin, dst = l.EndPin;
            if (IsInPin(src)) std::swap(src, dst);
            if (!(IsOptPin(src) || IsAddPin(src)) || !IsInPin(dst))
                continue;

            const std::string target = (dst == kEndInPin)
                ? std::string("@end")
                : m_Asset.Nodes[(int)(dst - 2000000)].Name;

            if (IsAddPin(src))
            {
                const int nn = (int)(src - 3000000);
                if (nn >= 0 && nn < (int)m_Asset.Nodes.size())
                {
                    Snapshot();
                    StoryOption opt;
                    opt.Text = "Continue";
                    opt.Next = target;
                    m_Asset.Nodes[nn].Options.push_back(opt);
                    m_SelNode = nn;
                    Revalidate();
                }
            }
            else
            {
                const int nn = (int)((src - 4000000) / 512);
                const int oo = (int)((src - 4000000) % 512);
                if (nn >= 0 && nn < (int)m_Asset.Nodes.size() &&
                    oo >= 0 && oo < (int)m_Asset.Nodes[nn].Options.size() &&
                    m_Asset.Nodes[nn].Options[oo].Next != target)
                {
                    Snapshot();
                    m_Asset.Nodes[nn].Options[oo].Next = target;
                    m_SelNode = nn;
                    Revalidate();
                }
            }
        }

        if (!edits.DeletedLinks.empty() || !edits.DeletedNodes.empty())
        {
            Snapshot();
            for (uintptr_t id : edits.DeletedLinks)
            {
                const int nn = (int)((id - 5000000) / 512);
                const int oo = (int)((id - 5000000) % 512);
                if (nn >= 0 && nn < (int)m_Asset.Nodes.size() &&
                    oo >= 0 && oo < (int)m_Asset.Nodes[nn].Options.size())
                    m_Asset.Nodes[nn].Options[oo].Next.clear();   // dead-end (ends the story)
            }

            std::vector<int> deadNodes;
            for (uintptr_t id : edits.DeletedNodes)
                if (IsNodeNode(id) && NodeOf(id) < (int)m_Asset.Nodes.size())
                    deadNodes.push_back(NodeOf(id));
            std::sort(deadNodes.rbegin(), deadNodes.rend());
            for (int nn : deadNodes)
                m_Asset.Nodes.erase(m_Asset.Nodes.begin() + nn);

            m_SelNode = m_Asset.Nodes.empty() ? -1 : std::min(m_SelNode, (int)m_Asset.Nodes.size() - 1);
            m_PlaceNodes = true;
            Revalidate();
        }

        if (uintptr_t sel = m_Canvas.SelectedNode(); sel != 0 && IsNodeNode(sel))
            m_SelNode = NodeOf(sel);

        for (int i = 0; i < (int)m_Asset.Nodes.size(); ++i)
        {
            const ImVec2 p = m_Canvas.GetNodePosition(NodeId(i));
            glm::vec2& stored = m_Asset.Nodes[i].EditorPos;
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
    // Edit-node panel
    // ========================================================================

    void StoryEditor::DrawNodeInspector(EditorContext& /*ctx*/)
    {
        if (!m_Problems.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "Problems");
            for (const std::string& p : m_Problems)
                ImGui::TextWrapped("- %s", p.c_str());
            ImGui::Separator();
        }

        if (m_SelNode < 0 || m_SelNode >= (int)m_Asset.Nodes.size())
        {
            ImGui::TextDisabled("Select a node.");
            return;
        }

        StoryNode& n = m_Asset.Nodes[m_SelNode];

        char name[96];
        std::snprintf(name, sizeof(name), "%s", n.Name.c_str());
        if (ImGui::InputText("Name", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue) &&
            name[0] && n.Name != name && !m_Asset.Find(name))
        {
            Snapshot();
            const std::string oldName = n.Name;
            n.Name = name;
            if (m_Asset.Start == oldName) m_Asset.Start = n.Name;
            for (StoryNode& other : m_Asset.Nodes)
                for (StoryOption& o : other.Options)
                    if (o.Next == oldName) o.Next = n.Name;
            Revalidate();
        }

        char speaker[96];
        std::snprintf(speaker, sizeof(speaker), "%s", n.Speaker.c_str());
        if (ImGui::InputText("Speaker", speaker, sizeof(speaker), ImGuiInputTextFlags_EnterReturnsTrue))
        { Snapshot(); n.Speaker = speaker; }

        char text[1024];
        std::snprintf(text, sizeof(text), "%s", n.Text.c_str());
        if (ImGui::InputTextMultiline("Text", text, sizeof(text), ImVec2(-1.0f, 70.0f)))
        { if (!m_Dirty) Snapshot(); n.Text = text; }

        // Asset slots (portrait / background / audio) — text + ASSET_PATH drop.
        auto assetInput = [&](const char* label, std::string& path)
        {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s", path.c_str());
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputTextWithHint(label, "project://… (drop)", buf, sizeof(buf),
                                         ImGuiInputTextFlags_EnterReturnsTrue))
            { Snapshot(); path = buf; }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                { Snapshot(); path = std::string(static_cast<const char*>(p->Data)); }
                ImGui::EndDragDropTarget();
            }
        };
        ImGui::TextDisabled("Portrait");    assetInput("##portrait", n.PortraitPath);
        ImGui::TextDisabled("Background");  assetInput("##background", n.BackgroundPath);
        ImGui::TextDisabled("Audio");       assetInput("##audio", n.AudioPath);

        const bool isStart = (m_Asset.Start == n.Name);
        ImGui::BeginDisabled(isStart);
        if (ImGui::Button(isStart ? "Start node" : "Set as Start"))
        { Snapshot(); m_Asset.Start = n.Name; Revalidate(); }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Delete Node"))
        {
            Snapshot();
            m_Asset.Nodes.erase(m_Asset.Nodes.begin() + m_SelNode);
            m_SelNode = m_Asset.Nodes.empty() ? -1 : std::min(m_SelNode, (int)m_Asset.Nodes.size() - 1);
            m_PlaceNodes = true;
            Revalidate();
            return;
        }

        // Signals.
        auto signalList = [&](const char* title, std::vector<std::string>& sigs)
        {
            ImGui::TextDisabled("%s", title);
            int kill = -1;
            for (int i = 0; i < (int)sigs.size(); ++i)
            {
                ImGui::PushID(title); ImGui::PushID(i);
                char sb[96];
                std::snprintf(sb, sizeof(sb), "%s", sigs[i].c_str());
                ImGui::SetNextItemWidth(-30.0f);
                if (ImGui::InputText("##sig", sb, sizeof(sb), ImGuiInputTextFlags_EnterReturnsTrue))
                { Snapshot(); sigs[i] = sb; }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) kill = i;
                ImGui::PopID(); ImGui::PopID();
            }
            if (kill >= 0) { Snapshot(); sigs.erase(sigs.begin() + kill); }
            ImGui::PushID(title);
            if (ImGui::SmallButton("+ signal")) { Snapshot(); sigs.push_back("signal"); }
            ImGui::PopID();
        };
        ImGui::Separator();
        signalList("On Enter (emit)", n.OnEnter);
        signalList("On Exit (emit)",  n.OnExit);

        // Options.
        ImGui::Separator();
        ImGui::TextDisabled("Options");
        int killOption = -1;
        for (int o = 0; o < (int)n.Options.size(); ++o)
        {
            StoryOption& opt = n.Options[o];
            ImGui::PushID(o + 500);
            ImGui::Separator();

            char ob[256];
            std::snprintf(ob, sizeof(ob), "%s", opt.Text.c_str());
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputTextWithHint("##otext", "choice text", ob, sizeof(ob),
                                         ImGuiInputTextFlags_EnterReturnsTrue))
            { Snapshot(); opt.Text = ob; }

            // Next target combo.
            const std::string nextLabel = opt.Next.empty() ? "(end)" : opt.Next;
            if (ImGui::BeginCombo("Next", nextLabel.c_str()))
            {
                if (ImGui::Selectable("(end)", opt.Next.empty()) && !opt.Next.empty())
                { Snapshot(); opt.Next.clear(); Revalidate(); }
                if (ImGui::Selectable("@end", opt.Next == "@end") && opt.Next != "@end")
                { Snapshot(); opt.Next = "@end"; Revalidate(); }
                for (const StoryNode& target : m_Asset.Nodes)
                {
                    const bool sel = target.Name == opt.Next;
                    if (ImGui::Selectable((target.Name + "##nx").c_str(), sel) && !sel)
                    { Snapshot(); opt.Next = target.Name; Revalidate(); }
                }
                ImGui::EndCombo();
            }

            bool once = opt.Once;
            if (ImGui::Checkbox("Once", &once)) { Snapshot(); opt.Once = once; }
            ImGui::SameLine();
            bool hasGuard = opt.HasGuard;
            if (ImGui::Checkbox("Condition (if)", &hasGuard)) { Snapshot(); opt.HasGuard = hasGuard; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete##opt")) killOption = o;

            if (opt.HasGuard)
                DrawFlowGuardFields("optguard", opt.Guard, [this]() { Snapshot(); });

            ImGui::PopID();
        }
        if (killOption >= 0) { Snapshot(); n.Options.erase(n.Options.begin() + killOption); Revalidate(); }
        if (ImGui::SmallButton("+ option"))
        {
            Snapshot();
            StoryOption opt;
            opt.Text = "Continue";
            opt.Next = "@end";
            n.Options.push_back(opt);
        }
    }

    void StoryEditor::DrawVariablesPanel(EditorContext& /*ctx*/)
    {
        DrawFlowVariablesPanel("storyvars", m_Asset.Variables, [this]() { Snapshot(); });
    }

    // ========================================================================
    // Play preview (runs the Q3 runner in-panel)
    // ========================================================================

    void StoryEditor::DrawPreview(EditorContext& /*ctx*/)
    {
        ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.5f, 1.0f), "Preview");
        ImGui::SameLine();
        if (ImGui::SmallButton("Restart"))
        {
            m_PreviewScene = Scene::Create();
            m_Runner.Start(m_Asset, m_PreviewScene.get());
        }
        ImGui::Separator();

        const StoryNode* node = m_Runner.Current();
        if (m_Runner.IsEnded() || !node)
        {
            ImGui::TextDisabled("— the story has ended —");
            return;
        }

        if (!node->Speaker.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", node->Speaker.c_str());
        ImGui::TextWrapped("%s", node->Text.c_str());
        ImGui::Spacing();

        const std::vector<int>& valid = m_Runner.ValidOptions();
        for (int i = 0; i < (int)valid.size(); ++i)
        {
            const StoryOption& opt = node->Options[(size_t)valid[i]];
            const std::string label = (opt.Text.empty() ? std::string("(option)") : opt.Text)
                                    + "##pvopt" + std::to_string(i);
            if (ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f)))
                m_Runner.Choose(i);
        }
        if (valid.empty())
            ImGui::TextDisabled("(no available options — a dead end)");
    }
}
