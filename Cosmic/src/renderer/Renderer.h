#pragma once

// Renderer.h
// Last Modified 5/14/2026

/**
 * ============================================================================
 * COSMIC ENGINE RENDERER
 * ============================================================================
 * GOAL: To act as the high-level orchestrator for all graphics operations.
 *
 * The Renderer provides the "Global Context" for a frame. It manages the
 * camera's perspective (SceneData) and provides a standardized pathway (Submit)
 * to draw geometry using any shader.
 *
 * It is the parent system to specialized renderers like Renderer2D and
 * directly interfaces with RenderCommand for API execution.
 *
 * ----------------------------------------------------------------------------
 * LEGACY PATH WARNING
 * ----------------------------------------------------------------------------
 * This class is the original low-level, un-batched submission path. Each
 * Submit() is one GPU draw call, and its camera (BeginScene below) is tracked
 * SEPARATELY from Renderer2D's. The two do NOT share view-projection state:
 * Renderer::BeginScene has no effect on Renderer2D, and vice-versa. Mixing
 * Renderer::Submit with Renderer2D::DrawQuad in the same frame will silently
 * draw the two sets of geometry under different cameras.
 *
 * Prefer Renderer2D (batched) for all normal 2D drawing. Reach for
 * Renderer::Submit only as an escape hatch for custom-shader geometry that the
 * batch renderer cannot express, and drive it from its own BeginScene/EndScene.
 */

#include "core/Core.h"
#include "renderer/RenderCommand.h"
#include "renderer/Renderer2D.h"
#include "camera/OrthographicCamera.h"
#include "graphics/Shader.h"
#include "graphics/VertexArray.h"
#include "graphics/Texture.h"
#include <glm/glm.hpp>

namespace Cosmic
{
	class Renderer
	{
	public:
		/**
		 * SYSTEM LIFECYCLE
		 * These methods are called by the Application to manage the GPU's state.
		 * Init() wakes up RenderCommand and Renderer2D.
		 */
		static void Init();
		static void Shutdown();

		/**
		 * VIEWPORT MANAGEMENT
		 * Communicates with the hardware API to update the draw area
		 * based on new window dimensions.
		 */
		static void OnWindowResize(uint32_t width, uint32_t height);

		/**
		 * SCENE BOUNDARIES
		 * BeginScene captures the camera's View-Projection matrix into SceneData.
		 * EndScene is reserved for future command queue sorting and optimization.
		 */
		static void BeginScene(OrthographicCamera& camera);
		static void EndScene();

		/**
		 * THE SUBMISSION PIPELINE
		 * Submit binds the shader, uploads global scene data (camera) and local
		 * object data (transform), and executes the draw via RenderCommand.
		 */

		 // 1. Raw Submission: Direct control via pre-calculated mat4
		static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const Ref<Texture>& texture, const glm::mat4& transform = glm::mat4(1.0f));
		static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::mat4& transform = glm::mat4(1.0f));

		// 2. Component Submission: Internal mat4 generation (Pos/Scale)
		static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::vec3& position, const glm::vec3& scale = glm::vec3(1.0f));

		// 3. Rotated Submission: Internal mat4 generation with Z-axis rotation
		static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::vec3& position, float rotationDegrees, const glm::vec3& scale = glm::vec3(1.0f));

		/**
		 * API ACCESS
		 * Identifies the current backend (e.g., OpenGL) for API-specific logic.
		 */
		inline static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }

	private:
		struct SceneData
		{
			glm::mat4 ViewProjectionMatrix{ 1.0f };
		};

		// Value member, not a heap pointer: it lives for the program's lifetime with
		// no allocation to leak. (Previously `new SceneData` that Shutdown never freed.)
		static SceneData s_SceneData; // Global state for the active camera pass
	};
}