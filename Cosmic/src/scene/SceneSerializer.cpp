// scene/SceneSerializer.cpp — generic JSON (de)serialization over the E1
// reflection registry (Phase 13 / E2).

#include "scene/SceneSerializer.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "reflect/TypeRegistry.h"
#include "core/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace Cosmic
{
    using nlohmann::json;
    using namespace Cosmic::Reflect;

    namespace
    {
        float ArrF(const json& j, size_t i, float def = 0.0f)
        {
            return (j.is_array() && i < j.size() && j[i].is_number())
                       ? j[i].get<float>() : def;
        }

        // FieldValue -> JSON, keyed by the field's declared kind.
        json SerializeValue(const FieldDescriptor& f, const FieldValue& v)
        {
            switch (f.Kind)
            {
            case FieldKind::Bool:   return std::get<bool>(v);
            case FieldKind::Int32:  return std::get<int32_t>(v);
            case FieldKind::UInt32: return std::get<uint32_t>(v);
            case FieldKind::Float:  return std::get<float>(v);
            case FieldKind::Vec2:  { auto a = std::get<glm::vec2>(v); return json::array({ a.x, a.y }); }
            case FieldKind::Vec3:  { auto a = std::get<glm::vec3>(v); return json::array({ a.x, a.y, a.z }); }
            case FieldKind::Vec4:
            case FieldKind::Color: { auto a = std::get<glm::vec4>(v); return json::array({ a.x, a.y, a.z, a.w }); }
            case FieldKind::Quat:  { auto q = std::get<glm::quat>(v); return json::array({ q.w, q.x, q.y, q.z }); }
            case FieldKind::String:
            case FieldKind::AssetPath: return std::get<std::string>(v);
            case FieldKind::EntityRef: return UUID(std::get<uint64_t>(v)).ToString();
            case FieldKind::Enum:      return std::get<int32_t>(v);
            }
            return nullptr;
        }

        // JSON -> FieldValue, keyed by the field's declared kind. Tolerant of
        // missing/short arrays and (for enums) a stored name instead of an int.
        FieldValue DeserializeValue(const FieldDescriptor& f, const json& j)
        {
            switch (f.Kind)
            {
            case FieldKind::Bool:   return FieldValue{ j.is_boolean() ? j.get<bool>() : false };
            case FieldKind::Int32:  return FieldValue{ j.is_number() ? j.get<int32_t>() : 0 };
            case FieldKind::UInt32: return FieldValue{ j.is_number() ? j.get<uint32_t>() : 0u };
            case FieldKind::Float:  return FieldValue{ j.is_number() ? j.get<float>() : 0.0f };
            case FieldKind::Vec2:   return FieldValue{ glm::vec2(ArrF(j, 0), ArrF(j, 1)) };
            case FieldKind::Vec3:   return FieldValue{ glm::vec3(ArrF(j, 0), ArrF(j, 1), ArrF(j, 2)) };
            case FieldKind::Vec4:
            case FieldKind::Color:  return FieldValue{ glm::vec4(ArrF(j, 0), ArrF(j, 1), ArrF(j, 2), ArrF(j, 3, 1.0f)) };
            case FieldKind::Quat:   return FieldValue{ glm::quat(ArrF(j, 0, 1.0f), ArrF(j, 1), ArrF(j, 2), ArrF(j, 3)) };
            case FieldKind::String:
            case FieldKind::AssetPath: return FieldValue{ j.is_string() ? j.get<std::string>() : std::string() };
            case FieldKind::EntityRef: return FieldValue{ j.is_string() ? UUID::FromString(j.get<std::string>()).Value() : uint64_t(0) };
            case FieldKind::Enum:
            {
                if (j.is_number()) return FieldValue{ j.get<int32_t>() };
                if (j.is_string())
                {
                    const std::string name = j.get<std::string>();
                    for (const auto& e : f.Hints.EnumEntries)
                        if (e.Name == name) return FieldValue{ e.Value };
                }
                return FieldValue{ int32_t(0) };
            }
            }
            return FieldValue{ int32_t(0) };
        }
    }

    std::string SceneSerializer::SaveToString(Scene& scene)
    {
        auto& reg      = scene.GetRegistry();
        auto& registry = Reflect::GetRegistry();

        // Deterministic ordering: emit entities sorted by UUID so save/load/save
        // is byte-identical regardless of entt's internal storage order.
        std::vector<entt::entity> entities;
        for (auto e : reg.view<IDComponent>())
            entities.push_back(e);
        std::sort(entities.begin(), entities.end(), [&](entt::entity a, entt::entity b)
        {
            return reg.get<IDComponent>(a).ID.Value() < reg.get<IDComponent>(b).ID.Value();
        });

        json out;
        out["cosmic_scene"] = 1;
        json jents = json::array();

        for (auto e : entities)
        {
            json je;
            je["id"] = reg.get<IDComponent>(e).ID.ToString();

            json comps = json::object();
            for (const TypeDescriptor* d : registry.ComponentsOf(reg, e))
            {
                void* comp = d->Get(reg, e);
                if (!comp && !d->Fields.empty())
                    continue;

                json cj = json::object();
                for (const auto& f : d->Fields)
                {
                    if (f.HasFlag(Field_NoSerialize))
                        continue;
                    cj[f.Name] = SerializeValue(f, f.Get(comp));
                }
                comps[d->Name] = cj;
            }

            // Verbatim unknown blocks (preserved from a prior load).
            if (reg.all_of<OpaqueComponentsComponent>(e))
            {
                for (const auto& [name, text] : reg.get<OpaqueComponentsComponent>(e).Blocks)
                {
                    json parsed = json::parse(text, nullptr, false);
                    if (!parsed.is_discarded())
                        comps[name] = parsed;
                }
            }

            // Hierarchy (E3): a parent emits its ordered Children under a
            // "Relationship" block. Roots and leaves add nothing, so flat scenes
            // (every shipped app) serialize byte-identically to before E3.
            if (reg.all_of<RelationshipComponent>(e))
            {
                const auto& rel = reg.get<RelationshipComponent>(e);
                if (!rel.Children.empty())
                {
                    json kids = json::array();
                    for (const UUID& c : rel.Children)
                        kids.push_back(UUID(c).ToString());
                    json rj;
                    rj["Children"] = std::move(kids);
                    comps["Relationship"] = std::move(rj);
                }
            }

            je["components"] = comps;
            jents.push_back(std::move(je));
        }

        out["entities"] = std::move(jents);
        return out.dump(2);
    }

    bool SceneSerializer::LoadFromString(Scene& scene, const std::string& text)
    {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded())
        {
            CS_CORE_ERROR("SceneSerializer: JSON parse failed.");
            return false;
        }
        if (!j.contains("entities") || !j["entities"].is_array())
        {
            CS_CORE_ERROR("SceneSerializer: missing 'entities' array.");
            return false;
        }

        auto& reg      = scene.GetRegistry();
        auto& registry = Reflect::GetRegistry();

        // Hierarchy links resolved in a second pass (children may not exist yet
        // when their parent's block is read).
        struct PendingLink { UUID Owner; std::vector<UUID> Children; };
        std::vector<PendingLink> pendingHierarchy;

        for (const auto& je : j["entities"])
        {
            UUID id = (je.contains("id") && je["id"].is_string())
                          ? UUID::FromString(je["id"].get<std::string>())
                          : UUID();

            Entity e = scene.CreateEntityWithUUID(id);   // Tag/Transform blocks overwrite defaults
            const entt::entity handle = (entt::entity)e;

            if (!je.contains("components") || !je["components"].is_object())
                continue;

            for (const auto& item : je["components"].items())
            {
                const std::string& compName = item.key();
                const json&        compJson = item.value();

                // Hierarchy is structural, not reflected — collect for pass 2.
                if (compName == "Relationship")
                {
                    if (compJson.contains("Children") && compJson["Children"].is_array())
                    {
                        PendingLink link;
                        link.Owner = id;
                        for (const auto& c : compJson["Children"])
                            if (c.is_string())
                                link.Children.push_back(UUID::FromString(c.get<std::string>()));
                        pendingHierarchy.push_back(std::move(link));
                    }
                    continue;
                }

                const TypeDescriptor* d = registry.FindByName(compName);
                if (!d)
                {
                    // Unknown component type — preserve verbatim so re-saving in
                    // this build never drops another module's data.
                    reg.get_or_emplace<OpaqueComponentsComponent>(handle)
                       .Blocks.emplace_back(compName, compJson.dump());
                    continue;
                }

                void* comp = d->Add(reg, handle);
                if (!comp)
                    continue;   // empty/tag component — presence only

                for (const auto& f : d->Fields)
                {
                    if (f.HasFlag(Field_NoSerialize))
                        continue;
                    if (compJson.contains(f.Name))
                        f.Set(comp, DeserializeValue(f, compJson[f.Name]));
                }
            }
        }

        // Pass 2 — wire the hierarchy. keepWorldPose=false: the saved transforms
        // are already LOCAL, so parenting must not rewrite them. SetParent
        // appends children in file order, preserving Children ordering.
        for (const auto& link : pendingHierarchy)
        {
            Entity owner = scene.FindByUUID(link.Owner);
            if (!owner)
                continue;
            for (const UUID& childID : link.Children)
            {
                Entity child = scene.FindByUUID(childID);
                if (child)
                    scene.SetParent(child, owner, /*keepWorldPose=*/false);
            }
        }

        return true;
    }

    bool SceneSerializer::Save(Scene& scene, const std::string& path)
    {
        const std::string text = SaveToString(scene);

        namespace fs = std::filesystem;
        const fs::path dest = fs::u8path(path);
        if (dest.has_parent_path())
        {
            std::error_code mkec;
            fs::create_directories(dest.parent_path(), mkec);
        }

        const fs::path tmp = fs::path(dest).concat(".tmp");
        {
            std::ofstream os(tmp, std::ios::binary | std::ios::trunc);
            if (!os)
            {
                CS_CORE_ERROR("SceneSerializer::Save: cannot open temp file for {0}", path);
                return false;
            }
            os << text;
            if (!os)
            {
                CS_CORE_ERROR("SceneSerializer::Save: write failed for {0}", path);
                return false;
            }
        }

        std::error_code ec;
        fs::rename(tmp, dest, ec);
        if (ec)
        {
            // Destination may already exist on some platforms — replace it.
            fs::remove(dest, ec);
            fs::rename(tmp, dest, ec);
            if (ec)
            {
                CS_CORE_ERROR("SceneSerializer::Save: atomic rename failed for {0}", path);
                return false;
            }
        }
        return true;
    }

    bool SceneSerializer::Load(Scene& scene, const std::string& path)
    {
        std::ifstream is(std::filesystem::u8path(path), std::ios::binary);
        if (!is)
        {
            CS_CORE_ERROR("SceneSerializer::Load: cannot open {0}", path);
            return false;
        }
        std::stringstream ss;
        ss << is.rdbuf();
        return LoadFromString(scene, ss.str());
    }
}
