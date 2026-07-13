// widgets/VariablesPanel.cpp — see header. Shared blackboard editor (Q2/Q4).

#include "widgets/VariablesPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string>

using namespace Cosmic;

namespace Starforge
{
    void DrawFlowVariablesPanel(const char* id, std::vector<FlowVariable>& vars,
                                const std::function<void()>& beforeEdit)
    {
        auto snap = [&]() { if (beforeEdit) beforeEdit(); };

        ImGui::PushID(id);
        ImGui::TextDisabled("Variables");
        ImGui::SameLine();
        if (ImGui::SmallButton("+ Var"))
        {
            snap();
            FlowVariable v;
            v.Name    = "Var" + std::to_string(vars.size() + 1);
            v.Default = FlowValue::MakeNumber(0.0);
            vars.push_back(std::move(v));
        }
        ImGui::TextDisabled("drag a name onto a guard");
        ImGui::Separator();

        int kill = -1;
        for (int i = 0; i < (int)vars.size(); ++i)
        {
            FlowVariable& v = vars[i];
            ImGui::PushID(i);

            char nm[64];
            std::snprintf(nm, sizeof(nm), "%s", v.Name.c_str());
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##vname", nm, sizeof(nm), ImGuiInputTextFlags_EnterReturnsTrue))
            { snap(); v.Name = nm; }
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("FLOW_VAR", v.Name.c_str(), v.Name.size() + 1);
                ImGui::Text("var: %s", v.Name.c_str());
                ImGui::EndDragDropSource();
            }

            char gp[64];
            std::snprintf(gp, sizeof(gp), "%s", v.Group.c_str());
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputTextWithHint("##vgroup", "group (optional)", gp, sizeof(gp),
                                         ImGuiInputTextFlags_EnterReturnsTrue))
            { snap(); v.Group = gp; }

            int kind = (int)v.Default.ValueKind;
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::Combo("##vtype", &kind, "bool\0number\0string\0enum\0"))
            { snap(); v.Default.ValueKind = (FlowValue::Kind)kind; }
            ImGui::SameLine();

            switch (v.Default.ValueKind)
            {
            case FlowValue::Kind::Bool:
            {
                bool b = v.Default.Bool;
                if (ImGui::Checkbox("default##vb", &b)) { snap(); v.Default.Bool = b; }
                break;
            }
            case FlowValue::Kind::Number:
            {
                double d = v.Default.Number;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputDouble("##vn", &d, 0, 0, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                { snap(); v.Default.Number = d; }
                break;
            }
            case FlowValue::Kind::String:
            {
                char sv[96];
                std::snprintf(sv, sizeof(sv), "%s", v.Default.String.c_str());
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputTextWithHint("##vs", "default", sv, sizeof(sv),
                                             ImGuiInputTextFlags_EnterReturnsTrue))
                { snap(); v.Default.String = sv; }
                break;
            }
            case FlowValue::Kind::Enum:
            {
                std::string joined;
                for (size_t k = 0; k < v.EnumOptions.size(); ++k)
                    joined += (k ? "," : "") + v.EnumOptions[k];
                char ob[192];
                std::snprintf(ob, sizeof(ob), "%s", joined.c_str());
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputTextWithHint("##veopts", "opt1,opt2,…", ob, sizeof(ob),
                                             ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    snap();
                    v.EnumOptions.clear();
                    std::string tok;
                    std::stringstream ss(ob);
                    while (std::getline(ss, tok, ','))
                        if (!tok.empty()) v.EnumOptions.push_back(tok);
                    if (!v.EnumOptions.empty() &&
                        std::find(v.EnumOptions.begin(), v.EnumOptions.end(), v.Default.String) == v.EnumOptions.end())
                        v.Default.String = v.EnumOptions.front();
                }
                if (ImGui::BeginCombo("##vedef", v.Default.String.c_str()))
                {
                    for (const std::string& opt : v.EnumOptions)
                        if (ImGui::Selectable(opt.c_str(), opt == v.Default.String))
                        { snap(); v.Default.String = opt; }
                    ImGui::EndCombo();
                }
                break;
            }
            }

            if (ImGui::SmallButton("Delete##v")) kill = i;
            ImGui::Separator();
            ImGui::PopID();
        }
        if (kill >= 0)
        {
            snap();
            vars.erase(vars.begin() + kill);
        }
        ImGui::PopID();
    }

    void DrawFlowGuardFields(const char* id, FlowGuard& g, const std::function<void()>& beforeEdit)
    {
        static const char* kOps[] = { "==", "!=", "<", ">", "<=", ">=" };
        auto snap = [&]() { if (beforeEdit) beforeEdit(); };

        ImGui::PushID(id);
        char buf[96];

        // Source: reflected Field (v1) or a flow Variable (Q2).
        int src = g.Var.empty() ? 0 : 1;
        if (ImGui::Combo("Compare", &src, "Field\0Variable\0"))
        {
            snap();
            if (src == 1 && g.Var.empty()) g.Var = "Var";
            if (src == 0) g.Var.clear();
        }

        if (!g.Var.empty())
        {
            std::snprintf(buf, sizeof(buf), "%s", g.Var.c_str());
            if (ImGui::InputText("Variable", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
            { snap(); g.Var = buf; }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("FLOW_VAR"))
                { snap(); g.Var = std::string(static_cast<const char*>(p->Data)); }
                ImGui::EndDragDropTarget();
            }
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "%s", g.Entity.c_str());
            if (ImGui::InputText("Entity (Tag)", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
            { snap(); g.Entity = buf; }
            std::snprintf(buf, sizeof(buf), "%s", g.Component.c_str());
            if (ImGui::InputText("Component", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
            { snap(); g.Component = buf; }
            std::snprintf(buf, sizeof(buf), "%s", g.Field.c_str());
            if (ImGui::InputText("Field", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
            { snap(); g.Field = buf; }
        }

        int op = 0;
        for (int i = 0; i < 6; ++i)
            if (g.Op == kOps[i]) { op = i; break; }
        if (ImGui::Combo("Op", &op, "==\0!=\0<\0>\0<=\0>=\0"))
        { snap(); g.Op = kOps[op]; }

        int kind = (int)g.Value.ValueKind;
        if (ImGui::Combo("Value type", &kind, "bool\0number\0string\0enum\0"))
        { snap(); g.Value.ValueKind = (FlowValue::Kind)kind; }
        switch (g.Value.ValueKind)
        {
        case FlowValue::Kind::Bool:
        {
            bool b = g.Value.Bool;
            if (ImGui::Checkbox("Value", &b)) { snap(); g.Value.Bool = b; }
            break;
        }
        case FlowValue::Kind::Number:
        {
            double d = g.Value.Number;
            if (ImGui::InputDouble("Value", &d, 0, 0, "%.4f", ImGuiInputTextFlags_EnterReturnsTrue))
            { snap(); g.Value.Number = d; }
            break;
        }
        case FlowValue::Kind::String:
        case FlowValue::Kind::Enum:
        {
            std::snprintf(buf, sizeof(buf), "%s", g.Value.String.c_str());
            if (ImGui::InputText("Value", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
            { snap(); g.Value.String = buf; }
            break;
        }
        }
        ImGui::PopID();
    }
}
