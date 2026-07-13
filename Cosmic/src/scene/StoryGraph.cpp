// scene/StoryGraph.cpp — Starforge Story Graph runtime + `.cstory` serdes (Q3).
// See StoryGraph.h. GL-free; the FlowMachine pattern.

#include "scene/StoryGraph.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/EventBus.h"
#include "utils/FileSystem.h"
#include "core/Log.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace Cosmic
{
    using nlohmann::json;

    namespace
    {
        FlowValue ParseValue(const json& j)
        {
            if (j.is_boolean()) return FlowValue::MakeBool(j.get<bool>());
            if (j.is_number())  return FlowValue::MakeNumber(j.get<double>());
            if (j.is_string())  return FlowValue::MakeString(j.get<std::string>());
            return FlowValue::MakeBool(false);
        }

        json ValueToJson(const FlowValue& v)
        {
            switch (v.ValueKind)
            {
                case FlowValue::Kind::Bool:   return json(v.Bool);
                case FlowValue::Kind::Number: return json(v.Number);
                case FlowValue::Kind::String: return json(v.String);
                case FlowValue::Kind::Enum:   return json(v.String);
            }
            return json(false);
        }

        // Variables blackboard serdes — shared shape with `.cflow` (Q2).
        void ParseVariables(const json& j, std::vector<FlowVariable>& out)
        {
            if (!j.contains("variables") || !j["variables"].is_array()) return;
            for (const auto& jv : j["variables"])
            {
                FlowVariable var;
                var.Name  = jv.value("name", std::string());
                var.Group = jv.value("group", std::string());
                if (var.Name.empty()) continue;
                if (jv.contains("options") && jv["options"].is_array())
                    for (const auto& o : jv["options"])
                        if (o.is_string()) var.EnumOptions.push_back(o.get<std::string>());
                const std::string type = jv.value("type", std::string("bool"));
                const json jd = jv.contains("default") ? jv["default"] : json();
                if (type == "number")      var.Default = FlowValue::MakeNumber(jd.is_number() ? jd.get<double>() : 0.0);
                else if (type == "string") var.Default = FlowValue::MakeString(jd.is_string() ? jd.get<std::string>() : std::string());
                else if (type == "enum")   var.Default = FlowValue::MakeEnum(jd.is_string() ? jd.get<std::string>()
                                                : (var.EnumOptions.empty() ? std::string() : var.EnumOptions.front()));
                else                       var.Default = FlowValue::MakeBool(jd.is_boolean() ? jd.get<bool>() : false);
                out.push_back(std::move(var));
            }
        }

        json VariablesToJson(const std::vector<FlowVariable>& vars)
        {
            json jvars = json::array();
            for (const FlowVariable& var : vars)
            {
                json jv;
                jv["name"] = var.Name;
                if (!var.Group.empty()) jv["group"] = var.Group;
                switch (var.Default.ValueKind)
                {
                    case FlowValue::Kind::Number: jv["type"] = "number"; break;
                    case FlowValue::Kind::String: jv["type"] = "string"; break;
                    case FlowValue::Kind::Enum:   jv["type"] = "enum";   break;
                    default:                      jv["type"] = "bool";   break;
                }
                jv["default"] = ValueToJson(var.Default);
                if (var.Default.ValueKind == FlowValue::Kind::Enum && !var.EnumOptions.empty())
                    jv["options"] = var.EnumOptions;
                jvars.push_back(std::move(jv));
            }
            return jvars;
        }

        void ParseGuard(const json& g, FlowGuard& guard)
        {
            guard.Var       = g.value("var", std::string());
            guard.Entity    = g.value("entity", std::string());
            guard.Component = g.value("component", std::string());
            guard.Field     = g.value("field", std::string());
            guard.Op        = g.value("op", std::string("=="));
            if (g.contains("value")) guard.Value = ParseValue(g["value"]);
        }

        json GuardToJson(const FlowGuard& g)
        {
            if (!g.Var.empty())
                return json{ { "var", g.Var }, { "op", g.Op }, { "value", ValueToJson(g.Value) } };
            return json{ { "entity", g.Entity }, { "component", g.Component },
                         { "field", g.Field }, { "op", g.Op }, { "value", ValueToJson(g.Value) } };
        }
    }

    // ========================================================================
    // StoryGraph
    // ========================================================================

    const StoryNode* StoryGraph::Find(const std::string& name) const
    {
        for (const auto& n : Nodes)
            if (n.Name == name) return &n;
        return nullptr;
    }

    bool StoryGraph::LoadFromString(StoryGraph& out, const std::string& jsonText, std::string* error)
    {
        out = StoryGraph{};
        json j;
        try { j = json::parse(jsonText); }
        catch (const std::exception& e)
        {
            if (error) *error = std::string("parse error: ") + e.what();
            return false;
        }

        out.Version = j.value("cosmic_story", 1);
        out.Start   = j.value("start", std::string());
        ParseVariables(j, out.Variables);

        if (j.contains("nodes") && j["nodes"].is_array())
        {
            for (const auto& jn : j["nodes"])
            {
                StoryNode n;
                n.Name           = jn.value("name", std::string());
                n.Speaker        = jn.value("speaker", std::string());
                n.Text           = jn.value("text", std::string());
                n.PortraitPath   = jn.value("portrait", std::string());
                n.BackgroundPath = jn.value("background", std::string());
                n.AudioPath      = jn.value("audio", std::string());

                auto readSignals = [&](const char* key, std::vector<std::string>& dst)
                {
                    if (jn.contains(key) && jn[key].is_array())
                        for (const auto& s : jn[key]) if (s.is_string()) dst.push_back(s.get<std::string>());
                };
                readSignals("onEnter", n.OnEnter);
                readSignals("onExit",  n.OnExit);

                if (jn.contains("options") && jn["options"].is_array())
                {
                    for (const auto& jo : jn["options"])
                    {
                        StoryOption o;
                        o.Text = jo.value("text", std::string());
                        o.Next = jo.value("next", std::string());
                        o.Once = jo.value("once", false);
                        if (jo.contains("if") && jo["if"].is_object())
                        {
                            o.HasGuard = true;
                            ParseGuard(jo["if"], o.Guard);
                        }
                        n.Options.push_back(std::move(o));
                    }
                }

                if (jn.contains("editor") && jn["editor"].is_object() &&
                    jn["editor"].contains("pos") && jn["editor"]["pos"].is_array() &&
                    jn["editor"]["pos"].size() == 2)
                {
                    n.EditorPos.x = jn["editor"]["pos"][0].get<float>();
                    n.EditorPos.y = jn["editor"]["pos"][1].get<float>();
                }

                out.Nodes.push_back(std::move(n));
            }
        }
        return true;
    }

    std::string StoryGraph::SaveToString() const
    {
        json j;
        j["cosmic_story"] = Version;
        j["start"]        = Start;

        json jnodes = json::array();
        for (const StoryNode& n : Nodes)
        {
            json jn;
            jn["name"] = n.Name;
            if (!n.Speaker.empty())        jn["speaker"]    = n.Speaker;
            if (!n.Text.empty())           jn["text"]       = n.Text;
            if (!n.PortraitPath.empty())   jn["portrait"]   = n.PortraitPath;
            if (!n.BackgroundPath.empty()) jn["background"] = n.BackgroundPath;
            if (!n.AudioPath.empty())      jn["audio"]      = n.AudioPath;
            if (!n.OnEnter.empty())        jn["onEnter"]    = n.OnEnter;
            if (!n.OnExit.empty())         jn["onExit"]     = n.OnExit;

            json jopts = json::array();
            for (const StoryOption& o : n.Options)
            {
                json jo;
                jo["text"] = o.Text;
                jo["next"] = o.Next;
                if (o.Once)     jo["once"] = true;
                if (o.HasGuard) jo["if"]   = GuardToJson(o.Guard);
                jopts.push_back(std::move(jo));
            }
            jn["options"] = std::move(jopts);
            jn["editor"]  = { { "pos", { n.EditorPos.x, n.EditorPos.y } } };
            jnodes.push_back(std::move(jn));
        }
        j["nodes"] = std::move(jnodes);

        if (!Variables.empty())
            j["variables"] = VariablesToJson(Variables);

        return j.dump(2);
    }

    bool StoryGraph::Load(StoryGraph& out, const std::string& path, std::string* error)
    {
        const std::string resolved = FileSystem::Resolve(path);
        std::ifstream is(std::filesystem::u8path(resolved), std::ios::binary);
        if (!is)
        {
            if (error) *error = "cannot open " + path;
            CS_CORE_ERROR("StoryGraph::Load: cannot open {0}", path);
            return false;
        }
        std::stringstream ss;
        ss << is.rdbuf();
        return LoadFromString(out, ss.str(), error);
    }

    bool StoryGraph::Save(const std::string& path) const
    {
        const std::string resolved = FileSystem::Resolve(path);
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::create_directories(fs::u8path(resolved).parent_path(), ec);
        std::ofstream os(fs::u8path(resolved), std::ios::binary | std::ios::trunc);
        if (!os)
        {
            CS_CORE_ERROR("StoryGraph::Save: cannot write {0}", path);
            return false;
        }
        os << SaveToString();
        return true;
    }

    std::vector<std::string> StoryGraph::Validate() const
    {
        std::vector<std::string> errors;
        if (Start.empty())
            errors.push_back("missing 'start' node");
        else if (!Find(Start))
            errors.push_back("start node '" + Start + "' not found");

        std::unordered_set<std::string> seen;
        for (const StoryNode& n : Nodes)
        {
            if (n.Name.empty()) { errors.push_back("a node has no name"); continue; }
            if (!seen.insert(n.Name).second)
                errors.push_back("duplicate node name '" + n.Name + "'");
            for (const StoryOption& o : n.Options)
            {
                if (o.Next.empty() || o.Next == "@end") continue;
                if (!Find(o.Next))
                    errors.push_back("node '" + n.Name + "' option -> unknown node '" + o.Next + "'");
            }
        }
        return errors;
    }

    // ========================================================================
    // StoryRunner
    // ========================================================================

    void StoryRunner::Start(const StoryGraph& graph, Scene* scene, FlowMachine* sharedVars)
    {
        Stop();
        m_Graph      = graph;
        m_Scene      = scene;
        m_SharedVars = sharedVars;
        m_Running    = true;
        m_Ended      = false;
        m_Chosen.clear();
        m_Vars.clear();

        // Seed the blackboard: own store, or top-up the shared flow store with any
        // story-only variables it doesn't already have (flow values win).
        if (m_SharedVars)
        {
            for (const FlowVariable& v : m_Graph.Variables)
                if (!m_SharedVars->HasVar(v.Name)) m_SharedVars->SetVar(v.Name, v.Default);
        }
        else
        {
            for (const FlowVariable& v : m_Graph.Variables)
                m_Vars[v.Name] = v.Default;
        }

        Enter(m_Graph.Start);
    }

    void StoryRunner::Stop()
    {
        m_Running = false;
        m_Ended   = false;
        m_Current.clear();
        m_Valid.clear();
        m_Chosen.clear();
        m_Vars.clear();
        m_Scene = nullptr;
        m_SharedVars = nullptr;
    }

    const StoryNode* StoryRunner::Current() const
    {
        if (!m_Running || m_Ended || m_Current.empty()) return nullptr;
        return m_Graph.Find(m_Current);
    }

    FlowValue StoryRunner::GetVar(const std::string& name) const
    {
        if (m_SharedVars) return m_SharedVars->GetVar(name);
        auto it = m_Vars.find(name);
        return it != m_Vars.end() ? it->second : FlowValue::MakeBool(false);
    }

    void StoryRunner::SetVar(const std::string& name, const FlowValue& value)
    {
        if (name.empty()) return;
        if (m_SharedVars) m_SharedVars->SetVar(name, value);
        else              m_Vars[name] = value;
    }

    std::string StoryRunner::OnceKey(int optionIndex) const
    {
        return m_Current + "#" + std::to_string(optionIndex);
    }

    void StoryRunner::Emit(const std::vector<std::string>& signals)
    {
        if (!m_Scene) return;
        for (const std::string& s : signals)
            if (!s.empty()) m_Scene->Events().Emit(s, Entity());
    }

    void StoryRunner::RebuildValid()
    {
        m_Valid.clear();
        const StoryNode* node = Current();
        if (!node) return;

        auto lookup = [this](const std::string& n, FlowValue& out) -> bool
        {
            if (m_SharedVars)
            {
                if (!m_SharedVars->HasVar(n)) return false;
                out = m_SharedVars->GetVar(n);
                return true;
            }
            auto it = m_Vars.find(n);
            if (it == m_Vars.end()) return false;
            out = it->second;
            return true;
        };

        for (int i = 0; i < (int)node->Options.size(); ++i)
        {
            const StoryOption& o = node->Options[i];
            if (o.Once && m_Chosen.count(OnceKey(i))) continue;
            if (o.HasGuard && !EvaluateFlowGuard(o.Guard, m_Scene, lookup)) continue;
            m_Valid.push_back(i);
        }
    }

    void StoryRunner::Enter(const std::string& nodeName)
    {
        if (nodeName.empty() || nodeName == "@end")
        {
            m_Ended = true;
            m_Current.clear();
            m_Valid.clear();
            return;
        }
        const StoryNode* node = m_Graph.Find(nodeName);
        if (!node)
        {
            CS_CORE_WARN("StoryRunner: unknown node '{0}' — ending", nodeName);
            m_Ended = true;
            m_Current.clear();
            m_Valid.clear();
            return;
        }
        m_Current = nodeName;
        Emit(node->OnEnter);
        RebuildValid();
    }

    void StoryRunner::Choose(int validIndex)
    {
        if (!m_Running || m_Ended) return;
        if (validIndex < 0 || validIndex >= (int)m_Valid.size()) return;
        const StoryNode* node = Current();
        if (!node) return;

        const int optIdx = m_Valid[validIndex];
        const StoryOption opt = node->Options[optIdx];   // copy — Enter re-points Current

        Emit(node->OnExit);
        if (opt.Once) m_Chosen.insert(OnceKey(optIdx));
        Enter(opt.Next);
    }
}
