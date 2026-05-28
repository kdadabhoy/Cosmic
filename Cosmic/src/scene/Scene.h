#pragma once
// Scene.h

#include "core/Core.h"
#include "scene/System.h"
#include "jobs/ParallelSystem.h"
#include <entt/entt.hpp>
#include <string>
#include <memory>
#include <vector>

namespace Cosmic
{
	class Entity;          // Forward declaration
	class OrthographicCamera; // Forward declaration

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
		 * @param camera  The orthographic camera whose View-Projection matrix will be used
		 *                for this render pass.
		 */
		void OnRender(const OrthographicCamera& camera);

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
			m_ParallelSystems.clear();
			m_Systems.clear();
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

		friend class Entity; // Gives access to the registry mapping internals securely
	};
}