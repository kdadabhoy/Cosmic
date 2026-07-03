#pragma once
// Scene.h

#include "core/Core.h"
#include "scene/System.h"
#include "jobs/ParallelSystem.h"
#include <entt/entt.hpp>
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

		/** @brief Instantiates a blank Entity handle bound to this scene instance. */
		Entity CreateEntity(const std::string& name = "GenericEntity");

		/** @brief Destroys and cleans up internal registry component references. */
		void DestroyEntity(Entity entity);

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
		entt::registry m_Registry;
		std::vector<Scope<System>>   m_Systems;
		std::vector<ParallelSystem*> m_ParallelSystems; // non-owning; owned by m_Systems

		// Persistent scratch buffers for OnRender's material-bucket sort. Reused every
		// frame (cleared, not reallocated) to avoid per-frame heap churn. Inner vectors
		// retain their capacity across frames; empty buckets are skipped during dispatch.
		std::unordered_map<Material*, std::vector<entt::entity>> m_RenderMaterialBuckets;
		std::vector<entt::entity>                                m_RenderFlatColorBucket;

		friend class Entity; // Gives access to the registry mapping internals securely
	};
}