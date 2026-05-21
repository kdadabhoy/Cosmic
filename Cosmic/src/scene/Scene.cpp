// Scene.cpp
// Last Modified: 5/20/2026

#include "Scene.h"
#include "Entity.h"
#include "Components.h"
#include "renderer/Renderer2D.h"

namespace Cosmic
{
	Scene::Scene()
	{
	}

	Entity Scene::CreateEntity(const std::string& name)
	{
		Entity entity = { m_Registry.create(), this };

		// Every entity automatically receives identity and position records by default
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
		// Future system expansions go here (e.g., Physics2D, Native Script Updates)
	}

	void Scene::OnRender()
	{
		// 1. Gather all entities containing both positioning and material layout properties
		auto group = m_Registry.view<TransformComponent, SpriteRendererComponent>();

		for (auto entity : group)
		{
			auto& transform = group.get<TransformComponent>(entity);
			auto& sprite = group.get<SpriteRendererComponent>(entity);

			// 2. Route directly to your native Renderer2D batching system
			if (sprite.ActiveMaterial)
			{
				// Pulling exact vectors from your structural definitions
				Renderer2D::DrawRotatedQuad(
					transform.Position,
					transform.Scale,
					transform.Rotation.z, // Mapping 2D roll
					sprite.ActiveMaterial
				);
			}
			else
			{
				// Fallback: If no material is assigned, render via raw tint color override
				Renderer2D::DrawRotatedQuad(
					transform.Position,
					transform.Scale,
					transform.Rotation.z,
					sprite.Color
				);
			}
		}
	}
}