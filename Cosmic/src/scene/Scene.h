#pragma once
// Scene.h

#include "core/Core.h"
#include "core/UUID.h"
#include "scene/System.h"
#include "jobs/ParallelSystem.h"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

namespace Cosmic
{
	class Entity;          // Forward declaration
	class Camera;             // Forward declaration (OnRender3D takes any camera)
	class OrthographicCamera; // Forward declaration
	class Material;        // Forward declaration (used only as a bucket key pointer)

	class COSMIC_API Scene
	{
	public:
		Scene();
		~Scene() = default;

		/** @brief Static factory helper to match unified engine smart pointer instantiation rules. */
		template<typename... Args>
		static Ref<Scene> Create(Args&&... args)
		{
			return std::make_shared<Scene>(std::forward<Args>(args)...);
		}

		/** @brief Instantiates a blank Entity handle bound to this scene instance.
		 *  Emplaces a Transform, a Tag, and an IDComponent with a fresh UUID (E2). */
		Entity CreateEntity(const std::string& name = "GenericEntity");

		/** @brief As CreateEntity, but with a caller-supplied UUID — used by the
		 *  SceneSerializer so loaded entities keep their stable identity. If the
		 *  UUID is 0/invalid a fresh one is generated. */
		Entity CreateEntityWithUUID(UUID id, const std::string& name = "GenericEntity");

		/** @brief Resolve a UUID to its Entity handle, or an invalid handle if the
		 *  scene holds no entity with that id (E2). O(1) via the id index. */
		Entity FindByUUID(UUID id);

		/** @brief Destroys an entity (E3). By default its whole subtree is
		 *  destroyed too; pass destroyChildren=false to orphan the children
		 *  (their Parent link is cleared) instead. Always detaches the entity
		 *  from its own parent's Children list. Flat entities (no
		 *  RelationshipComponent) behave exactly as before. */
		void DestroyEntity(Entity entity, bool destroyChildren = true);

		// --- Hierarchy (E3) ---------------------------------------------------

		/** @brief Re-parent `child` under `parent` (pass an invalid parent to
		 *  detach to root). When keepWorldPose is true the child's local
		 *  transform is rewritten so its WORLD pose does not change. Refuses (and
		 *  warns, returns false) if it would create a cycle. Children order is
		 *  the append order of SetParent calls. */
		bool SetParent(Entity child, Entity parent, bool keepWorldPose = true);

		/** @brief World-space transform = parent-world x local, walking the
		 *  parent chain. For an entity without a RelationshipComponent this is
		 *  just its local TransformComponent — so flat scenes are unchanged. */
		glm::mat4 GetWorldTransform(Entity entity);

		/** @brief True if `ancestor` appears anywhere on `node`'s parent chain. */
		bool IsAncestor(Entity ancestor, Entity node);

		/** @brief Runs ongoing frame logic updates across all registered systems. */
		void OnUpdate(float deltaTime);

		/** @brief Runs fixed time-step physics/simulation routines across systems. */
		void OnFixedUpdate(float fixedDeltaTime);

		/**
		 * @brief Calls BeginScene with the provided camera, dispatches all sprite-bearing
		 * entities to Renderer2D (grouped by material bucket to minimise draw call overhead),
		 * then calls EndScene.
		 *
		 * Callers must NOT wrap this in their own BeginScene/EndScene — the scene owns the
		 * full render pass so that the camera and viewport state are always correct.
		 *
		 * Entities within each material bucket are sorted ascending by Position.z before
		 * drawing to guarantee correct depth order. This sort is O(n log n) per bucket per
		 * frame, where n is the number of entities sharing one material. Cost is negligible
		 * for typical bucket sizes (< ~1000). If a single material bucket becomes very large,
		 * consider a dirty-flag skip (only sort when z-values changed) or a pre-sorted
		 * container updated incrementally rather than re-sorted every frame.
		 *
		 * @param camera  The orthographic camera whose View-Projection matrix will be used
		 *                for this render pass.
		 */
		void OnRender(const OrthographicCamera& camera);

		/**
		 * @brief 3D render pass (S4.3): draws every entity with a
		 * TransformComponent + MeshRendererComponent via Renderer3D.
		 *
		 * Owns its own BeginScene/EndScene — do NOT wrap this call. Entities with a
		 * null MeshAsset are skipped; a null MaterialAsset uses the Lambert color
		 * path (Color tint), otherwise the custom-material path. No sorting/culling
		 * yet (that is S12). Does not touch OnRender (the 2D path) — both can run in
		 * one frame, 3D world first then 2D overlay.
		 */
		void OnRender3D(const Camera& camera);

		/**
		 * @brief Prepare mesh assets for rendering (E15/E16). (Re)generates the mesh
		 * of every entity with a PrimitiveMeshComponent whose parameters changed
		 * since its last build, and resolves any MeshRendererComponent::MeshPath
		 * (an imported/loaded asset) to a live MeshAsset via AssetLibrary — both
		 * cover the freshly-loaded-scene case where meshes are stored by params /
		 * path only. Called automatically at the top of OnRender3D; safe to call
		 * manually before a custom render path. Main-thread / GL (uploads meshes).
		 */
		void SyncPrimitiveMeshes();

		/** @brief Allocates and attaches an execution system to the scene lifecycle. */
		template<typename T, typename... Args>
		T& AddSystem(Args&&... args)
		{
			auto system = CreateScope<T>(std::forward<Args>(args)...);
			T& ref = *system;
			if (auto* ps = dynamic_cast<ParallelSystem*>(system.get()))
				m_ParallelSystems.push_back(ps);
			m_Systems.push_back(std::move(system));
			return ref;
		}

		/** @brief Clears out all systems bound to this scene instance. */
		void RemoveAllSystems()
		{
			// Destroy owned systems first (runs their destructors), then clear the
			// non-owning parallel pointer list. This order ensures that if a future
			// ParallelSystem destructor ever inspects m_ParallelSystems (e.g. to
			// unregister itself), the list is still intact when the destructor runs.
			m_Systems.clear();
			m_ParallelSystems.clear();
		}

		/** @brief Safe multi-component layout querying mechanism for external or client layers. */
		template<typename... Components>
		auto View()
		{
			return m_Registry.view<Components...>();
		}

		inline entt::registry& GetRegistry() { return m_Registry; }
		inline const entt::registry& GetRegistry() const { return m_Registry; }


	public:
		/**
		 * @brief Linear search for a registered system of type T.
		 *
		 * WARNING — O(n) per call: iterates the full system list and performs a
		 * dynamic_cast on each entry. Must NOT be called per-frame. Cache the
		 * returned pointer in your layer's OnAttach and reuse it every tick.
		 *
		 * @return Pointer to the first system of type T, or nullptr if none is found.
		 */
		template<typename T>
		T* GetSystem()
		{
			for (auto& system : m_Systems)
			{
				// Safely attempt to downcast the base System pointer to your requested subclass
				T* target = dynamic_cast<T*>(system.get());
				if (target)
					return target;
			}
			return nullptr;
		}

	private:
		/** @brief Recursive world-transform walk keyed by entt handle (E3). */
		glm::mat4 WorldOf(entt::entity handle);

		entt::registry m_Registry;
		std::vector<Scope<System>>   m_Systems;
		std::vector<ParallelSystem*> m_ParallelSystems; // non-owning; owned by m_Systems

		// UUID -> handle index, kept in sync by CreateEntity*/DestroyEntity so
		// FindByUUID (parent/EntityRef/prefab resolution) is O(1). Entities added
		// straight to the registry (not via CreateEntity*) aren't indexed.
		std::unordered_map<UUID, entt::entity> m_UUIDMap;

		// Persistent scratch buffers for OnRender's material-bucket sort. Reused every
		// frame (cleared, not reallocated) to avoid per-frame heap churn. Inner vectors
		// retain their capacity across frames; empty buckets are skipped during dispatch.
		std::unordered_map<Material*, std::vector<entt::entity>> m_RenderMaterialBuckets;
		std::vector<entt::entity>                                m_RenderFlatColorBucket;

		friend class Entity; // Gives access to the registry mapping internals securely
	};
}