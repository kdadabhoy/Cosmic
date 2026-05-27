#pragma once
#include <Cosmic.h>
#include <vector>
#include <string>

namespace Workspace
{
	// ============================================================================
	// Sprite data for a single dino character managed through the ECS
	// ============================================================================
	struct DinoCharacterComponent
	{
		float      WalkSpeed = 3.0f;   // world units per second
		float      BobFrequency = 2.5f;   // Hz
		float      BobAmplitude = 0.18f;  // world units
		float      PhaseOffset = 0.0f;   // radians — staggers the bob between dinos
		glm::vec3  HomePosition = { 0.f, 0.f, 0.f };
		bool       FacingLeft = false;
	};

	// ============================================================================
	// TemplateSpriteLayer
	//
	// Demonstrates:
	//   - Loading a sprite atlas via the VFS and slicing it with SubTexture2D
	//   - ECS entity creation: TransformComponent + SpriteRendererComponent
	//     + custom DinoCharacterComponent
	//   - Scene::OnRender(camera) for batch sprite rendering
	//   - Mixed rendering: scene entities + manual Renderer2D overlay draw calls
	//     in the same frame using two separate passes
	//   - Multi-camera split-screen using RenderPass RAII
	//       TL: close-up follow camera (tracks the selected dino)
	//       TR: wide overhead overview
	//       BL: blue-tinted slow-motion mirror
	//       BR: debug wireframe with velocity vectors and bounding boxes
	//   - CS_REGISTER_COMPONENT for DLL-safe component type IDs
	//   - TransformComponent::Rotation.z stored in degrees; Scene::OnRender
	//     converts to radians automatically — manual DrawRotatedQuad callers
	//     must do their own conversion
	// ============================================================================
	class TemplateSpriteLayer : public Cosmic::Layer
	{
	public:
		explicit TemplateSpriteLayer(Cosmic::Ref<Cosmic::Scene> scene);
		virtual ~TemplateSpriteLayer() override = default;

		virtual void OnAttach()                          override;
		virtual void OnDetach()                          override;
		virtual void OnUpdate(float ts)                  override;
		virtual void OnFixedUpdate(float deltaFixedTime) override;
		virtual void OnImGuiRender()                     override;
		virtual void OnEvent(Cosmic::Event& e)           override;

	private:
		// -----------------------------------------------------------------------
		// Internal helpers
		// -----------------------------------------------------------------------
		void BuildEntities();
		void CleanupEntities();
		void UpdateSubTexture(int entityIndex);

		bool OnWindowResize(Cosmic::WindowResizeEvent& e);
		bool OnKeyPressed(Cosmic::KeyPressedEvent& e);
		bool OnMouseClicked(Cosmic::MouseButtonPressedEvent& e);

		// Screen-to-world unprojection for the close-up camera quadrant
		glm::vec2 ScreenToWorldTL(glm::vec2 screenPos) const;
		bool      HitTestEntity(Cosmic::Entity ent, glm::vec2 worldPos) const;

		// Per-quadrant draw helpers called inside RenderPass scopes
		void DrawGrid(const glm::vec4& color, float spacing, float extent);
		void DrawDebugOverlay();   // wireframe bounds + velocity vectors (BR quadrant)
		void DrawTrackingRings();  // pulsing SDF ring under selected dino

	private:
		// -----------------------------------------------------------------------
		// Scene
		// -----------------------------------------------------------------------
		Cosmic::Ref<Cosmic::Scene>                  m_Scene;

		// -----------------------------------------------------------------------
		// Atlas & sub-textures
		// -----------------------------------------------------------------------
		// One texture per colour variant (doux=blue, mort=red, tard=yellow, vita=green)
		std::vector<Cosmic::Ref<Cosmic::Texture2D>>    m_Atlases;
		std::vector<std::string>                        m_AtlasNames;

		// Per-entity current frame sub-texture
		std::vector<Cosmic::Ref<Cosmic::SubTexture2D>> m_SubTextures;

		// ECS entity handles — one per dino
		std::vector<Cosmic::Entity>                     m_Entities;

		// Per-entity atlas selection (which colour variant is active)
		std::vector<int>                                m_AtlasIndex;

		// Per-entity current animation column (0-based)
		std::vector<float>                              m_AnimCoords;

		static constexpr glm::vec2 k_CellSize = { 24.0f, 24.0f };

		// -----------------------------------------------------------------------
		// Animation
		// -----------------------------------------------------------------------
		float m_FrameTimer = 0.0f;
		float m_FrameDuration = 0.12f;   // seconds per frame
		int   m_RunStartCol = 4;       // column where the run cycle begins
		int   m_RunFrameCount = 6;
		int   m_CurrentRunCol = 0;       // shared animation column across all dinos

		// -----------------------------------------------------------------------
		// Four independent cameras for split-screen
		// -----------------------------------------------------------------------
		Cosmic::OrthographicCameraController m_CamTL;   // close-up follow
		Cosmic::OrthographicCameraController m_CamTR;   // overhead overview
		Cosmic::OrthographicCameraController m_CamBL;   // blue-tinted slow-mo mirror
		Cosmic::OrthographicCameraController m_CamBR;   // debug wireframe

		glm::vec2 m_ViewportSize = { 1280.f, 720.f };

		// -----------------------------------------------------------------------
		// Selection
		// -----------------------------------------------------------------------
		int   m_SelectedIndex = 0;    // which dino the TL camera follows
		float m_LocalTime = 0.f;  // mirrors GetLocalTime() for overlay draws

		// -----------------------------------------------------------------------
		// Inspector tweakables
		// -----------------------------------------------------------------------
		float m_GlobalMoveSpeed = 3.0f;
		bool  m_ShowGrid = true;
		bool  m_ShowRings = true;
		bool  m_ShowDebugBounds = true;
		float m_RingPulseSpeed = 3.5f;
		float m_RingPulseAmp = 0.10f;
	};

} // namespace Workspace

// DLL-safe registration — must be at file scope in the header
CS_REGISTER_COMPONENT(Workspace::DinoCharacterComponent)