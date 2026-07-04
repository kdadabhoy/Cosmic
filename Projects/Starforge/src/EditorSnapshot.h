#pragma once

// EditorSnapshot.h
//
// ============================================================================
// Starforge — reflection-driven subtree snapshot (E7 backbone).
// ============================================================================
//
// Captures an entity + its descendants into a detached entt::registry using the
// E1 reflection descriptors' type-erased Copy/Get thunks. Because it copies
// whole components (not just reflected scalar fields), it preserves non-reflected
// members like MeshRendererComponent::MeshAsset (Ref<Mesh>) that E1 does not yet
// surface as fields — so delete/undo and duplicate keep geometry intact.
//
//   Restore()     recreates the subtree with the SAME UUIDs (undo of a delete /
//                 redo of a create), rebuilding parent/child links + order.
//   Instantiate() clones it with FRESH UUIDs (Ctrl+D duplicate) under the same
//                 parent as the original root, and returns the new root.
//
// Structural components (IDComponent / RelationshipComponent) are handled
// explicitly; unknown opaque blocks (E2 OpaqueComponentsComponent) are NOT
// carried by this app-side snapshot — v1 limitation, documented.
// ============================================================================

#include <Cosmic.h>
#include <entt/entt.hpp>

#include <cstdint>
#include <vector>

namespace Starforge
{
    class Snapshot
    {
    public:
        Snapshot() = default;
        Snapshot(Snapshot&&) = default;
        Snapshot& operator=(Snapshot&&) = default;
        Snapshot(const Snapshot&) = delete;
        Snapshot& operator=(const Snapshot&) = delete;

        // Capture `root` and everything under it. Empty snapshot if root invalid.
        static Snapshot Capture(Cosmic::Scene& scene, Cosmic::Entity root);

        bool     Empty()  const { return m_Order.empty(); }
        uint64_t RootId() const { return m_RootId; }

        // Recreate the subtree with identical UUIDs. Returns the recreated root.
        Cosmic::Entity Restore(Cosmic::Scene& scene) const;

        // Clone the subtree with fresh UUIDs, parented like the original root.
        Cosmic::Entity Instantiate(Cosmic::Scene& scene) const;

    private:
        entt::registry            m_Hold;    // detached copies (component-complete)
        std::vector<entt::entity> m_Order;   // parent-before-children
        uint64_t                  m_RootId = 0;
    };
}
