// scene/SceneSerializer.cpp — generic JSON (de)serialization over the E1
// reflection registry (Phase 13 / E2).

#include "scene/SceneSerializer.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "reflect/TypeRegistry.h"
#include "scripting/ModuleRegistry.h"   // E11 — typed NativeScript field (de)serialization
#include "core/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
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

        // Deferred parent->children link (children may not exist when a parent's
        // block is read). Shared by scene load (LoadFromString) and prefab load.
        struct PendingLink { UUID Owner; std::vector<UUID> Children; };

        // Load every component block of one entity into `handle`. Collects the
        // "Relationship" block into `pending` (keyed by `ownerId`) for a second
        // pass, preserves unknown blocks verbatim, and reloads NativeScript field
        // overrides. Shared by scene + prefab loaders so both stay in lockstep.
        void LoadEntityComponents(entt::registry& reg, entt::entity handle,
                                  Reflect::TypeRegistry& registry, const json& compsJson,
                                  std::vector<PendingLink>& pending, UUID ownerId)
        {
            for (const auto& item : compsJson.items())
            {
                const std::string& compName = item.key();
                const json&        compJson = item.value();

                if (compName == "Relationship")
                {
                    if (compJson.contains("Children") && compJson["Children"].is_array())
                    {
                        PendingLink link;
                        link.Owner = ownerId;
                        for (const auto& c : compJson["Children"])
                            if (c.is_string())
                                link.Children.push_back(UUID::FromString(c.get<std::string>()));
                        pending.push_back(std::move(link));
                    }
                    continue;
                }

                const TypeDescriptor* d = registry.FindByName(compName);
                if (!d)
                {
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

                if (compName == "NativeScript" && compJson.contains("Fields")
                    && compJson["Fields"].is_object())
                {
                    auto* nsc = static_cast<NativeScriptComponent*>(comp);
                    if (const ScriptDescriptor* sd = ModuleRegistry::Get().FindScript(nsc->ClassName))
                    {
                        const json& fj = compJson["Fields"];
                        for (const auto& sf : sd->Fields.Fields)
                            if (fj.contains(sf.Name))
                                nsc->Fields[sf.Name] = DeserializeValue(sf, fj[sf.Name]);
                    }
                }

                // SystemScript reflected overrides (H9) — same out-of-band path, via
                // the SystemDescriptor's field list.
                if (compName == "SystemScript" && compJson.contains("Fields")
                    && compJson["Fields"].is_object())
                {
                    auto* ssc = static_cast<SystemScriptComponent*>(comp);
                    if (const SystemDescriptor* sd = ModuleRegistry::Get().FindSystem(ssc->ClassName))
                    {
                        const json& fj = compJson["Fields"];
                        for (const auto& sf : sd->Fields.Fields)
                            if (fj.contains(sf.Name))
                                ssc->Fields[sf.Name] = DeserializeValue(sf, fj[sf.Name]);
                    }
                }
            }
        }

        // Serialize one entity (id + every component block) to a JSON object.
        // Shared by scene save (SaveToString) and prefab save (SavePrefab).
        json SerializeEntity(entt::registry& reg, Reflect::TypeRegistry& registry, entt::entity e)
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

                if (d->Name == "NativeScript")
                {
                    auto* nsc = static_cast<const NativeScriptComponent*>(comp);
                    if (const ScriptDescriptor* sd = ModuleRegistry::Get().FindScript(nsc->ClassName))
                    {
                        json fj = json::object();
                        for (const auto& sf : sd->Fields.Fields)
                        {
                            auto it = nsc->Fields.find(sf.Name);
                            if (it != nsc->Fields.end())
                                fj[sf.Name] = SerializeValue(sf, it->second);
                        }
                        if (!fj.empty())
                            cj["Fields"] = std::move(fj);
                    }
                }

                if (d->Name == "SystemScript")   // H9 — mirror of the NativeScript block
                {
                    auto* ssc = static_cast<const SystemScriptComponent*>(comp);
                    if (const SystemDescriptor* sd = ModuleRegistry::Get().FindSystem(ssc->ClassName))
                    {
                        json fj = json::object();
                        for (const auto& sf : sd->Fields.Fields)
                        {
                            auto it = ssc->Fields.find(sf.Name);
                            if (it != ssc->Fields.end())
                                fj[sf.Name] = SerializeValue(sf, it->second);
                        }
                        if (!fj.empty())
                            cj["Fields"] = std::move(fj);
                    }
                }

                comps[d->Name] = cj;
            }

            if (reg.all_of<OpaqueComponentsComponent>(e))
            {
                for (const auto& [name, text] : reg.get<OpaqueComponentsComponent>(e).Blocks)
                {
                    json parsed = json::parse(text, nullptr, false);
                    if (!parsed.is_discarded())
                        comps[name] = parsed;
                }
            }

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
            return je;
        }

        // Preorder subtree walk (parent, then children in stored order) via the
        // RelationshipComponent tree — used by prefab save (E14).
        void GatherSubtree(Scene& scene, entt::entity node, std::vector<entt::entity>& out)
        {
            out.push_back(node);
            auto& reg = scene.GetRegistry();
            if (auto* rel = reg.try_get<RelationshipComponent>(node))
                for (const UUID& childId : rel->Children)
                {
                    Entity child = scene.FindByUUID(childId);
                    if (child) GatherSubtree(scene, (entt::entity)child, out);
                }
        }

        // Crash-safe text write (temp file + atomic rename). Shared by Save + SavePrefab.
        bool WriteTextAtomic(const std::string& path, const std::string& text)
        {
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
                if (!os) { CS_CORE_ERROR("SceneSerializer: cannot open temp file for {0}", path); return false; }
                os << text;
                if (!os) { CS_CORE_ERROR("SceneSerializer: write failed for {0}", path); return false; }
            }

            std::error_code ec;
            fs::rename(tmp, dest, ec);
            if (ec)
            {
                fs::remove(dest, ec);
                fs::rename(tmp, dest, ec);
                if (ec) { CS_CORE_ERROR("SceneSerializer: atomic rename failed for {0}", path); return false; }
            }
            return true;
        }

        // Crash-safe single-backup rotation (E21): before an existing file is
        // overwritten, copy it to "<path>.bak" (replacing any previous backup).
        // Keeps exactly one backup — the last SUCCESSFULLY saved version — so a
        // crash mid-write (or a bad edit) is always recoverable. No-op on the
        // first save (nothing to back up). A failed copy is non-fatal: it is
        // logged and the save proceeds (a save must never be blocked by backup
        // trouble).
        void RotateBackup(const std::string& path)
        {
            namespace fs = std::filesystem;
            const fs::path dest = fs::u8path(path);
            std::error_code ec;
            if (!fs::exists(dest, ec))
                return;
            const fs::path bak = fs::path(dest).concat(".bak");
            fs::copy_file(dest, bak, fs::copy_options::overwrite_existing, ec);
            if (ec)
                CS_CORE_WARN("SceneSerializer: could not roll backup for {0}: {1}",
                             path, ec.message());
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
            jents.push_back(SerializeEntity(reg, registry, e));

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

        std::vector<PendingLink> pendingHierarchy;

        for (const auto& je : j["entities"])
        {
            UUID id = (je.contains("id") && je["id"].is_string())
                          ? UUID::FromString(je["id"].get<std::string>())
                          : UUID();

            Entity e = scene.CreateEntityWithUUID(id);   // Tag/Transform blocks overwrite defaults
            const entt::entity handle = (entt::entity)e;

            if (je.contains("components") && je["components"].is_object())
                LoadEntityComponents(reg, handle, registry, je["components"], pendingHierarchy, id);
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
        RotateBackup(path);   // E21: keep one crash-safe "<path>.bak"
        return WriteTextAtomic(path, SaveToString(scene));
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

    // -------------------------------------------------------------------------
    // Prefabs (E14)
    // -------------------------------------------------------------------------
    bool SceneSerializer::SavePrefab(Scene& scene, Entity root, const std::string& path)
    {
        if (!root)
        {
            CS_CORE_ERROR("SceneSerializer::SavePrefab: invalid root entity.");
            return false;
        }
        auto& reg      = scene.GetRegistry();
        auto& registry = Reflect::GetRegistry();

        std::vector<entt::entity> order;
        GatherSubtree(scene, (entt::entity)root, order);

        json out;
        out["cosmic_prefab"] = 1;
        out["root"] = reg.get<IDComponent>((entt::entity)root).ID.ToString();

        json jents = json::array();
        for (entt::entity e : order)
            jents.push_back(SerializeEntity(reg, registry, e));
        out["entities"] = std::move(jents);

        return WriteTextAtomic(path, out.dump(2));
    }

    Entity SceneSerializer::InstantiatePrefab(Scene& scene, const std::string& path)
    {
        std::ifstream is(std::filesystem::u8path(path), std::ios::binary);
        if (!is)
        {
            CS_CORE_ERROR("SceneSerializer::InstantiatePrefab: cannot open {0}", path);
            return Entity{};
        }
        std::stringstream ss; ss << is.rdbuf();

        json j = json::parse(ss.str(), nullptr, false);
        if (j.is_discarded() || !j.contains("entities") || !j["entities"].is_array())
        {
            CS_CORE_ERROR("SceneSerializer::InstantiatePrefab: bad prefab '{0}'.", path);
            return Entity{};
        }

        auto& reg      = scene.GetRegistry();
        auto& registry = Reflect::GetRegistry();

        const UUID rootOld = (j.contains("root") && j["root"].is_string())
                                 ? UUID::FromString(j["root"].get<std::string>()) : UUID(0);

        // Pass 1 — fresh entity per block; remember old->new UUID + component load.
        std::unordered_map<uint64_t, UUID> remap;
        std::vector<PendingLink>           pending;   // Owner = NEW id; Children = OLD ids
        UUID rootNew(0);

        for (const auto& je : j["entities"])
        {
            const UUID oldId = (je.contains("id") && je["id"].is_string())
                                   ? UUID::FromString(je["id"].get<std::string>()) : UUID();
            Entity e = scene.CreateEntity();                 // FRESH uuid, indexed
            const UUID newId = e.GetComponent<IDComponent>().ID;
            remap[oldId.Value()] = newId;
            if (oldId.Value() == rootOld.Value())
                rootNew = newId;

            if (je.contains("components") && je["components"].is_object())
                LoadEntityComponents(reg, (entt::entity)e, registry, je["components"], pending, newId);
        }

        // Pass 2 — rebuild internal hierarchy, remapping child ids old->new.
        for (const auto& link : pending)
        {
            Entity owner = scene.FindByUUID(link.Owner);
            if (!owner) continue;
            for (const UUID& childOld : link.Children)
            {
                auto it = remap.find(childOld.Value());
                if (it == remap.end()) continue;
                Entity child = scene.FindByUUID(it->second);
                if (child)
                    scene.SetParent(child, owner, /*keepWorldPose=*/false);
            }
        }

        // Stamp the new root with a PrefabComponent so it can be reverted later.
        Entity rootEntity = rootNew.IsValid() ? scene.FindByUUID(rootNew) : Entity{};
        if (rootEntity)
            rootEntity.GetOrAddComponent<PrefabComponent>().SourcePath = path;
        return rootEntity;
    }

    // -------------------------------------------------------------------------
    // Generic reflected-struct (de)serialization (E17) — .cmat and friends
    // -------------------------------------------------------------------------
    std::string SceneSerializer::SaveReflectedToString(uint32_t typeId, const void* instance)
    {
        const TypeDescriptor* d = Reflect::GetRegistry().Find((entt::id_type)typeId);
        if (!d || !instance)
            return "{}";

        json j = json::object();
        j["cosmic_type"] = d->Name;
        json fields = json::object();
        for (const auto& f : d->Fields)
        {
            if (f.HasFlag(Field_NoSerialize))
                continue;
            fields[f.Name] = SerializeValue(f, f.Get(instance));
        }
        j["fields"] = std::move(fields);
        return j.dump(2);
    }

    bool SceneSerializer::LoadReflectedFromString(uint32_t typeId, void* instance, const std::string& jsonText)
    {
        const TypeDescriptor* d = Reflect::GetRegistry().Find((entt::id_type)typeId);
        if (!d || !instance)
            return false;

        json j = json::parse(jsonText, nullptr, false);
        if (j.is_discarded())
        {
            CS_CORE_ERROR("SceneSerializer: reflected-struct JSON parse failed.");
            return false;
        }

        // Accept either the wrapped { fields:{...} } form or a bare field object.
        const json& fields = (j.contains("fields") && j["fields"].is_object()) ? j["fields"] : j;
        for (const auto& f : d->Fields)
        {
            if (f.HasFlag(Field_NoSerialize))
                continue;
            if (fields.contains(f.Name))
                f.Set(instance, DeserializeValue(f, fields[f.Name]));
        }
        return true;
    }

    bool SceneSerializer::SaveReflectedToFile(uint32_t typeId, const void* instance, const std::string& path)
    {
        return WriteTextAtomic(path, SaveReflectedToString(typeId, instance));
    }

    bool SceneSerializer::LoadReflectedFromFile(uint32_t typeId, void* instance, const std::string& path)
    {
        std::ifstream is(std::filesystem::u8path(path), std::ios::binary);
        if (!is)
        {
            CS_CORE_ERROR("SceneSerializer::LoadReflectedFromFile: cannot open {0}", path);
            return false;
        }
        std::stringstream ss;
        ss << is.rdbuf();
        return LoadReflectedFromString(typeId, instance, ss.str());
    }
}
