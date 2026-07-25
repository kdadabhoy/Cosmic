// commands/EditorCommands.cpp — see EditorCommands.h.

#include "commands/EditorCommands.h"
#include "EditorContext.h"
#include "EditorSnapshot.h"

#ifndef COSMIC_2D_ONLY
#include "voxel/VoxelVolume.h"   // V4 — undoable voxel edits
#endif

#include <memory>
#include <vector>

using namespace Cosmic;
using Cosmic::Reflect::FieldValue;
using Cosmic::Reflect::FieldDescriptor;
using Cosmic::Reflect::TypeDescriptor;

namespace Starforge
{
    namespace
    {
        Entity Resolve(EditorContext& ctx, uint64_t uuid)
        {
            return ctx.Scene ? ctx.Scene->FindByUUID(UUID(uuid)) : Entity{};
        }

        uint64_t IdOf(Entity e)
        {
            return e.HasComponent<IDComponent>() ? (uint64_t)e.GetComponent<IDComponent>().ID : 0;
        }

        // ----- one reflected-field change ---------------------------------
        class FieldEditCommand : public ICommand
        {
        public:
            FieldEditCommand(EditorContext& ctx, uint64_t uuid, entt::id_type typeId,
                             std::string field, FieldValue before, FieldValue after, std::string label)
                : m_Ctx(&ctx), m_Uuid(uuid), m_TypeId(typeId), m_Field(std::move(field)),
                  m_Before(std::move(before)), m_After(std::move(after)), m_Label(std::move(label)) {}

            void Do() override   { Apply(m_After); }
            void Undo() override { Apply(m_Before); }
            std::string Name() const override { return m_Label; }
            std::string MergeKey() const override
            {
                return "f:" + std::to_string(m_Uuid) + ":" +
                       std::to_string(m_TypeId) + ":" + m_Field;
            }
            bool TryMerge(const ICommand& next) override
            {
                const auto* n = dynamic_cast<const FieldEditCommand*>(&next);
                if (!n || n->MergeKey() != MergeKey()) return false;
                m_After = n->m_After;
                return true;
            }

        private:
            void Apply(const FieldValue& v)
            {
                Entity e = Resolve(*m_Ctx, m_Uuid);
                if (!e) return;
                const TypeDescriptor* d = Reflect::GetRegistry().Find(m_TypeId);
                if (!d) return;
                void* comp = d->Get(m_Ctx->Scene->GetRegistry(), (entt::entity)e);
                if (!comp) return;
                const FieldDescriptor* f = d->FindField(m_Field);
                if (!f) return;
                f->Set(comp, v);
                m_Ctx->MarkDirty();
            }

            EditorContext* m_Ctx;
            uint64_t       m_Uuid;
            entt::id_type  m_TypeId;
            std::string    m_Field;
            FieldValue     m_Before, m_After;
            std::string    m_Label;
        };

        // ----- a group of commands treated as one undo step ---------------
        class BatchCommand : public ICommand
        {
        public:
            BatchCommand(std::string label, std::string mergeKey)
                : m_Label(std::move(label)), m_Merge(std::move(mergeKey)) {}

            void Add(Scope<ICommand> c) { m_Cmds.push_back(std::move(c)); }
            bool Empty() const { return m_Cmds.empty(); }

            void Do() override   { for (auto& c : m_Cmds) c->Do(); }
            void Undo() override { for (auto it = m_Cmds.rbegin(); it != m_Cmds.rend(); ++it) (*it)->Undo(); }
            std::string Name() const override { return m_Label; }
            std::string MergeKey() const override { return m_Merge; }
            bool TryMerge(const ICommand& next) override
            {
                const auto* n = dynamic_cast<const BatchCommand*>(&next);
                if (!n || n->MergeKey() != MergeKey() || n->m_Cmds.size() != m_Cmds.size())
                    return false;
                for (size_t i = 0; i < m_Cmds.size(); ++i)
                    if (!m_Cmds[i]->TryMerge(*n->m_Cmds[i]))
                        return false;
                return true;
            }

        private:
            std::vector<Scope<ICommand>> m_Cmds;
            std::string m_Label, m_Merge;
        };

        // ----- create/duplicate (Undo removes, Redo restores) -------------
        class RestoreCommand : public ICommand
        {
        public:
            RestoreCommand(EditorContext& ctx, Snapshot snap, uint64_t root, std::string label)
                : m_Ctx(&ctx), m_Snap(std::move(snap)), m_Root(root), m_Label(std::move(label)) {}

            void Do() override   // redo: bring the subtree (back) into existence
            {
                Entity root = m_Snap.Restore(*m_Ctx->Scene);
                if (root) m_Ctx->SelectOnly(root);
                m_Ctx->MarkDirty();
            }
            void Undo() override
            {
                Entity e = Resolve(*m_Ctx, m_Root);
                if (e) m_Ctx->Scene->DestroyEntity(e, true);
                m_Ctx->ValidateSelection();
                m_Ctx->MarkDirty();
            }
            std::string Name() const override { return m_Label; }

        private:
            EditorContext* m_Ctx;
            Snapshot       m_Snap;
            uint64_t       m_Root;
            std::string    m_Label;
        };

        // ----- destroy (Do removes, Undo restores) ------------------------
        class DestroyCommand : public ICommand
        {
        public:
            DestroyCommand(EditorContext& ctx, Snapshot snap, uint64_t root, std::string label)
                : m_Ctx(&ctx), m_Snap(std::move(snap)), m_Root(root), m_Label(std::move(label)) {}

            void Do() override
            {
                Entity e = Resolve(*m_Ctx, m_Root);
                if (e) m_Ctx->Scene->DestroyEntity(e, true);
                m_Ctx->ValidateSelection();
                m_Ctx->MarkDirty();
            }
            void Undo() override
            {
                Entity root = m_Snap.Restore(*m_Ctx->Scene);
                if (root) m_Ctx->SelectOnly(root);
                m_Ctx->MarkDirty();
            }
            std::string Name() const override { return m_Label; }

        private:
            EditorContext* m_Ctx;
            Snapshot       m_Snap;
            uint64_t       m_Root;
            std::string    m_Label;
        };

        // ----- reparent ----------------------------------------------------
        class ReparentCommand : public ICommand
        {
        public:
            ReparentCommand(EditorContext& ctx, uint64_t child, uint64_t oldP, uint64_t newP,
                            TransformComponent beforeLocal)
                : m_Ctx(&ctx), m_Child(child), m_Old(oldP), m_New(newP), m_Before(beforeLocal) {}

            void Do() override
            {
                Entity c = Resolve(*m_Ctx, m_Child);
                Entity p = Resolve(*m_Ctx, m_New);
                if (c) m_Ctx->Scene->SetParent(c, p, /*keepWorldPose=*/true);
                m_Ctx->MarkDirty();
            }
            void Undo() override
            {
                Entity c = Resolve(*m_Ctx, m_Child);
                Entity oldP = Resolve(*m_Ctx, m_Old);
                if (c)
                {
                    c.GetComponent<TransformComponent>() = m_Before;
                    m_Ctx->Scene->SetParent(c, oldP, /*keepWorldPose=*/false);
                }
                m_Ctx->MarkDirty();
            }
            std::string Name() const override { return "Reparent"; }

        private:
            EditorContext*     m_Ctx;
            uint64_t           m_Child, m_Old, m_New;
            TransformComponent m_Before;
        };

        // ----- add / remove component -------------------------------------
        class AddComponentCommand : public ICommand
        {
        public:
            AddComponentCommand(EditorContext& ctx, uint64_t uuid, entt::id_type typeId, std::string label)
                : m_Ctx(&ctx), m_Uuid(uuid), m_TypeId(typeId), m_Label(std::move(label)) {}

            void Do() override
            {
                Entity e = Resolve(*m_Ctx, m_Uuid);
                const TypeDescriptor* d = Reflect::GetRegistry().Find(m_TypeId);
                if (e && d && d->Add) d->Add(m_Ctx->Scene->GetRegistry(), (entt::entity)e);
                m_Ctx->MarkDirty();
            }
            void Undo() override
            {
                Entity e = Resolve(*m_Ctx, m_Uuid);
                const TypeDescriptor* d = Reflect::GetRegistry().Find(m_TypeId);
                if (e && d && d->Remove) d->Remove(m_Ctx->Scene->GetRegistry(), (entt::entity)e);
                m_Ctx->MarkDirty();
            }
            std::string Name() const override { return m_Label; }

        private:
            EditorContext* m_Ctx;
            uint64_t       m_Uuid;
            entt::id_type  m_TypeId;
            std::string    m_Label;
        };

        class RemoveComponentCommand : public ICommand
        {
        public:
            RemoveComponentCommand(EditorContext& ctx, uint64_t uuid, entt::id_type typeId, std::string label)
                : m_Ctx(&ctx), m_Uuid(uuid), m_TypeId(typeId), m_Label(std::move(label))
            {
                // Snapshot the component value into a detached registry so undo
                // restores it whole (Ref<> members included).
                Entity e = Resolve(ctx, uuid);
                const TypeDescriptor* d = Reflect::GetRegistry().Find(typeId);
                if (e && d && d->Copy)
                {
                    m_HoldE = m_Hold.create();
                    void* src = d->Get(ctx.Scene->GetRegistry(), (entt::entity)e);
                    d->Copy(m_Hold, m_HoldE, src);
                }
            }

            void Do() override
            {
                Entity e = Resolve(*m_Ctx, m_Uuid);
                const TypeDescriptor* d = Reflect::GetRegistry().Find(m_TypeId);
                if (e && d && d->Remove) d->Remove(m_Ctx->Scene->GetRegistry(), (entt::entity)e);
                m_Ctx->MarkDirty();
            }
            void Undo() override
            {
                Entity e = Resolve(*m_Ctx, m_Uuid);
                const TypeDescriptor* d = Reflect::GetRegistry().Find(m_TypeId);
                if (e && d && d->Copy && m_HoldE != entt::null)
                    d->Copy(m_Ctx->Scene->GetRegistry(), (entt::entity)e, d->Get(m_Hold, m_HoldE));
                m_Ctx->MarkDirty();
            }
            std::string Name() const override { return m_Label; }

        private:
            EditorContext* m_Ctx;
            uint64_t       m_Uuid;
            entt::id_type  m_TypeId;
            std::string    m_Label;
            entt::registry m_Hold;
            entt::entity   m_HoldE = entt::null;
        };

        // ----- gizmo transform edit (already applied live) ----------------
        class TransformEditCommand : public ICommand
        {
        public:
            TransformEditCommand(EditorContext& ctx, uint64_t uuid,
                                 TransformComponent before, TransformComponent after)
                : m_Ctx(&ctx), m_Uuid(uuid), m_Before(before), m_After(after) {}

            void Do() override   { Set(m_After); }
            void Undo() override { Set(m_Before); }
            std::string Name() const override { return "Transform"; }
            std::string MergeKey() const override { return "xform:" + std::to_string(m_Uuid); }
            bool TryMerge(const ICommand& next) override
            {
                const auto* n = dynamic_cast<const TransformEditCommand*>(&next);
                if (!n || n->MergeKey() != MergeKey()) return false;
                m_After = n->m_After;
                return true;
            }

        private:
            void Set(const TransformComponent& t)
            {
                Entity e = Resolve(*m_Ctx, m_Uuid);
                if (e && e.HasComponent<TransformComponent>())
                {
                    e.GetComponent<TransformComponent>() = t;
                    m_Ctx->MarkDirty();
                }
            }
            EditorContext*     m_Ctx;
            uint64_t           m_Uuid;
            TransformComponent m_Before, m_After;
        };
    } // namespace

    // ======================================================================
    // Free functions
    // ======================================================================
    void Commands::SetField(EditorContext& ctx, Entity e, entt::id_type typeId,
                            const std::string& field, FieldValue after)
    {
        if (!ctx.Scene || !e) return;
        const TypeDescriptor* d = Reflect::GetRegistry().Find(typeId);
        if (!d) return;
        void* comp = d->Get(ctx.Scene->GetRegistry(), (entt::entity)e);
        const FieldDescriptor* f = d->FindField(field);
        if (!comp || !f) return;
        FieldValue before = f->Get(comp);
        ctx.Commands.Execute(std::make_unique<FieldEditCommand>(
            ctx, IdOf(e), typeId, field, before, after, "Edit " + field));
    }

    void Commands::CommitFieldEditFor(EditorContext& ctx, Entity target, const std::string& label,
                                      entt::id_type typeId, const std::string& field,
                                      FieldValue before, FieldValue after)
    {
        if (!ctx.Scene || !target) return;
        const TypeDescriptor* d = Reflect::GetRegistry().Find(typeId);
        if (!d || !d->FindField(field)) return;

        // T15 — during Play, the value is already applied live by the widget; edits
        // are transient (the Stop snapshot-restore discards them), so record NOTHING
        // (no undo entry, no dirty flag).
        if (ctx.Playing)
            return;

        ctx.MarkDirty();
        // The value was already applied live by the panel widget; Push (not Execute).
        ctx.Commands.Push(std::make_unique<FieldEditCommand>(
            ctx, IdOf(target), typeId, field, std::move(before), std::move(after), label));
        ctx.Commands.SetMergeBarrier();
    }

    void Commands::CommitFieldEdit(EditorContext& ctx, const std::string& label,
                                   entt::id_type typeId, const std::string& field,
                                   FieldValue primaryBefore, FieldValue after)
    {
        if (!ctx.Scene) return;
        const TypeDescriptor* d = Reflect::GetRegistry().Find(typeId);
        const FieldDescriptor* f = d ? d->FindField(field) : nullptr;
        if (!d || !f) return;

        Entity primary = ctx.PrimaryEntity();
        if (!primary) return;

        // T15 — during Play, still fan the value across the selection (so multi-edit
        // works live) but record NO undo entry and don't dirty the scene: these edits
        // are transient and vanish on Stop's snapshot-restore.
        if (ctx.Playing)
        {
            for (entt::entity h : ctx.Selection)
            {
                if (h == (entt::entity)primary) continue;   // primary already applied by the widget
                if (void* comp = d->Get(ctx.Scene->GetRegistry(), h))
                    f->Set(comp, after);
            }
            return;
        }

        auto batch = std::make_unique<BatchCommand>(label,
                        "batch:" + std::to_string(typeId) + ":" + field);
        batch->Add(std::make_unique<FieldEditCommand>(
            ctx, IdOf(primary), typeId, field, primaryBefore, after, label));

        // Fan the new value out to the rest of the selection (apply now).
        for (entt::entity h : ctx.Selection)
        {
            if (h == (entt::entity)primary) continue;
            Entity e(h, ctx.Scene.get());
            if (!e) continue;
            void* comp = d->Get(ctx.Scene->GetRegistry(), h);
            if (!comp) continue;
            FieldValue before = f->Get(comp);
            f->Set(comp, after);
            batch->Add(std::make_unique<FieldEditCommand>(
                ctx, IdOf(e), typeId, field, before, after, label));
        }

        ctx.MarkDirty();
        if (!batch->Empty())
        {
            ctx.Commands.Push(std::move(batch));
            // Each completed inspector edit is its own undo step (the panel pushes
            // once per edit, so there is no continuous drag to coalesce).
            ctx.Commands.SetMergeBarrier();
        }
    }

    Entity Commands::Create(EditorContext& ctx, const std::string& name, Entity parent,
                            std::function<void(Entity)> build)
    {
        if (!ctx.Scene) return {};
        Entity e = ctx.Scene->CreateEntity(name);
        if (parent) ctx.Scene->SetParent(e, parent, /*keepWorldPose=*/false);
        if (build)  build(e);
        ctx.SelectOnly(e);
        ctx.MarkDirty();

        Snapshot snap = Snapshot::Capture(*ctx.Scene, e);
        ctx.Commands.Push(std::make_unique<RestoreCommand>(
            ctx, std::move(snap), IdOf(e), "Create " + name));
        return e;
    }

    void Commands::RecordSpawn(EditorContext& ctx, Entity root, const std::string& label)
    {
        if (!ctx.Scene || !root) return;
        ctx.SelectOnly(root);
        ctx.MarkDirty();
        Snapshot snap = Snapshot::Capture(*ctx.Scene, root);
        ctx.Commands.Push(std::make_unique<RestoreCommand>(
            ctx, std::move(snap), IdOf(root), label));
    }

    // ----- MeshRenderer material commands (K13 / M5) — 3D only --------------
    // Both operate on MeshRendererComponent, which the 2D build does not have.
    // A 2D material assignment is a reflected SpriteRenderer field write and
    // already goes through the generic Commands::SetField path.
#ifndef COSMIC_2D_ONLY
    namespace
    {
        // K13 — material drop: path + resolved asset move together so undo/redo
        // are visually exact (the string-only reflected write leaves the old
        // Ref<MaterialAsset> live).
        class AssignMaterialCommand : public ICommand
        {
        public:
            AssignMaterialCommand(EditorContext& ctx, uint64_t uuid,
                                  std::string before, std::string after)
                : m_Ctx(&ctx), m_Uuid(uuid), m_Before(std::move(before)), m_After(std::move(after)) {}

            void Do() override   { Apply(m_After); }
            void Undo() override { Apply(m_Before); }
            std::string Name() const override { return "Assign Material"; }

        private:
            void Apply(const std::string& path)
            {
                Entity e = Resolve(*m_Ctx, m_Uuid);
                if (!e || !e.HasComponent<MeshRendererComponent>()) return;
                auto& mr = e.GetComponent<MeshRendererComponent>();
                mr.MaterialPath         = path;
                mr.MaterialAsset        = path.empty() ? nullptr : AssetLibrary::GetMaterial(path);
                mr.MaterialPathResolved = true;   // the sync must not overwrite this
                m_Ctx->MarkDirty();
            }

            EditorContext* m_Ctx;
            uint64_t       m_Uuid;
            std::string    m_Before, m_After;
        };
    }

    void Commands::AssignMaterial(EditorContext& ctx, Entity e, const std::string& vfsPath)
    {
        if (!ctx.Scene || !e || !e.HasComponent<MeshRendererComponent>()) return;
        const std::string before = e.GetComponent<MeshRendererComponent>().MaterialPath;
        if (before == vfsPath) return;
        ctx.Commands.Execute(std::make_unique<AssignMaterialCommand>(ctx, IdOf(e), before, vfsPath));
    }

    namespace
    {
        // M5 — captures the whole MaterialPaths vector before/after (a slot edit is
        // rare, the vector tiny) so undo restores it exactly; clears the resolved
        // flag so the next SyncPrimitiveMeshes rebuilds MaterialAssets.
        class MaterialSlotCommand : public ICommand
        {
        public:
            MaterialSlotCommand(EditorContext& ctx, uint64_t uuid,
                                std::vector<std::string> before, std::vector<std::string> after)
                : m_Ctx(&ctx), m_Uuid(uuid), m_Before(std::move(before)), m_After(std::move(after)) {}

            void Do() override   { Apply(m_After); }
            void Undo() override { Apply(m_Before); }
            std::string Name() const override { return "Edit Material Slot"; }

        private:
            void Apply(const std::vector<std::string>& paths)
            {
                Entity e = Resolve(*m_Ctx, m_Uuid);
                if (!e || !e.HasComponent<MeshRendererComponent>()) return;
                auto& mr = e.GetComponent<MeshRendererComponent>();
                mr.MaterialPaths          = paths;
                mr.MaterialPathsResolved  = false;   // re-resolve on the next sync
                m_Ctx->MarkDirty();
            }

            EditorContext*           m_Ctx;
            uint64_t                 m_Uuid;
            std::vector<std::string> m_Before, m_After;
        };
    }

    void Commands::SetMaterialSlot(EditorContext& ctx, Entity e, size_t slot,
                                   const std::string& vfsPath)
    {
        if (!ctx.Scene || !e || !e.HasComponent<MeshRendererComponent>()) return;
        std::vector<std::string> before = e.GetComponent<MeshRendererComponent>().MaterialPaths;
        std::vector<std::string> after  = before;
        if (after.size() <= slot)
            after.resize(slot + 1);
        if (after[slot] == vfsPath)
            return;   // no-op
        after[slot] = vfsPath;
        // Trailing-empty trim: an all-empty vector serializes as absent (compat).
        while (!after.empty() && after.back().empty())
            after.pop_back();
        ctx.Commands.Execute(std::make_unique<MaterialSlotCommand>(
            ctx, IdOf(e), std::move(before), std::move(after)));
    }
#endif   // COSMIC_2D_ONLY — AssignMaterial + SetMaterialSlot

    Entity Commands::Duplicate(EditorContext& ctx, Entity src)
    {
        if (!ctx.Scene || !src) return {};
        Snapshot srcSnap = Snapshot::Capture(*ctx.Scene, src);
        Entity clone = srcSnap.Instantiate(*ctx.Scene);
        if (!clone) return {};
        ctx.SelectOnly(clone);
        ctx.MarkDirty();

        Snapshot cloneSnap = Snapshot::Capture(*ctx.Scene, clone);
        ctx.Commands.Push(std::make_unique<RestoreCommand>(
            ctx, std::move(cloneSnap), IdOf(clone), "Duplicate"));
        return clone;
    }

    void Commands::Destroy(EditorContext& ctx, Entity e)
    {
        if (!ctx.Scene || !e) return;
        std::string name = e.HasComponent<TagComponent>()
            ? e.GetComponent<TagComponent>().Tag : std::string("Entity");
        Snapshot snap = Snapshot::Capture(*ctx.Scene, e);
        ctx.Commands.Execute(std::make_unique<DestroyCommand>(
            ctx, std::move(snap), IdOf(e), "Delete " + name));
    }

    void Commands::Reparent(EditorContext& ctx, Entity child, Entity newParent)
    {
        if (!ctx.Scene || !child) return;

        // Refuse cycles up-front (SetParent also refuses, but we avoid recording
        // a no-op command).
        if (newParent && (newParent == child || ctx.Scene->IsAncestor(child, newParent)))
            return;

        uint64_t oldU = 0;
        if (auto* rel = ctx.Scene->GetRegistry().try_get<RelationshipComponent>((entt::entity)child))
            oldU = (uint64_t)rel->Parent;
        if (oldU == (newParent ? IdOf(newParent) : 0))
            return;   // no change

        TransformComponent before = child.GetComponent<TransformComponent>();
        ctx.Commands.Execute(std::make_unique<ReparentCommand>(
            ctx, IdOf(child), oldU, newParent ? IdOf(newParent) : 0, before));
    }

    void Commands::AddComponent(EditorContext& ctx, Entity e, entt::id_type typeId)
    {
        if (!ctx.Scene || !e) return;
        const TypeDescriptor* d = Reflect::GetRegistry().Find(typeId);
        if (!d || (d->Has && d->Has(ctx.Scene->GetRegistry(), (entt::entity)e)))
            return;   // unknown, or already present
        ctx.Commands.Execute(std::make_unique<AddComponentCommand>(
            ctx, IdOf(e), typeId, "Add " + d->Name));
    }

    void Commands::RemoveComponent(EditorContext& ctx, Entity e, entt::id_type typeId)
    {
        if (!ctx.Scene || !e) return;
        const TypeDescriptor* d = Reflect::GetRegistry().Find(typeId);
        if (!d || (d->Has && !d->Has(ctx.Scene->GetRegistry(), (entt::entity)e)))
            return;
        ctx.Commands.Execute(std::make_unique<RemoveComponentCommand>(
            ctx, IdOf(e), typeId, "Remove " + d->Name));
    }

    void Commands::CommitTransform(EditorContext& ctx, Entity e, const TransformComponent& before)
    {
        if (!ctx.Scene || !e || !e.HasComponent<TransformComponent>()) return;
        TransformComponent after = e.GetComponent<TransformComponent>();
        ctx.Commands.Push(std::make_unique<TransformEditCommand>(ctx, IdOf(e), before, after));
    }

    // ----- voxel edits (V4) — a coalesced brush stroke = one undo step -------
#ifndef COSMIC_2D_ONLY
    namespace
    {
        class VoxelEditCommand : public ICommand
        {
        public:
            struct Op { glm::ivec3 V; uint16_t Old; uint16_t New; };

            VoxelEditCommand(EditorContext& ctx, uint64_t uuid, int stroke, Op op)
                : m_Ctx(&ctx), m_Uuid(uuid), m_Stroke(stroke) { m_Ops.push_back(op); }

            void Do() override
            {
                if (VoxelVolume* v = Vol())
                    for (const Op& o : m_Ops) v->Set(o.V, o.New);
                if (m_Ctx) m_Ctx->MarkDirty();
            }
            void Undo() override
            {
                if (VoxelVolume* v = Vol())
                    for (auto it = m_Ops.rbegin(); it != m_Ops.rend(); ++it) v->Set(it->V, it->Old);
                if (m_Ctx) m_Ctx->MarkDirty();
            }
            std::string Name() const override { return "Voxel Edit"; }
            std::string MergeKey() const override
            {
                return "voxel:" + std::to_string(m_Uuid) + ":" + std::to_string(m_Stroke);
            }
            bool TryMerge(const ICommand& next) override
            {
                const auto* n = dynamic_cast<const VoxelEditCommand*>(&next);
                if (!n || n->MergeKey() != MergeKey()) return false;
                m_Ops.insert(m_Ops.end(), n->m_Ops.begin(), n->m_Ops.end());
                return true;
            }

        private:
            VoxelVolume* Vol() const
            {
                Entity e = Resolve(*m_Ctx, m_Uuid);
                if (!e || !e.HasComponent<VoxelVolumeComponent>()) return nullptr;
                return e.GetComponent<VoxelVolumeComponent>().Volume.get();
            }

            EditorContext*  m_Ctx;
            uint64_t        m_Uuid;
            int             m_Stroke;
            std::vector<Op> m_Ops;
        };
    }

    void Commands::VoxelEdit(EditorContext& ctx, Entity e, const glm::ivec3& voxel,
                             uint16_t newId, int stroke)
    {
        if (!ctx.Scene || !e || !e.HasComponent<VoxelVolumeComponent>()) return;
        VoxelVolume* vol = e.GetComponent<VoxelVolumeComponent>().Volume.get();
        if (!vol) return;
        const uint16_t oldId = vol->Get(voxel);
        if (oldId == newId) return;

        vol->Set(voxel, newId);   // apply live, then record (Push, not Execute)
        ctx.Commands.Push(std::make_unique<VoxelEditCommand>(
            ctx, IdOf(e), stroke, VoxelEditCommand::Op{ voxel, oldId, newId }));
    }
#endif   // COSMIC_2D_ONLY — VoxelEdit

    // ----- tilemap edits (U4) — a coalesced paint stroke = one undo step ------
    namespace
    {
        class TileEditCommand : public ICommand
        {
        public:
            struct Op { uint32_t Cell; uint16_t Old; uint16_t New; };

            TileEditCommand(EditorContext& ctx, uint64_t uuid, int stroke,
                            std::vector<Op> ops, std::string label)
                : m_Ctx(&ctx), m_Uuid(uuid), m_Stroke(stroke),
                  m_Ops(std::move(ops)), m_Label(std::move(label)) {}

            void Do() override
            {
                if (TilemapComponent* tm = Map())
                    for (const Op& o : m_Ops)
                        if (o.Cell < tm->Cells.size()) tm->Cells[o.Cell] = o.New;
                if (m_Ctx) m_Ctx->MarkDirty();
            }
            void Undo() override
            {
                if (TilemapComponent* tm = Map())
                    for (auto it = m_Ops.rbegin(); it != m_Ops.rend(); ++it)
                        if (it->Cell < tm->Cells.size()) tm->Cells[it->Cell] = it->Old;
                if (m_Ctx) m_Ctx->MarkDirty();
            }
            std::string Name() const override { return m_Label; }
            std::string MergeKey() const override
            {
                // Batch fills use stroke -1: empty key = never coalesced.
                if (m_Stroke < 0) return {};
                return "tile:" + std::to_string(m_Uuid) + ":" + std::to_string(m_Stroke);
            }
            bool TryMerge(const ICommand& next) override
            {
                const auto* n = dynamic_cast<const TileEditCommand*>(&next);
                if (!n || MergeKey().empty() || n->MergeKey() != MergeKey()) return false;
                m_Ops.insert(m_Ops.end(), n->m_Ops.begin(), n->m_Ops.end());
                return true;
            }

        private:
            TilemapComponent* Map() const
            {
                Entity e = Resolve(*m_Ctx, m_Uuid);
                if (!e || !e.HasComponent<TilemapComponent>()) return nullptr;
                TilemapComponent& tm = e.GetComponent<TilemapComponent>();
                tm.EnsureCells();
                return &tm;
            }

            EditorContext*  m_Ctx;
            uint64_t        m_Uuid;
            int             m_Stroke;
            std::vector<Op> m_Ops;
            std::string     m_Label;
        };
    }

    void Commands::TileEdit(EditorContext& ctx, Entity e, int x, int y,
                            uint16_t value, int stroke)
    {
        if (!ctx.Scene || !e || !e.HasComponent<TilemapComponent>()) return;
        TilemapComponent& tm = e.GetComponent<TilemapComponent>();
        tm.EnsureCells();
        if (!tm.InBounds(x, y)) return;
        const uint32_t cell = (uint32_t)(y * tm.GridW + x);
        const uint16_t old  = tm.Cells[cell];
        if (old == value) return;

        tm.Cells[cell] = value;   // apply live, then record (Push, not Execute)
        ctx.Commands.Push(std::make_unique<TileEditCommand>(
            ctx, IdOf(e), stroke,
            std::vector<TileEditCommand::Op>{ { cell, old, value } }, "Paint Tiles"));
    }

    void Commands::TileEditRun(EditorContext& ctx, Entity e,
                               const std::vector<std::pair<uint32_t, uint16_t>>& writes,
                               const char* label)
    {
        if (!ctx.Scene || !e || !e.HasComponent<TilemapComponent>() || writes.empty()) return;
        TilemapComponent& tm = e.GetComponent<TilemapComponent>();
        tm.EnsureCells();

        std::vector<TileEditCommand::Op> ops;
        ops.reserve(writes.size());
        for (const auto& [cell, value] : writes)
        {
            if (cell >= tm.Cells.size() || tm.Cells[cell] == value) continue;
            ops.push_back({ cell, tm.Cells[cell], value });
            tm.Cells[cell] = value;   // apply live
        }
        if (ops.empty()) return;

        ctx.Commands.Push(std::make_unique<TileEditCommand>(
            ctx, IdOf(e), /*stroke=*/-1, std::move(ops), label ? label : "Fill Tiles"));
    }
}
