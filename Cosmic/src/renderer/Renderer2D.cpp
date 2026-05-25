// Renderer2D.cpp
// Last Modified: 5/24/2026

#include "renderer/Renderer2D.h"
#include "graphics/VertexArray.h"
#include "graphics/Shader.h"
#include "renderer/RenderCommand.h"
#include "core/Log.h"
#include "graphics/SubTexture2D.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <vector>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////
	// Internal Vertex Structures
	/////////////////////////////////////////////////////////////////////////////////

	struct LineVertex
	{
		glm::vec3 Position;
		glm::vec4 Color;
	};

	struct QuadVertex
	{
		glm::vec3 Position;
		glm::vec4 Color;
		glm::vec2 TexCoord;
		float     TexIndex;
		float     TilingFactor;
	};

	struct CircleVertex
	{
		glm::vec3 WorldPosition;
		glm::vec2 LocalPosition;
		glm::vec4 Color;
		float     Thickness;
		float     Fade;
	};

	/////////////////////////////////////////////////////////////////////////////////
	// Internal State Storage
	/////////////////////////////////////////////////////////////////////////////////

	struct Renderer2DData
	{
		// Batch Limits
		static const uint32_t MaxQuads = 10000;
		static const uint32_t MaxVertices = MaxQuads * 4;
		static const uint32_t MaxIndices = MaxQuads * 6;
		static const uint32_t MaxTextureSlots = 32;
		static const uint32_t MaxCircles = 2000;
		static const uint32_t MaxCircleVertices = MaxCircles * 4;
		static const uint32_t MaxCircleIndices = MaxCircles * 6;

		// --- Quad Data ---
		Ref<VertexArray>  QuadVertexArray;
		Ref<VertexBuffer> QuadVertexBuffer;
		Ref<Shader>       TextureShader;
		Ref<Texture>      WhiteTexture;

		Ref<Material> CurrentMaterial = nullptr;
		Ref<Material> DefaultMaterial = nullptr;

		uint32_t    QuadIndexCount = 0;
		QuadVertex* QuadVertexBufferBase = nullptr;
		QuadVertex* QuadVertexPtr = nullptr;

		std::array<Ref<Texture>, MaxTextureSlots> TextureSlots;
		uint32_t TextureSlotIndex = 1; // Slot 0 = White Texture

		glm::vec4 QuadVertexPositions[4];

		// --- Line Data ---
		Ref<VertexArray>  LineVertexArray;
		Ref<VertexBuffer> LineVertexBuffer;
		Ref<Shader>       LineShader;

		uint32_t    LineVertexCount = 0;
		LineVertex* LineVertexBufferBase = nullptr;
		LineVertex* LineVertexBufferPtr = nullptr;

		// --- Circle Data ---
		Ref<VertexArray>  CircleVertexArray;
		Ref<VertexBuffer> CircleVertexBuffer;
		Ref<Shader>       CircleShader;

		uint32_t      CircleIndexCount = 0;
		CircleVertex* CircleVertexBufferBase = nullptr;
		CircleVertex* CircleVertexBufferPtr = nullptr;

		// --- Scene-Wide Uniforms ---
		glm::mat4  ViewProjectionMatrix{ 1.0f };
		glm::vec2  ViewportDimensions{ 1280.0f, 720.0f };

		// --- Telemetry ---
		Renderer2D::Statistics Stats;
		bool StatsEnabled = false;

		// =========================================================================
		// RENDER PASS STACK
		// Stores per-pass camera matrix + viewport bounds so that multiple cameras
		// can render sequentially (or even in nested scopes) within a single frame.
		// Push flushes the current batch before installing new state.
		// Pop flushes again and restores the prior state.
		// =========================================================================
		std::vector<Renderer2D::RenderPassState> RenderPassStack;
	};

	static Renderer2DData s_Data;

	/////////////////////////////////////////////////////////////////////////////////
	// Lifecycle
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer2D::Init()
	{
		CS_CORE_TRACE("Initializing Renderer2D...");

		// --- Quad Initialization ---
		s_Data.QuadVertexArray = VertexArray::Create();
		s_Data.QuadVertexBuffer = VertexBuffer::Create(s_Data.MaxVertices * sizeof(QuadVertex));
		s_Data.QuadVertexBuffer->SetLayout({
			{ ShaderDataType::Float3, "a_Position"     },
			{ ShaderDataType::Float4, "a_Color"        },
			{ ShaderDataType::Float2, "a_TexCoord"     },
			{ ShaderDataType::Float,  "a_TexIndex"     },
			{ ShaderDataType::Float,  "a_TilingFactor" }
			});
		s_Data.QuadVertexArray->AddVertexBuffer(s_Data.QuadVertexBuffer);
		s_Data.QuadVertexBufferBase = new QuadVertex[s_Data.MaxVertices];

		uint32_t* quadIndices = new uint32_t[s_Data.MaxIndices];
		uint32_t offset = 0;
		for (uint32_t i = 0; i < s_Data.MaxIndices; i += 6)
		{
			quadIndices[i + 0] = offset + 0;
			quadIndices[i + 1] = offset + 1;
			quadIndices[i + 2] = offset + 2;
			quadIndices[i + 3] = offset + 2;
			quadIndices[i + 4] = offset + 3;
			quadIndices[i + 5] = offset + 0;
			offset += 4;
		}
		Ref<IndexBuffer> quadIB = IndexBuffer::Create(quadIndices, s_Data.MaxIndices);
		s_Data.QuadVertexArray->SetIndexBuffer(quadIB);
		delete[] quadIndices;

		// --- White Texture (Slot 0 fallback) ---
		s_Data.WhiteTexture = Texture2D::Create(1, 1);
		uint32_t whiteTextureData = 0xffffffff;
		s_Data.WhiteTexture->SetData(&whiteTextureData, sizeof(uint32_t));

		int32_t samplers[s_Data.MaxTextureSlots];
		for (uint32_t i = 0; i < s_Data.MaxTextureSlots; i++)
			samplers[i] = i;

		s_Data.TextureShader = Shader::Create("assets/shaders/Texture.glsl");
		s_Data.TextureShader->Bind();
		s_Data.TextureShader->SetIntArray("u_Textures", samplers, s_Data.MaxTextureSlots);

		s_Data.DefaultMaterial = Material::Create(s_Data.TextureShader, "Cosmic_Default_Material");
		s_Data.TextureSlots[0] = s_Data.WhiteTexture;

		s_Data.QuadVertexPositions[0] = { -0.5f, -0.5f, 0.0f, 1.0f };
		s_Data.QuadVertexPositions[1] = { 0.5f, -0.5f, 0.0f, 1.0f };
		s_Data.QuadVertexPositions[2] = { 0.5f,  0.5f, 0.0f, 1.0f };
		s_Data.QuadVertexPositions[3] = { -0.5f,  0.5f, 0.0f, 1.0f };

		// --- Line Initialization ---
		s_Data.LineVertexArray = VertexArray::Create();
		s_Data.LineVertexBuffer = VertexBuffer::Create(s_Data.MaxVertices * sizeof(LineVertex));
		s_Data.LineVertexBuffer->SetLayout({
			{ ShaderDataType::Float3, "a_Position" },
			{ ShaderDataType::Float4, "a_Color"    }
			});
		s_Data.LineVertexArray->AddVertexBuffer(s_Data.LineVertexBuffer);
		s_Data.LineVertexBufferBase = new LineVertex[s_Data.MaxVertices];
		s_Data.LineShader = Shader::Create("assets/shaders/Line.glsl");

		if (!s_Data.LineShader)
			CS_CORE_ERROR("Renderer2D: Failed to load Line shader!");

		// --- Circle Initialization ---
		s_Data.CircleVertexArray = VertexArray::Create();
		s_Data.CircleVertexBuffer = VertexBuffer::Create(s_Data.MaxCircleVertices * sizeof(CircleVertex));
		s_Data.CircleVertexBuffer->SetLayout({
			{ ShaderDataType::Float3, "a_WorldPosition" },
			{ ShaderDataType::Float2, "a_LocalPosition" },
			{ ShaderDataType::Float4, "a_Color"         },
			{ ShaderDataType::Float,  "a_Thickness"     },
			{ ShaderDataType::Float,  "a_Fade"          }
			});
		s_Data.CircleVertexArray->AddVertexBuffer(s_Data.CircleVertexBuffer);
		s_Data.CircleVertexBufferBase = new CircleVertex[s_Data.MaxCircleVertices];

		// Reuse the quad index buffer — circle quads use the same 0-1-2/2-3-0 topology
		s_Data.CircleVertexArray->SetIndexBuffer(quadIB);
		s_Data.CircleShader = Shader::Create("assets/shaders/Circle.glsl");

		CS_CORE_INFO("Renderer2D initialized.");
	}

	void Renderer2D::Shutdown()
	{
		CS_CORE_TRACE("Shutting down Renderer2D");
		delete[] s_Data.QuadVertexBufferBase;
		delete[] s_Data.LineVertexBufferBase;
		delete[] s_Data.CircleVertexBufferBase;
		s_Data.RenderPassStack.clear();
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Render Pass Stack Implementation
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer2D::PushRenderPass(const glm::mat4& viewProj, const glm::vec4& viewportBounds)
	{
		// Safety: Flush any geometry pending under the current pass BEFORE changing state.
		// This guarantees that quads submitted under Camera A are drawn with Camera A's
		// VP matrix, and not contaminated by the incoming Camera B matrix.
		bool hasPendingGeometry = (s_Data.QuadIndexCount > 0 ||
			s_Data.CircleIndexCount > 0 ||
			s_Data.LineVertexCount > 0);
		if (hasPendingGeometry)
		{
			Flush();
		}

		// Build and push the new pass state
		RenderPassState newState;
		newState.ViewProjectionMatrix = viewProj;
		newState.ViewportBounds = viewportBounds;
		s_Data.RenderPassStack.push_back(newState);

		// Install the new VP matrix into the active data slot
		s_Data.ViewProjectionMatrix = viewProj;

		// Update the hardware viewport to the requested bounds
		glViewport(
			static_cast<int>(viewportBounds.x),
			static_cast<int>(viewportBounds.y),
			static_cast<int>(viewportBounds.z),
			static_cast<int>(viewportBounds.w)
		);

		// Update viewport dimension tracking for shader uniforms (u_ViewportSize)
		s_Data.ViewportDimensions = { viewportBounds.z, viewportBounds.w };

		// Reset all batch counters for this new pass
		s_Data.QuadIndexCount = 0;
		s_Data.QuadVertexPtr = s_Data.QuadVertexBufferBase;
		s_Data.TextureSlotIndex = 1;
		s_Data.CurrentMaterial = s_Data.DefaultMaterial;

		s_Data.LineVertexCount = 0;
		s_Data.LineVertexBufferPtr = s_Data.LineVertexBufferBase;

		s_Data.CircleIndexCount = 0;
		s_Data.CircleVertexBufferPtr = s_Data.CircleVertexBufferBase;
	}

	void Renderer2D::PopRenderPass()
	{
		CS_CORE_ASSERT(!s_Data.RenderPassStack.empty(), "PopRenderPass called with an empty stack! Mismatched Push/Pop.");

		// Flush geometry staged under the pass being popped
		bool hasPendingGeometry = (s_Data.QuadIndexCount > 0 ||
			s_Data.CircleIndexCount > 0 ||
			s_Data.LineVertexCount > 0);
		if (hasPendingGeometry)
		{
			Flush();
		}

		// Remove the current pass
		s_Data.RenderPassStack.pop_back();

		// If a prior pass exists, restore its state
		if (!s_Data.RenderPassStack.empty())
		{
			const RenderPassState& restored = s_Data.RenderPassStack.back();
			s_Data.ViewProjectionMatrix = restored.ViewProjectionMatrix;

			glViewport(
				static_cast<int>(restored.ViewportBounds.x),
				static_cast<int>(restored.ViewportBounds.y),
				static_cast<int>(restored.ViewportBounds.z),
				static_cast<int>(restored.ViewportBounds.w)
			);

			s_Data.ViewportDimensions = { restored.ViewportBounds.z, restored.ViewportBounds.w };
		}

		// Reset batch counters for whatever comes next
		s_Data.QuadIndexCount = 0;
		s_Data.QuadVertexPtr = s_Data.QuadVertexBufferBase;
		s_Data.TextureSlotIndex = 1;
		s_Data.CurrentMaterial = s_Data.DefaultMaterial;

		s_Data.LineVertexCount = 0;
		s_Data.LineVertexBufferPtr = s_Data.LineVertexBufferBase;

		s_Data.CircleIndexCount = 0;
		s_Data.CircleVertexBufferPtr = s_Data.CircleVertexBufferBase;
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Legacy Scene Wrappers (backward-compatible shims over PushRenderPass)
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer2D::BeginScene(const OrthographicCamera& camera)
	{
		// Derive full-window bounds from the current tracked viewport dimensions
		glm::vec4 fullWindowBounds = {
			0.0f,
			0.0f,
			s_Data.ViewportDimensions.x,
			s_Data.ViewportDimensions.y
		};

		PushRenderPass(camera.GetViewProjectionMatrix(), fullWindowBounds);
	}

	void Renderer2D::EndScene()
	{
		PopRenderPass();
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Flush
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer2D::Flush()
	{
		// --- Draw Quads ---
		if (s_Data.QuadIndexCount != 0)
		{
			uint32_t dataSize = (uint32_t)((uint8_t*)s_Data.QuadVertexPtr - (uint8_t*)s_Data.QuadVertexBufferBase);
			s_Data.QuadVertexBuffer->SetData(s_Data.QuadVertexBufferBase, dataSize);

			for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
				s_Data.TextureSlots[i]->Bind(i);

			Ref<Shader> activeShader = s_Data.TextureShader;
			if (s_Data.CurrentMaterial)
			{
				s_Data.CurrentMaterial->Bind();
				activeShader = s_Data.CurrentMaterial->GetShader();
			}
			else
			{
				s_Data.TextureShader->Bind();
			}

			activeShader->SetMat4("u_ViewProjection", s_Data.ViewProjectionMatrix);
			activeShader->SetFloat2("u_ViewportSize", s_Data.ViewportDimensions);

			s_Data.QuadVertexArray->Bind();
			RenderCommand::DrawIndexed(s_Data.QuadVertexArray, s_Data.QuadIndexCount);
			s_Data.Stats.DrawCalls++;
		}

		// --- Draw Lines ---
		if (s_Data.LineVertexCount != 0)
		{
			uint32_t dataSize = (uint32_t)((uint8_t*)s_Data.LineVertexBufferPtr - (uint8_t*)s_Data.LineVertexBufferBase);
			s_Data.LineVertexBuffer->SetData(s_Data.LineVertexBufferBase, dataSize);

			s_Data.LineShader->Bind();
			s_Data.LineShader->SetMat4("u_ViewProjection", s_Data.ViewProjectionMatrix);

			s_Data.LineVertexArray->Bind();
			RenderCommand::DrawLines(s_Data.LineVertexArray, s_Data.LineVertexCount);
			s_Data.Stats.DrawCalls++;
		}

		// --- Draw Circles (SDF) ---
		if (s_Data.CircleIndexCount != 0)
		{
			uint32_t dataSize = (uint32_t)((uint8_t*)s_Data.CircleVertexBufferPtr - (uint8_t*)s_Data.CircleVertexBufferBase);
			s_Data.CircleVertexBuffer->SetData(s_Data.CircleVertexBufferBase, dataSize);

			s_Data.CircleShader->Bind();
			s_Data.CircleShader->SetMat4("u_ViewProjection", s_Data.ViewProjectionMatrix);

			s_Data.CircleVertexArray->Bind();
			RenderCommand::DrawIndexed(s_Data.CircleVertexArray, s_Data.CircleIndexCount);
			s_Data.Stats.DrawCalls++;
		}
	}

	/////////////////////////////////////////////////////////////////////////////////
	// FlushAndReset (internal — mid-batch state transition helper)
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer2D::FlushAndReset()
	{
		// Preserve the active material across the reset so DrawQuad can continue
		// into the same material bucket without a spurious batch break.
		Ref<Material> activeMaterial = s_Data.CurrentMaterial;

		Flush();

		// Reset counters only — do NOT pop/push the render pass stack
		s_Data.QuadIndexCount = 0;
		s_Data.QuadVertexPtr = s_Data.QuadVertexBufferBase;
		s_Data.TextureSlotIndex = 1;

		s_Data.LineVertexCount = 0;
		s_Data.LineVertexBufferPtr = s_Data.LineVertexBufferBase;

		s_Data.CircleIndexCount = 0;
		s_Data.CircleVertexBufferPtr = s_Data.CircleVertexBufferBase;

		s_Data.CurrentMaterial = activeMaterial;
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Viewport
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer2D::SetViewportSize(uint32_t width, uint32_t height)
	{
		s_Data.ViewportDimensions = { (float)width, (float)height };
	}

	/////////////////////////////////////////////////////////////////////////////////
	// DrawQuad — Flat Color
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color)
	{
		if (s_Data.CurrentMaterial != s_Data.DefaultMaterial) FlushAndReset();
		if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices) FlushAndReset();

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
			* glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

		for (uint32_t i = 0; i < 4; i++)
		{
			s_Data.QuadVertexPtr->Position = transform * s_Data.QuadVertexPositions[i];
			s_Data.QuadVertexPtr->Color = color;
			s_Data.QuadVertexPtr->TexCoord = { (i == 0 || i == 3) ? 0.0f : 1.0f, (i == 0 || i == 1) ? 0.0f : 1.0f };
			s_Data.QuadVertexPtr->TexIndex = 0.0f;
			s_Data.QuadVertexPtr->TilingFactor = 1.0f;
			s_Data.QuadVertexPtr++;
		}
		s_Data.QuadIndexCount += 6;
		s_Data.Stats.QuadCount++;
	}

	void Renderer2D::DrawQuad(const glm::vec2& pos, const glm::vec2& size, const glm::vec4& col)
	{
		DrawQuad({ pos.x, pos.y, 0.0f }, size, col);
	}

	/////////////////////////////////////////////////////////////////////////////////
	// DrawQuad — Texture
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Texture>& texture, float tilingFactor, const glm::vec4& tintColor)
	{
		if (!texture)
		{
			CS_CORE_WARN("Renderer2D: DrawQuad received null texture. Falling back to white.");
			DrawQuad(position, size, tintColor);
			return;
		}

		if (s_Data.CurrentMaterial != s_Data.DefaultMaterial) FlushAndReset();
		if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices) FlushAndReset();

		float textureIndex = -1.0f;
		for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
		{
			if (s_Data.TextureSlots[i]->GetRendererID() == texture->GetRendererID())
			{
				textureIndex = (float)i;
				break;
			}
		}
		if (textureIndex == -1.0f)
		{
			if (s_Data.TextureSlotIndex >= Renderer2DData::MaxTextureSlots) FlushAndReset();
			textureIndex = (float)s_Data.TextureSlotIndex;
			s_Data.TextureSlots[s_Data.TextureSlotIndex++] = texture;
		}

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
			* glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

		constexpr glm::vec2 texCoords[] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
		for (uint32_t i = 0; i < 4; i++)
		{
			s_Data.QuadVertexPtr->Position = transform * s_Data.QuadVertexPositions[i];
			s_Data.QuadVertexPtr->Color = tintColor;
			s_Data.QuadVertexPtr->TexCoord = texCoords[i];
			s_Data.QuadVertexPtr->TexIndex = textureIndex;
			s_Data.QuadVertexPtr->TilingFactor = tilingFactor;
			s_Data.QuadVertexPtr++;
		}
		s_Data.QuadIndexCount += 6;
		s_Data.Stats.QuadCount++;
	}

	void Renderer2D::DrawQuad(const glm::vec2& pos, const glm::vec2& size, const Ref<Texture>& tex, float tiling, const glm::vec4& tint)
	{
		DrawQuad({ pos.x, pos.y, 0.0f }, size, tex, tiling, tint);
	}

	/////////////////////////////////////////////////////////////////////////////////
	// DrawQuad — Material
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Material>& material)
	{
		if (!material) return;

		if (s_Data.CurrentMaterial != material) FlushAndReset();
		s_Data.CurrentMaterial = material;

		Ref<Texture> tex = material->GetTexture("u_Texture");
		if (!tex) tex = material->GetTexture("Texture");
		if (!tex) tex = material->GetTexture("u_Textures");
		if (!tex) tex = s_Data.WhiteTexture;

		glm::vec4 color = material->GetVector("u_Color");

		if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices) FlushAndReset();

		float textureIndex = -1.0f;
		for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
		{
			if (s_Data.TextureSlots[i]->GetRendererID() == tex->GetRendererID())
			{
				textureIndex = (float)i;
				break;
			}
		}
		if (textureIndex == -1.0f)
		{
			if (s_Data.TextureSlotIndex >= Renderer2DData::MaxTextureSlots) FlushAndReset();
			textureIndex = (float)s_Data.TextureSlotIndex;
			s_Data.TextureSlots[s_Data.TextureSlotIndex++] = tex;
		}

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
			* glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

		constexpr glm::vec2 texCoords[] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
		for (uint32_t i = 0; i < 4; i++)
		{
			s_Data.QuadVertexPtr->Position = transform * s_Data.QuadVertexPositions[i];
			s_Data.QuadVertexPtr->Color = color;
			s_Data.QuadVertexPtr->TexCoord = texCoords[i];
			s_Data.QuadVertexPtr->TexIndex = textureIndex;
			s_Data.QuadVertexPtr->TilingFactor = 1.0f;
			s_Data.QuadVertexPtr++;
		}
		s_Data.QuadIndexCount += 6;
		s_Data.Stats.QuadCount++;
	}

	void Renderer2D::DrawQuad(const glm::vec2& pos, const glm::vec2& size, const Ref<Material>& material)
	{
		DrawQuad({ pos.x, pos.y, 0.0f }, size, material);
	}

	/////////////////////////////////////////////////////////////////////////////////
	// DrawQuad — SubTexture2D
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<SubTexture2D>& subTexture, const glm::vec4& tintColor)
	{
		if (s_Data.CurrentMaterial != s_Data.DefaultMaterial) FlushAndReset();
		if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices) FlushAndReset();

		Ref<Texture> texture = subTexture->GetTexture();
		float textureIndex = -1.0f;
		for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
		{
			if (s_Data.TextureSlots[i]->GetRendererID() == texture->GetRendererID())
			{
				textureIndex = (float)i;
				break;
			}
		}
		if (textureIndex == -1.0f)
		{
			if (s_Data.TextureSlotIndex >= Renderer2DData::MaxTextureSlots) FlushAndReset();
			textureIndex = (float)s_Data.TextureSlotIndex;
			s_Data.TextureSlots[s_Data.TextureSlotIndex++] = texture;
		}

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
			* glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

		const glm::vec2* texCoords = subTexture->GetTexCoords();
		for (uint32_t i = 0; i < 4; i++)
		{
			s_Data.QuadVertexPtr->Position = transform * s_Data.QuadVertexPositions[i];
			s_Data.QuadVertexPtr->Color = tintColor;
			s_Data.QuadVertexPtr->TexCoord = texCoords[i];
			s_Data.QuadVertexPtr->TexIndex = textureIndex;
			s_Data.QuadVertexPtr->TilingFactor = 1.0f;
			s_Data.QuadVertexPtr++;
		}
		s_Data.QuadIndexCount += 6;
		s_Data.Stats.QuadCount++;
	}

	void Renderer2D::DrawQuad(const glm::vec2& pos, const glm::vec2& size, const Ref<SubTexture2D>& subTexture, const glm::vec4& tintColor)
	{
		DrawQuad({ pos.x, pos.y, 0.0f }, size, subTexture, tintColor);
	}

	/////////////////////////////////////////////////////////////////////////////////
	// DrawRotatedQuad — Color
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer2D::DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const glm::vec4& color)
	{
		if (s_Data.CurrentMaterial != s_Data.DefaultMaterial) FlushAndReset();
		if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices) FlushAndReset();

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
			* glm::rotate(glm::mat4(1.0f), rotation, { 0.0f, 0.0f, 1.0f })
			* glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

		for (uint32_t i = 0; i < 4; i++)
		{
			s_Data.QuadVertexPtr->Position = transform * s_Data.QuadVertexPositions[i];
			s_Data.QuadVertexPtr->Color = color;
			s_Data.QuadVertexPtr->TexCoord = { (i == 0 || i == 3) ? 0.0f : 1.0f, (i == 0 || i == 1) ? 0.0f : 1.0f };
			s_Data.QuadVertexPtr->TexIndex = 0.0f;
			s_Data.QuadVertexPtr->TilingFactor = 1.0f;
			s_Data.QuadVertexPtr++;
		}
		s_Data.QuadIndexCount += 6;
		s_Data.Stats.QuadCount++;
	}

	void Renderer2D::DrawRotatedQuad(const glm::vec2& pos, const glm::vec2& size, float rot, const glm::vec4& col)
	{
		DrawRotatedQuad({ pos.x, pos.y, 0.0f }, size, rot, col);
	}

	/////////////////////////////////////////////////////////////////////////////////
	// DrawRotatedQuad — Texture
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer2D::DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<Texture>& texture, float tilingFactor, const glm::vec4& tintColor)
	{
		if (s_Data.CurrentMaterial != s_Data.DefaultMaterial) FlushAndReset();
		if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices) FlushAndReset();

		float textureIndex = -1.0f;
		for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
		{
			if (s_Data.TextureSlots[i]->GetRendererID() == texture->GetRendererID())
			{
				textureIndex = (float)i;
				break;
			}
		}
		if (textureIndex == -1.0f)
		{
			if (s_Data.TextureSlotIndex >= Renderer2DData::MaxTextureSlots) FlushAndReset();
			textureIndex = (float)s_Data.TextureSlotIndex;
			s_Data.TextureSlots[s_Data.TextureSlotIndex++] = texture;
		}

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
			* glm::rotate(glm::mat4(1.0f), rotation, { 0.0f, 0.0f, 1.0f })
			* glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

		constexpr glm::vec2 texCoords[] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
		for (uint32_t i = 0; i < 4; i++)
		{
			s_Data.QuadVertexPtr->Position = transform * s_Data.QuadVertexPositions[i];
			s_Data.QuadVertexPtr->Color = tintColor;
			s_Data.QuadVertexPtr->TexCoord = texCoords[i];
			s_Data.QuadVertexPtr->TexIndex = textureIndex;
			s_Data.QuadVertexPtr->TilingFactor = tilingFactor;
			s_Data.QuadVertexPtr++;
		}
		s_Data.QuadIndexCount += 6;
		s_Data.Stats.QuadCount++;
	}

	void Renderer2D::DrawRotatedQuad(const glm::vec2& pos, const glm::vec2& size, float rot, const Ref<Texture>& tex, float tiling, const glm::vec4& tint)
	{
		DrawRotatedQuad({ pos.x, pos.y, 0.0f }, size, rot, tex, tiling, tint);
	}

	/////////////////////////////////////////////////////////////////////////////////
	// DrawRotatedQuad — Material
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer2D::DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<Material>& material)
	{
		if (!material) return;

		if (s_Data.CurrentMaterial != material) FlushAndReset();
		s_Data.CurrentMaterial = material;

		Ref<Texture> tex = material->GetTexture("u_Texture");
		if (!tex) tex = material->GetTexture("Texture");
		if (!tex) tex = material->GetTexture("u_Textures");
		if (!tex) tex = s_Data.WhiteTexture;

		glm::vec4 color = material->GetVector("u_Color");

		if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices) FlushAndReset();

		float textureIndex = -1.0f;
		for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
		{
			if (s_Data.TextureSlots[i]->GetRendererID() == tex->GetRendererID())
			{
				textureIndex = (float)i;
				break;
			}
		}
		if (textureIndex == -1.0f)
		{
			if (s_Data.TextureSlotIndex >= Renderer2DData::MaxTextureSlots) FlushAndReset();
			textureIndex = (float)s_Data.TextureSlotIndex;
			s_Data.TextureSlots[s_Data.TextureSlotIndex++] = tex;
		}

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
			* glm::rotate(glm::mat4(1.0f), rotation, { 0.0f, 0.0f, 1.0f })
			* glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

		constexpr glm::vec2 texCoords[] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
		for (uint32_t i = 0; i < 4; i++)
		{
			s_Data.QuadVertexPtr->Position = transform * s_Data.QuadVertexPositions[i];
			s_Data.QuadVertexPtr->Color = color;
			s_Data.QuadVertexPtr->TexCoord = texCoords[i];
			s_Data.QuadVertexPtr->TexIndex = textureIndex;
			s_Data.QuadVertexPtr->TilingFactor = 1.0f;
			s_Data.QuadVertexPtr++;
		}
		s_Data.QuadIndexCount += 6;
		s_Data.Stats.QuadCount++;
	}

	/////////////////////////////////////////////////////////////////////////////////
	// DrawRotatedQuad — SubTexture2D
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer2D::DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<SubTexture2D>& subTexture, const glm::vec4& tintColor)
	{
		if (s_Data.CurrentMaterial != s_Data.DefaultMaterial) FlushAndReset();
		if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices) FlushAndReset();

		Ref<Texture> texture = subTexture->GetTexture();
		float textureIndex = -1.0f;
		for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
		{
			if (s_Data.TextureSlots[i]->GetRendererID() == texture->GetRendererID())
			{
				textureIndex = (float)i;
				break;
			}
		}
		if (textureIndex == -1.0f)
		{
			if (s_Data.TextureSlotIndex >= Renderer2DData::MaxTextureSlots) FlushAndReset();
			textureIndex = (float)s_Data.TextureSlotIndex;
			s_Data.TextureSlots[s_Data.TextureSlotIndex++] = texture;
		}

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
			* glm::rotate(glm::mat4(1.0f), rotation, { 0.0f, 0.0f, 1.0f })
			* glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

		const glm::vec2* texCoords = subTexture->GetTexCoords();
		for (uint32_t i = 0; i < 4; i++)
		{
			s_Data.QuadVertexPtr->Position = transform * s_Data.QuadVertexPositions[i];
			s_Data.QuadVertexPtr->Color = tintColor;
			s_Data.QuadVertexPtr->TexCoord = texCoords[i];
			s_Data.QuadVertexPtr->TexIndex = textureIndex;
			s_Data.QuadVertexPtr->TilingFactor = 1.0f;
			s_Data.QuadVertexPtr++;
		}
		s_Data.QuadIndexCount += 6;
		s_Data.Stats.QuadCount++;
	}

	void Renderer2D::DrawRotatedQuad(const glm::vec2& pos, const glm::vec2& size, float rot, const Ref<SubTexture2D>& subTexture, const glm::vec4& tint)
	{
		DrawRotatedQuad({ pos.x, pos.y, 0.0f }, size, rot, subTexture, tint);
	}

	/////////////////////////////////////////////////////////////////////////////////
	// DrawCircle (SDF)
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer2D::DrawCircle(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color, float thickness, float fade)
	{
		if (s_Data.CircleIndexCount >= Renderer2DData::MaxCircleIndices) FlushAndReset();

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
			* glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

		constexpr glm::vec2 localPositions[4] = {
			{ -1.0f, -1.0f },
			{  1.0f, -1.0f },
			{  1.0f,  1.0f },
			{ -1.0f,  1.0f }
		};

		// Normalize color if it appears to be in 0-255 range
		glm::vec4 normalizedColor = color;
		if (color.r > 1.0f || color.g > 1.0f || color.b > 1.0f || color.a > 1.0f)
			normalizedColor = color / 255.0f;

		for (uint32_t i = 0; i < 4; i++)
		{
			s_Data.CircleVertexBufferPtr->WorldPosition = transform * s_Data.QuadVertexPositions[i];
			s_Data.CircleVertexBufferPtr->LocalPosition = localPositions[i];
			s_Data.CircleVertexBufferPtr->Color = normalizedColor;
			s_Data.CircleVertexBufferPtr->Thickness = thickness;
			s_Data.CircleVertexBufferPtr->Fade = fade;
			s_Data.CircleVertexBufferPtr++;
		}

		s_Data.CircleIndexCount += 6;
		s_Data.Stats.QuadCount++;
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Debug Geometry
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer2D::DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color)
	{
		if (s_Data.LineVertexCount >= Renderer2DData::MaxVertices - 1) FlushAndReset();

		s_Data.LineVertexBufferPtr->Position = p0;
		s_Data.LineVertexBufferPtr->Color = color;
		s_Data.LineVertexBufferPtr++;

		s_Data.LineVertexBufferPtr->Position = p1;
		s_Data.LineVertexBufferPtr->Color = color;
		s_Data.LineVertexBufferPtr++;

		s_Data.LineVertexCount += 2;
	}

	void Renderer2D::DrawRect(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color)
	{
		glm::vec3 p0 = { position.x - size.x * 0.5f, position.y - size.y * 0.5f, position.z };
		glm::vec3 p1 = { position.x + size.x * 0.5f, position.y - size.y * 0.5f, position.z };
		glm::vec3 p2 = { position.x + size.x * 0.5f, position.y + size.y * 0.5f, position.z };
		glm::vec3 p3 = { position.x - size.x * 0.5f, position.y + size.y * 0.5f, position.z };

		DrawLine(p0, p1, color);
		DrawLine(p1, p2, color);
		DrawLine(p2, p3, color);
		DrawLine(p3, p0, color);
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Telemetry
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer2D::SetStatsStatus(bool enabled) { s_Data.StatsEnabled = enabled; }
	Renderer2D::Statistics Renderer2D::GetStats() { return s_Data.Stats; }
	void Renderer2D::ResetStats() { memset(&s_Data.Stats, 0, sizeof(Statistics)); }

} // namespace Cosmic