#include "Cosmic.h"
#include <imgui.h>
#include <random>
#include <vector>
#include <algorithm>
#include <cmath>

namespace Workspace
{
	// Minimal Inline Obstacle Blueprint
	struct Obstacle
	{
		glm::vec3 Position;
		glm::vec2 Size;
		glm::vec4 Color;
	};

	// FIXED: Inherit from Cosmic::Layer instead of ProjectPlugin
	class SimpleDinoProject : public Cosmic::Layer
	{
	public:
		SimpleDinoProject()
			: Layer("SimpleDinoProject"), m_CameraController(1280.0f / 720.0f, true), m_RandomEngine(std::random_device{}())
		{
		}

		virtual ~SimpleDinoProject() = default;

		// --- Host Lifecycle Subscriptions ---

		virtual void OnAttach() override
		{
			ResetSimulation();
		}

		virtual void OnDetach() override
		{
			m_Obstacles.clear();
		}

		// FIXED: Signature changed from Cosmic::Timestep to float to match Layer.h
		virtual void OnUpdate(float deltaTime) override
		{
			// 1. High-frequency Input Polling
			if (Cosmic::Input::IsKeyPressed(CS_KEY_SPACE) && m_IsGrounded)
			{
				m_VelocityY = 5.0f;
				m_IsGrounded = false;
			}

			m_CameraController.OnUpdate(deltaTime);

			// 2. Direct Rendering
			RenderActiveScene();
		}

		// FIXED: Signature changed from Cosmic::Timestep to float to match Layer.h
		virtual void OnFixedUpdate(float deltaFixedTime) override
		{
			m_Score += deltaFixedTime * 10.0f;

			// 1. Gravity and Linear Physics Jump Processing
			if (!m_IsGrounded)
			{
				m_VelocityY -= 12.0f * deltaFixedTime;
				m_DinoPos.y += m_VelocityY * deltaFixedTime;
			}

			// Floor Bounds Collision Check
			if (m_DinoPos.y <= -0.5f)
			{
				m_DinoPos.y = -0.5f;
				m_VelocityY = 0.0f;
				m_IsGrounded = true;
			}

			// 2. Obstacle Generation Tick Logic
			m_SpawnTimer += deltaFixedTime;
			if (m_SpawnTimer > m_NextSpawnTime)
			{
				float calculatedHeight = std::uniform_real_distribution<float>(0.3f, 0.8f)(m_RandomEngine);
				m_Obstacles.push_back({
					{ 2.5f, -0.8f + (calculatedHeight / 2.0f), 0.0f },
					{ 0.3f, calculatedHeight },
					{ 0.8f, 0.2f, 0.2f, 1.0f }
					});
				m_SpawnTimer = 0.0f;
				m_NextSpawnTime = std::uniform_real_distribution<float>(1.0f, 2.0f)(m_RandomEngine);
			}

			// 3. Positional Progression & AABB Intersect Checking
			for (auto& obs : m_Obstacles)
			{
				obs.Position.x -= 2.0f * deltaFixedTime;

				if (std::abs(m_DinoPos.x - obs.Position.x) < 0.3f &&
					std::abs(m_DinoPos.y - obs.Position.y) < (obs.Size.y / 2.0f + 0.2f))
				{
					ResetSimulation();
					return;
				}
			}

			// Garbage collection for out-of-bounds obstacles
			m_Obstacles.erase(std::remove_if(m_Obstacles.begin(), m_Obstacles.end(),
				[](const Obstacle& o) { return o.Position.x < -3.0f; }), m_Obstacles.end());
		}

		virtual void OnImGuiRender() override
		{
			ImGui::Begin("Dino Game Panel");
			ImGui::Text("Proof of Concept Runner DLL Active!");
			ImGui::Separator();

			ImGui::Value("Score Counter", (int)m_Score);
			ImGui::Text("Player Grounded: %s", m_IsGrounded ? "True" : "False");
			ImGui::Text("Vertical Velocity: %.2f", m_VelocityY);

			if (ImGui::Button("Force Clear Obstacles"))
			{
				ResetSimulation();
			}
			ImGui::End();
		}

	private:
		void ResetSimulation()
		{
			m_DinoPos = { -1.0f, -0.5f, 0.0f };
			m_VelocityY = 0.0f;
			m_Score = 0.0f;
			m_IsGrounded = true;
			m_Obstacles.clear();
		}

		void RenderActiveScene()
		{
			Cosmic::Renderer2D::BeginScene(m_CameraController.GetCamera());

			// Render Static Floor Anchor
			Cosmic::Renderer2D::DrawQuad({ 0.0f, -0.85f }, { 20.0f, 0.2f }, { 0.3f, 0.3f, 0.33f, 1.0f });

			// Render Generated Moving Obstacles
			for (const auto& obs : m_Obstacles)
			{
				Cosmic::Renderer2D::DrawQuad(obs.Position, obs.Size, obs.Color);
			}

			// Render Minimalist Dino Block
			Cosmic::Renderer2D::DrawQuad(m_DinoPos, { 0.5f, 0.5f }, { 0.2f, 0.8f, 0.2f, 1.0f });

			Cosmic::Renderer2D::EndScene();
		}

	private:
		Cosmic::OrthographicCameraController m_CameraController;
		std::mt19937 m_RandomEngine;

		glm::vec3 m_DinoPos = { -1.0f, -0.5f, 0.0f };
		float m_VelocityY = 0.0f;
		bool m_IsGrounded = true;

		std::vector<Obstacle> m_Obstacles;
		float m_SpawnTimer = 0.0f;
		float m_NextSpawnTime = 2.0f;
		float m_Score = 0.0f;
	};
}

// --- Dynamic Module Export Linkages ---
extern "C"
{
	__declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context)
	{
		ImGui::SetCurrentContext(context.ImGuiCtx);
		ImPlot::SetCurrentContext(context.ImPlotCtx);
	}

	// FIXED: Signature changed to match the CreatePluginLayer expectations returning a Cosmic::Layer*
	__declspec(dllexport) Cosmic::Layer* CreatePluginLayer()
	{
		return new Workspace::SimpleDinoProject();
	}
}