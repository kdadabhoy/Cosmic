// scene/FlowMachine.cpp — screen-flow runtime + `.cflow` (de)serialization (U5).

#include "scene/FlowMachine.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"       // TagComponent (guard/setField target lookup)
#include "scene/EventBus.h"
#include "reflect/TypeRegistry.h"
#include "reflect/TypeDescriptor.h"
#include "utils/FileSystem.h"
#include "core/Log.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace Cosmic
{
    using nlohmann::json;

    // ========================================================================
    // FlowValue JSON <-> struct
    // ========================================================================
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
            }
            return json(false);
        }
    }

    // ========================================================================
    // FlowAsset
    // ========================================================================

    const FlowState* FlowAsset::Find(const std::string& name) const
    {
        for (const auto& s : States)
            if (s.Name == name) return &s;
        return nullptr;
    }

    bool FlowAsset::LoadFromString(FlowAsset& out, const std::string& jsonText, std::string* error)
    {
        out = FlowAsset{};
        json j;
        try { j = json::parse(jsonText); }
        catch (const std::exception& e)
        {
            if (error) *error = std::string("parse error: ") + e.what();
            return false;
        }

        out.Version = j.value("cosmic_flow", 1);
        out.Start   = j.value("start", std::string());

        if (j.contains("states") && j["states"].is_array())
        {
            for (const auto& js : j["states"])
            {
                FlowState s;
                s.Name    = js.value("name", std::string());
                s.Scene   = js.value("scene", std::string());
                s.Overlay = js.value("overlay", false);

                if (js.contains("onEnter") && js["onEnter"].is_array())
                {
                    for (const auto& ja : js["onEnter"])
                    {
                        FlowAction a;
                        if (ja.contains("emit"))
                        {
                            a.ActionType = FlowAction::Type::Emit;
                            a.Signal = ja["emit"].get<std::string>();
                        }
                        else if (ja.contains("setField") && ja["setField"].is_object())
                        {
                            const auto& sf = ja["setField"];
                            a.ActionType = FlowAction::Type::SetField;
                            a.Entity    = sf.value("entity", std::string());
                            a.Component = sf.value("component", std::string());
                            a.Field     = sf.value("field", std::string());
                            if (sf.contains("value")) a.Value = ParseValue(sf["value"]);
                        }
                        else continue;
                        s.OnEnter.push_back(std::move(a));
                    }
                }

                if (js.contains("transitions") && js["transitions"].is_array())
                {
                    for (const auto& jt : js["transitions"])
                    {
                        FlowTransition t;
                        t.On         = jt.value("on", std::string());
                        t.To         = jt.value("to", std::string());
                        t.Transition = jt.value("transition", std::string("None"));
                        t.Push       = jt.value("push", false);
                        if (jt.contains("if") && jt["if"].is_object())
                        {
                            const auto& g = jt["if"];
                            t.HasGuard        = true;
                            t.Guard.Entity    = g.value("entity", std::string());
                            t.Guard.Component = g.value("component", std::string());
                            t.Guard.Field     = g.value("field", std::string());
                            t.Guard.Op        = g.value("op", std::string("=="));
                            if (g.contains("value")) t.Guard.Value = ParseValue(g["value"]);
                        }
                        s.Transitions.push_back(std::move(t));
                    }
                }

                if (js.contains("editor") && js["editor"].is_object() &&
                    js["editor"].contains("pos") && js["editor"]["pos"].is_array() &&
                    js["editor"]["pos"].size() == 2)
                {
                    s.EditorPos.x = js["editor"]["pos"][0].get<float>();
                    s.EditorPos.y = js["editor"]["pos"][1].get<float>();
                }

                out.States.push_back(std::move(s));
            }
        }
        return true;
    }

    std::string FlowAsset::SaveToString() const
    {
        json j;
        j["cosmic_flow"] = Version;
        j["start"]       = Start;
        json jstates = json::array();
        for (const auto& s : States)
        {
            json js;
            js["name"] = s.Name;
            if (!s.Scene.empty()) js["scene"] = s.Scene;
            if (s.Overlay)        js["overlay"] = true;

            if (!s.OnEnter.empty())
            {
                json ja = json::array();
                for (const auto& a : s.OnEnter)
                {
                    if (a.ActionType == FlowAction::Type::Emit)
                        ja.push_back({ { "emit", a.Signal } });
                    else
                        ja.push_back({ { "setField", {
                            { "entity", a.Entity }, { "component", a.Component },
                            { "field", a.Field }, { "value", ValueToJson(a.Value) } } } });
                }
                js["onEnter"] = std::move(ja);
            }

            json jt = json::array();
            for (const auto& t : s.Transitions)
            {
                json o;
                o["on"] = t.On;
                o["to"] = t.To;
                if (t.Transition != "None") o["transition"] = t.Transition;
                if (t.Push) o["push"] = true;
                if (t.HasGuard)
                    o["if"] = { { "entity", t.Guard.Entity }, { "component", t.Guard.Component },
                                { "field", t.Guard.Field }, { "op", t.Guard.Op },
                                { "value", ValueToJson(t.Guard.Value) } };
                jt.push_back(std::move(o));
            }
            js["transitions"] = std::move(jt);
            js["editor"] = { { "pos", { s.EditorPos.x, s.EditorPos.y } } };

            jstates.push_back(std::move(js));
        }
        j["states"] = std::move(jstates);
        return j.dump(2);
    }

    bool FlowAsset::Load(FlowAsset& out, const std::string& path, std::string* error)
    {
        const std::string resolved = FileSystem::Resolve(path);
        std::ifstream is(std::filesystem::u8path(resolved), std::ios::binary);
        if (!is)
        {
            if (error) *error = "cannot open " + path;
            CS_CORE_ERROR("FlowAsset::Load: cannot open {0}", path);
            return false;
        }
        std::stringstream ss;
        ss << is.rdbuf();
        return LoadFromString(out, ss.str(), error);
    }

    bool FlowAsset::Save(const std::string& path) const
    {
        const std::string resolved = FileSystem::Resolve(path);
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::create_directories(fs::u8path(resolved).parent_path(), ec);
        std::ofstream os(fs::u8path(resolved), std::ios::binary | std::ios::trunc);
        if (!os)
        {
            CS_CORE_ERROR("FlowAsset::Save: cannot write {0}", path);
            return false;
        }
        os << SaveToString();
        return true;
    }

    std::vector<std::string> FlowAsset::Validate() const
    {
        std::vector<std::string> errors;
        if (Start.empty())
            errors.push_back("missing 'start' state");
        else if (!Find(Start))
            errors.push_back("start state '" + Start + "' not found");

        std::unordered_set<std::string> seen;
        for (const auto& s : States)
        {
            if (s.Name.empty()) { errors.push_back("a state has no name"); continue; }
            if (!seen.insert(s.Name).second)
                errors.push_back("duplicate state name '" + s.Name + "'");
            for (const auto& t : s.Transitions)
            {
                if (t.To.empty())
                    errors.push_back("state '" + s.Name + "' has a transition with no target");
                else if (t.To[0] == '@')
                {
                    if (t.To != "@quit" && t.To != "@pop")
                        errors.push_back("state '" + s.Name + "' has unknown builtin target '" + t.To + "'");
                }
                else if (!Find(t.To))
                    errors.push_back("state '" + s.Name + "' transitions to unknown state '" + t.To + "'");
            }
        }
        return errors;
    }

    // ========================================================================
    // Reflection helpers (guards + setField)
    // ========================================================================
    namespace
    {
        entt::entity FindTag(Scene& scene, const std::string& tag)
        {
            auto& reg = scene.GetRegistry();
            for (auto e : reg.view<TagComponent>())
                if (reg.get<TagComponent>(e).Tag == tag) return e;
            return entt::null;
        }

        bool AsNumber(const Reflect::FieldValue& v, double& out)
        {
            if (auto p = std::get_if<int32_t>(&v))  { out = *p; return true; }
            if (auto p = std::get_if<uint32_t>(&v)) { out = *p; return true; }
            if (auto p = std::get_if<uint64_t>(&v)) { out = (double)*p; return true; }
            if (auto p = std::get_if<float>(&v))    { out = *p; return true; }
            return false;
        }

        bool CompareValue(const Reflect::FieldValue& lhs, const FlowValue& rhs, const std::string& op)
        {
            switch (rhs.ValueKind)
            {
                case FlowValue::Kind::Bool:
                {
                    auto p = std::get_if<bool>(&lhs);
                    if (!p) return false;
                    if (op == "==") return *p == rhs.Bool;
                    if (op == "!=") return *p != rhs.Bool;
                    return false;
                }
                case FlowValue::Kind::Number:
                {
                    double d;
                    if (!AsNumber(lhs, d)) return false;
                    const double r = rhs.Number;
                    if (op == "==") return std::abs(d - r) < 1e-6;
                    if (op == "!=") return std::abs(d - r) >= 1e-6;
                    if (op == "<")  return d <  r;
                    if (op == ">")  return d >  r;
                    if (op == "<=") return d <= r;
                    if (op == ">=") return d >= r;
                    return false;
                }
                case FlowValue::Kind::String:
                {
                    auto p = std::get_if<std::string>(&lhs);
                    if (!p) return false;
                    if (op == "==") return *p == rhs.String;
                    if (op == "!=") return *p != rhs.String;
                    return false;
                }
            }
            return false;
        }

        void WriteFieldValue(Scene& scene, const std::string& tag, const std::string& comp,
                             const std::string& field, const FlowValue& val)
        {
            entt::entity e = FindTag(scene, tag);
            if (e == entt::null) { CS_CORE_WARN("flow setField: no entity tagged '{0}'", tag); return; }
            const auto* desc = Reflect::GetRegistry().FindByName(comp);
            if (!desc) { CS_CORE_WARN("flow setField: unknown component '{0}'", comp); return; }
            void* inst = desc->Get(scene.GetRegistry(), e);
            if (!inst) { CS_CORE_WARN("flow setField: entity '{0}' has no {1}", tag, comp); return; }
            const auto* fd = desc->FindField(field);
            if (!fd) { CS_CORE_WARN("flow setField: {0} has no field '{1}'", comp, field); return; }

            using K = Reflect::FieldKind;
            Reflect::FieldValue fv;
            switch (fd->Kind)
            {
                case K::Bool:      fv = val.Bool; break;
                case K::Int32:     fv = (int32_t)val.Number; break;
                case K::UInt32:    fv = (uint32_t)val.Number; break;
                case K::EntityRef: fv = (uint64_t)val.Number; break;
                case K::Float:     fv = (float)val.Number; break;
                case K::Enum:      fv = (int32_t)val.Number; break;
                case K::String:
                case K::AssetPath: fv = val.String; break;
                default:
                    CS_CORE_WARN("flow setField: field '{0}' has an unsupported kind", field);
                    return;
            }
            fd->Set(inst, fv);
        }
    }

    // ========================================================================
    // FlowMachine
    // ========================================================================

    FlowMachine::~FlowMachine() { Stop(); }

    void FlowMachine::Start(const FlowAsset& asset)
    {
        Stop();
        m_Asset   = asset;
        m_Quit    = false;
        m_Running = true;
        m_Elapsed = 0.0f;

        const FlowState* s = m_Asset.Find(m_Asset.Start);
        if (!s)
        {
            CS_CORE_WARN("FlowMachine::Start: start state '{0}' not found", m_Asset.Start);
            m_Running = false;
            return;
        }
        Enter(*s, /*push=*/false);
    }

    void FlowMachine::Stop()
    {
        UnsubscribeActiveBus();
        m_Stack.clear();
        m_Pending.clear();
        m_Running = false;
        m_Elapsed = 0.0f;
    }

    void FlowMachine::FeedSignal(const std::string& signal)
    {
        if (m_Running) m_Pending.push_back(signal);
    }

    const std::string& FlowMachine::CurrentState() const
    {
        static const std::string empty;
        return m_Stack.empty() ? empty : m_Stack.back().StateName;
    }

    Ref<Scene> FlowMachine::ActiveScene() const
    {
        return m_Stack.empty() ? nullptr : m_Stack.back().ActiveScene;
    }

    const FlowState* FlowMachine::CurrentStateDef() const
    {
        if (m_Stack.empty()) return nullptr;
        return m_Asset.Find(m_Stack.back().StateName);
    }

    void FlowMachine::SubscribeActiveBus()
    {
        Ref<Scene> scene = ActiveScene();
        if (!scene) return;
        m_BusScene  = scene.get();
        m_BusHandle = scene->Events().ConnectAny(
            [this](const std::string& sig, Entity) { FeedSignal(sig); });
    }

    void FlowMachine::UnsubscribeActiveBus()
    {
        if (m_BusScene && m_BusHandle)
            m_BusScene->Events().Disconnect(m_BusHandle);
        m_BusScene  = nullptr;
        m_BusHandle = 0;
    }

    void FlowMachine::Enter(const FlowState& state, bool push)
    {
        // Resolve the scene this state shows.
        Ref<Scene> scene;
        if (!state.Scene.empty())
        {
            scene = m_Loader ? m_Loader(state.Scene) : nullptr;
            if (!scene)
            {
                CS_CORE_WARN("FlowMachine: failed to load scene '{0}' for state '{1}'",
                             state.Scene, state.Name);
                if (!m_Stack.empty()) scene = m_Stack.back().ActiveScene;  // keep current
            }
        }
        else
        {
            // Overlay / scene-less state reuses the current active scene.
            if (!m_Stack.empty()) scene = m_Stack.back().ActiveScene;
        }

        UnsubscribeActiveBus();

        if (push)
            m_Stack.push_back({ state.Name, scene });
        else
        {
            m_Stack.clear();
            m_Stack.push_back({ state.Name, scene });
        }
        m_Elapsed = 0.0f;

        SubscribeActiveBus();
        RunActions(state.OnEnter);   // emits queue onto m_Pending via the bus subscription
    }

    void FlowMachine::RunActions(const std::vector<FlowAction>& actions)
    {
        Ref<Scene> scene = ActiveScene();
        for (const auto& a : actions)
        {
            if (a.ActionType == FlowAction::Type::Emit)
            {
                if (scene) scene->Events().Emit(a.Signal, Entity());
                else       FeedSignal(a.Signal);   // no scene: still drive the flow
            }
            else // SetField
            {
                if (scene) WriteFieldValue(*scene, a.Entity, a.Component, a.Field, a.Value);
            }
        }
    }

    bool FlowMachine::EvalGuard(const FlowGuard& guard) const
    {
        Ref<Scene> scene = m_Stack.empty() ? nullptr : m_Stack.back().ActiveScene;
        auto warnOnce = [this](const std::string& key)
        {
            if (m_GuardWarned.insert(key).second)
                CS_CORE_WARN("flow guard: {0}", key);
        };
        if (!scene) { warnOnce("no active scene"); return false; }

        entt::entity e = FindTag(*scene, guard.Entity);
        if (e == entt::null) { warnOnce("no entity tagged '" + guard.Entity + "'"); return false; }
        const auto* desc = Reflect::GetRegistry().FindByName(guard.Component);
        if (!desc) { warnOnce("unknown component '" + guard.Component + "'"); return false; }
        void* inst = desc->Get(scene->GetRegistry(), e);
        if (!inst) { warnOnce(guard.Entity + " has no " + guard.Component); return false; }
        const auto* fd = desc->FindField(guard.Field);
        if (!fd) { warnOnce(guard.Component + " has no field '" + guard.Field + "'"); return false; }

        return CompareValue(fd->Get(inst), guard.Value, guard.Op);
    }

    bool FlowMachine::TryFireSignal(const std::string& signal)
    {
        const FlowState* cur = CurrentStateDef();
        if (!cur) return false;
        for (const auto& t : cur->Transitions)
        {
            if (t.On != signal) continue;
            if (t.HasGuard && !EvalGuard(t.Guard)) continue;
            PerformTransition(t);
            return true;
        }
        return false;
    }

    bool FlowMachine::TryFireTimer()
    {
        const FlowState* cur = CurrentStateDef();
        if (!cur) return false;
        for (const auto& t : cur->Transitions)
        {
            if (t.On.rfind("timer:", 0) != 0) continue;
            const float seconds = std::strtof(t.On.c_str() + 6, nullptr);
            if (m_Elapsed < seconds) continue;
            if (t.HasGuard && !EvalGuard(t.Guard)) continue;
            PerformTransition(t);
            return true;
        }
        return false;
    }

    void FlowMachine::PerformTransition(const FlowTransition& t)
    {
        if (t.To == "@quit")
        {
            m_Quit = true;
            Stop();
            return;
        }
        if (t.To == "@pop")
        {
            if (m_Stack.size() > 1)
            {
                UnsubscribeActiveBus();
                m_Stack.pop_back();
                m_Elapsed = 0.0f;
                SubscribeActiveBus();
            }
            else CS_CORE_WARN("FlowMachine: @pop with no overlay to pop");
            return;
        }
        const FlowState* s = m_Asset.Find(t.To);
        if (!s)
        {
            CS_CORE_WARN("FlowMachine: transition to unknown state '{0}'", t.To);
            return;
        }
        Enter(*s, t.Push);
    }

    void FlowMachine::OnUpdate(float dt)
    {
        if (!m_Running) return;
        m_Elapsed += dt;

        // Drain queued signals (cascades: an onEnter emit re-queues into the same
        // loop). Bounded to avoid an authored infinite ping-pong.
        int iterations = 0;
        while (m_Running && !m_Pending.empty())
        {
            const std::string sig = m_Pending.front();
            m_Pending.erase(m_Pending.begin());
            TryFireSignal(sig);
            if (++iterations > 100000)
            {
                CS_CORE_WARN("FlowMachine: signal cascade guard tripped (possible cycle)");
                m_Pending.clear();
                break;
            }
        }

        if (m_Running) TryFireTimer();
    }
}
