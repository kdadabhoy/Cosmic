// Scene.cpp
// Optimized Material Grouping System implemented 5/21/2026
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "renderer/Renderer2D.h"
#include <unordered_map>
#include <vector>

namespace Cosmic
{
	Scene::Scene()
	{
	}

	Entity Scene::CreateEntity(const std::string& name)
	{
		Entity entity = { m_Registry.create(), this };
		entity.AddComponent<TransformComponent>();
		entity.AddComponent<TagComponent>(name.empty() ? "Entity" : name);
		return entity;
	}

	void Scene::DestroyEntity(Entity entity)
	{
		m_Registry.destroy((entt::entity)entity);
	}

	void Scene::OnUpdate(float deltaTime)
	{
		// Sequentially process each decoupled subsystem dispatch logic
		for (auto& system : m_Systems)
		{
			system->OnUpdate(*this, deltaTime);
		}
	}

	void Scene::OnFixedUpdate(float fixedDeltaTime)
	{
		for (auto& system : m_Systems)
		{
			system->OnFixedUpdate(*this, fixedDeltaTime);
		}
	}

	void Scene::OnRender()
	{
		// 1. Gather all entities containing rendering properties
		auto view = m_Registry.view<TransformComponent, SpriteRendererComponent>();

		// 2. Establish sorting buckets to minimize batch breaking state changes
		std::unordered_map<Material*, std::vector<entt::entity>> materialBuckets;
		std::vector<entt::entity> flatColorFallbackBucket;

		materialBuckets.reserve(16);

		// Use EnTT's native .each() layout to cleanly extract entity IDs and references
		view.each([&](auto entity, const auto& transform, const auto& sprite)
			{
				if (sprite.ActiveMaterial)
				{
					materialBuckets[sprite.ActiveMaterial.get()].push_back(entity);
				}
				else
				{
					flatColorFallbackBucket.push_back(entity);
				}
			});

		// 3. Dispatch Material-Batched Quads
		for (const auto& [materialPtr, entities] : materialBuckets)
		{
			// Safely extract the shared_ptr reference from the first entity in the bucket
			auto& firstSprite = view.get<SpriteRendererComponent>(entities[0]);
			Ref<Material> activeMaterial = firstSprite.ActiveMaterial;

			for (auto entity : entities)
			{
				auto& transform = view.get<TransformComponent>(entity);

				Renderer2D::DrawRotatedQuad(
					transform.Position,
					transform.Scale,
					transform.Rotation.z,
					activeMaterial
				);
			}
		}

		// 4. Dispatch Fallback Flat-Color Quads
		for (auto entity : flatColorFallbackBucket)
		{
			auto& transform = view.get<TransformComponent>(entity);
			auto& sprite = view.get<SpriteRendererComponent>(entity);

			Renderer2D::DrawRotatedQuad(
				transform.Position,
				transform.Scale,
				transform.Rotation.z,
				sprite.Color
			);
		}
	}
}