// EditorSnapshot.cpp — see EditorSnapshot.h.

#include "EditorSnapshot.h"

#include <unordered_map>

using namespace Cosmic;
using Cosmic::Reflect::TypeDescriptor;

namespace Starforge
{
    namespace
    {
        // Copy every registered component the source entity owns into `dstReg`'s
        // entity, using the E1 descriptor thunks (preserves Ref<> members).
        void CopyReflectedComponents(entt::registry& srcReg, entt::entity src,
                                     entt::registry& dstReg, entt::entity dst)
        {
            auto& registry = Reflect::GetRegistry();
            for (const TypeDescriptor* desc : registry.ComponentsOf(srcReg, src))
            {
                void* srcPtr = desc->Get(srcReg, src);
                if (desc->Copy)
                    desc->Copy(dstReg, dst, srcPtr);   // srcPtr may be null for empties
            }
        }

        std::string TagOf(entt::registry& reg, entt::entity e)
        {
            if (auto* t = reg.try_get<TagComponent>(e))
                return t->Tag;
            return "Entity";
        }

        // Depth-first preorder over the RelationshipComponent tree (parent first,
        // children in stored order), collecting source handles.
        void GatherSubtree(Scene& scene, Entity node, std::vector<Entity>& out)
        {
            if (!node) return;
            out.push_back(node);
            auto& reg = scene.GetRegistry();
            if (auto* rel = reg.try_get<RelationshipComponent>((entt::entity)node))
            {
                // Copy the child list first — SetParent during later ops can
                // mutate it, but capture is read-only so a direct walk is safe.
                for (UUID childId : rel->Children)
                {
                    Entity child = scene.FindByUUID(childId);
                    if (child) GatherSubtree(scene, child, out);
                }
            }
        }
    }

    Snapshot Snapshot::Capture(Scene& scene, Entity root)
    {
        Snapshot snap;
        if (!root)
            return snap;

        std::vector<Entity> order;
        GatherSubtree(scene, root, order);

        auto& sceneReg = scene.GetRegistry();
        snap.m_RootId = root.HasComponent<IDComponent>()
            ? (uint64_t)root.GetComponent<IDComponent>().ID : 0;

        for (Entity e : order)
        {
            entt::entity holdE = snap.m_Hold.create();
            snap.m_Order.push_back(holdE);

            // Identity + relationship (structural — not in the reflect registry).
            if (auto* id = sceneReg.try_get<IDComponent>((entt::entity)e))
                snap.m_Hold.emplace<IDComponent>(holdE, *id);
            if (auto* rel = sceneReg.try_get<RelationshipComponent>((entt::entity)e))
                snap.m_Hold.emplace<RelationshipComponent>(holdE, *rel);

            // Every reflected component, copied whole (Ref<> included).
            CopyReflectedComponents(sceneReg, (entt::entity)e, snap.m_Hold, holdE);
        }
        return snap;
    }

    Entity Snapshot::Restore(Scene& scene) const
    {
        Entity restoredRoot;

        // Pass 1: recreate entities with their original UUIDs + components.
        for (entt::entity holdE : m_Order)
        {
            entt::registry& hold = const_cast<entt::registry&>(m_Hold);
            const UUID id = hold.get<IDComponent>(holdE).ID;
            Entity e = scene.CreateEntityWithUUID(id, TagOf(hold, holdE));
            CopyReflectedComponents(hold, holdE, scene.GetRegistry(), (entt::entity)e);

            if ((uint64_t)id == m_RootId)
                restoredRoot = e;
        }

        // Pass 2: rebuild parent links in preorder (preserves sibling order).
        for (entt::entity holdE : m_Order)
        {
            entt::registry& hold = const_cast<entt::registry&>(m_Hold);
            auto* rel = hold.try_get<RelationshipComponent>(holdE);
            if (!rel || !rel->Parent.IsValid())
                continue;
            const UUID childId  = hold.get<IDComponent>(holdE).ID;
            Entity child  = scene.FindByUUID(childId);
            Entity parent = scene.FindByUUID(rel->Parent);
            if (child && parent)
                scene.SetParent(child, parent, /*keepWorldPose=*/false);
        }
        return restoredRoot;
    }

    Entity Snapshot::Instantiate(Scene& scene) const
    {
        entt::registry& hold = const_cast<entt::registry&>(m_Hold);
        std::unordered_map<uint64_t, Entity> remap;   // old UUID -> new entity

        // Pass 1: fresh entities + components.
        for (entt::entity holdE : m_Order)
        {
            const UUID oldId = hold.get<IDComponent>(holdE).ID;
            Entity e = scene.CreateEntity(TagOf(hold, holdE));   // fresh UUID
            CopyReflectedComponents(hold, holdE, scene.GetRegistry(), (entt::entity)e);
            remap[(uint64_t)oldId] = e;
        }

        // Pass 2: parent links. Internal links remap; the root keeps the original
        // parent (outside the snapshot) so the clone sits beside the original.
        for (entt::entity holdE : m_Order)
        {
            auto* rel = hold.try_get<RelationshipComponent>(holdE);
            if (!rel || !rel->Parent.IsValid())
                continue;
            const UUID oldId = hold.get<IDComponent>(holdE).ID;
            Entity child = remap[(uint64_t)oldId];

            Entity parent;
            auto it = remap.find((uint64_t)rel->Parent);
            parent = (it != remap.end()) ? it->second : scene.FindByUUID(rel->Parent);
            if (child && parent)
                scene.SetParent(child, parent, /*keepWorldPose=*/false);
        }

        auto rootIt = remap.find(m_RootId);
        return rootIt != remap.end() ? rootIt->second : Entity{};
    }
}
