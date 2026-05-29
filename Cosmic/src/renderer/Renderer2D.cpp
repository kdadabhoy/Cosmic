// Renderer2D.cpp
// Last Modified: 5/24/2026

#include "renderer/Renderer2D.h"
#include "graphics/VertexArray.h"
#include "graphics/Shader.h"
#include "renderer/RenderCommand.h"
#include "core/Log.h"
#include "graphics/SubTexture2D.h"

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
		// =========================================================================
		// Pipeline Allocation Limits
		// =========================================================================
		static const uint32_t MaxQuads = 10000;
		static const uint32_t MaxVertices = MaxQuads * 4;
		static const uint32_t MaxIndices = MaxQuads * 6;
		static const uint32_t MaxTextureSlots = 32;

		static const uint32_t MaxLines = 10000;
		static const uint32_t MaxLineVertices = MaxLines * 2;

		static const uint32_t MaxCircles = 10000;
		static const uint32_t MaxCircleVertices = MaxCircles * 4;
		static const uint32_t MaxCircleIndices = MaxCircles * 6;

		// Hardware Limit ceiling for dedicated instancing pipeline chunks
		static const uint32_t MaxInstancedCircles = 20000;

		// =========================================================================
		// --- Quad Batch Data ---
		// =========================================================================
		Ref<VertexArray>  QuadVertexArray;
		Ref<VertexBuffer> QuadVertexBuffer;
		Ref<Shader>       TextureShader;
		Ref<Texture>      WhiteTexture; // Keeps original base Ref<Texture> typing

		Ref<Material> CurrentMaterial = nullptr;
		Ref<Material> DefaultMaterial = nullptr;

		uint32_t      QuadIndexCount = 0;
		QuadVertex* QuadVertexBufferBase = nullptr;
		QuadVertex* QuadVertexPtr = nullptr; // Restored from original implementation

		// =========================================================================
		// --- Line Batch Data ---
		// =========================================================================
		Ref<VertexArray>  LineVertexArray;
		Ref<VertexBuffer> LineVertexBuffer;
		Ref<Shader>       LineShader;

		uint32_t          LineVertexCount = 0; // Restored from original implementation
		LineVertex* LineVertexBufferBase = nullptr;
		LineVertex* LineVertexBufferPtr = nullptr;

		// =========================================================================
		// --- Classic Batch Circle Data ---
		// =========================================================================
		Ref<VertexArray>  CircleVertexArray;
		Ref<VertexBuffer> CircleVertexBuffer;
		Ref<Shader>       DefaultCircleShader; // Fallback core engine shader (Circle.glsl)
		Ref<Shader>       ActiveCircleShader;  // Current bound shader in active batch

		uint32_t          CircleIndexCount = 0;
		CircleVertex* CircleVertexBufferBase = nullptr;
		CircleVertex* CircleVertexBufferPtr = nullptr;

		// =========================================================================
		// --- Instanced Circle Pipeline Assets ---
		// =========================================================================
		Ref<VertexArray>  InstancedCircleVAO;
		Ref<VertexBuffer> InstancedCircleQuadVBO;
		Ref<VertexBuffer> InstancedCircleInstanceVBO;
		Ref<Shader>       DefaultInstancedCircleShader; // Dedicated (CircleInstance.glsl)


		// =========================================================================
		// --- Instanced Quad Pipeline Assets ---
		// =========================================================================
		Ref<VertexArray>  InstancedQuadVAO;
		Ref<VertexBuffer> InstancedQuadBaseVBO;      // Shared unit-quad geometry
		Ref<VertexBuffer> InstancedQuadInstanceVBO;  // Per-instance stream
		Ref<Shader>       DefaultInstancedQuadShader; // QuadInstance.glsl

		// Hardware ceiling matching the circle instancing limit
		static const uint32_t MaxInstancedQuads = 20000;


		// =========================================================================
		// --- Global Texture State ---
		// =========================================================================
		std::array<Ref<Texture>, MaxTextureSlots> TextureSlots; // Restored Ref<Texture> array
		uint32_t TextureSlotIndex = 1; // Slot 0 = White Texture

		glm::vec4 QuadVertexPositions[4];

		// =========================================================================
		// --- Scene-Wide Uniform Tracking ---
		// =========================================================================
		glm::mat4 ViewProjectionMatrix{ 1.0f };
		glm::vec2 ViewportDimensions{ 1280.0f, 720.0f }; // Restored from original implementation

		// =========================================================================
		// --- Telemetry & Profiling ---
		// =========================================================================
		Renderer2D::Statistics Stats;
		bool StatsEnabled = false; // Restored from original implementation

		// =========================================================================
		// --- Render Pass Stack ---
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

		// =========================================================================
		// --- Quad Batch Initialization ---
		// =========================================================================
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

		// Allocate host-side staging buffer and bind runtime writing pointer
		s_Data.QuadVertexBufferBase = new QuadVertex[s_Data.MaxVertices];
		s_Data.QuadVertexPtr = s_Data.QuadVertexBufferBase; // RESTORED FIX

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

		// --- White Texture Asset Mapping (Slot 0 fallback) ---
		s_Data.WhiteTexture = Texture2D::Create(1, 1);
		uint32_t whiteTextureData = 0xffffffff;
		s_Data.WhiteTexture->SetData(&whiteTextureData, sizeof(uint32_t));

		int32_t samplers[s_Data.MaxTextureSlots];
		for (uint32_t i = 0; i < s_Data.MaxTextureSlots; i++)
			samplers[i] = i;

		s_Data.TextureShader = Shader::Create("assets/shaders/Texture.glsl");
		CS_CORE_ASSERT(s_Data.TextureShader, "Renderer2D: Failed to load core Texture shader — engine cannot continue.");
		s_Data.TextureShader->Bind();
		s_Data.TextureShader->SetIntArray("u_Textures", samplers, s_Data.MaxTextureSlots);

		s_Data.DefaultMaterial = Material::Create(s_Data.TextureShader, "Cosmic_Default_Material");
		s_Data.TextureSlots[0] = s_Data.WhiteTexture;

		s_Data.QuadVertexPositions[0] = { -0.5f, -0.5f, 0.0f, 1.0f };
		s_Data.QuadVertexPositions[1] = { 0.5f, -0.5f, 0.0f, 1.0f };
		s_Data.QuadVertexPositions[2] = { 0.5f,  0.5f, 0.0f, 1.0f };
		s_Data.QuadVertexPositions[3] = { -0.5f,  0.5f, 0.0f, 1.0f };

		// =========================================================================
		// --- Line Batch Initialization ---
		// =========================================================================
		s_Data.LineVertexArray = VertexArray::Create();
		s_Data.LineVertexBuffer = VertexBuffer::Create(s_Data.MaxLineVertices * sizeof(LineVertex));
		s_Data.LineVertexBuffer->SetLayout({
			{ ShaderDataType::Float3, "a_Position" },
			{ ShaderDataType::Float4, "a_Color"    }
			});
		s_Data.LineVertexArray->AddVertexBuffer(s_Data.LineVertexBuffer);

		// Allocate host-side staging buffer and bind runtime writing pointer
		s_Data.LineVertexBufferBase = new LineVertex[s_Data.MaxLineVertices];
		s_Data.LineVertexBufferPtr = s_Data.LineVertexBufferBase; // RESTORED FIX

		s_Data.LineShader = Shader::Create("assets/shaders/Line.glsl");
		if (!s_Data.LineShader)
			CS_CORE_ERROR("Renderer2D: Failed to load Line shader!");

		// =========================================================================
		// --- Classic Batch Circle Initialization ---
		// =========================================================================
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
		s_Data.CircleVertexBufferPtr = s_Data.CircleVertexBufferBase; // RESTORED FIX
		s_Data.CircleVertexArray->SetIndexBuffer(quadIB);

		s_Data.DefaultCircleShader = Shader::Create("assets/shaders/Circle.glsl");
		s_Data.ActiveCircleShader = s_Data.DefaultCircleShader;
		if (!s_Data.DefaultCircleShader)
			CS_CORE_ERROR("Renderer2D: Failed to load Engine Default Batch Circle shader!");

		// =========================================================================
		// --- Instanced Circle Hardware Pipeline Initialization ---
		// =========================================================================
		s_Data.InstancedCircleVAO = VertexArray::Create();

		// Base mesh shared layout geometry [-0.5, 0.5] unit-bounds quad configuration
		glm::vec2 quadVertices[4] = {
			{ -0.5f, -0.5f },
			{  0.5f, -0.5f },
			{  0.5f,  0.5f },
			{ -0.5f,  0.5f }
		};

		s_Data.InstancedCircleQuadVBO = VertexBuffer::Create(4 * sizeof(glm::vec2));
		s_Data.InstancedCircleQuadVBO->SetLayout({
			{ ShaderDataType::Float2, "a_LocalPosition" }
			});
		s_Data.InstancedCircleVAO->AddVertexBuffer(s_Data.InstancedCircleQuadVBO);
		s_Data.InstancedCircleQuadVBO->SetData(quadVertices, sizeof(quadVertices));

		// Base index template mapping configuration
		uint32_t instancedIndices[6] = { 0, 1, 2, 2, 3, 0 };
		Ref<IndexBuffer> instancedIB = IndexBuffer::Create(instancedIndices, 6);
		s_Data.InstancedCircleVAO->SetIndexBuffer(instancedIB);

		// Setup instance data stream VBO allocation
		s_Data.InstancedCircleInstanceVBO = VertexBuffer::Create(s_Data.MaxInstancedCircles * sizeof(Renderer2D::InstanceCircleData));

		// SAFE STRUCTURAL RE-ALIGNMENT:
		// We add the per-element data layouts using your internal engine enum conventions.
		// To cleanly bypass the cross-contamination bug, we explicitly supply the 
		// "instanced" flag argument (or update layout fields sequentially) so that your 
		// internal `VertexArray::AddVertexBuffer` class handles calling `glVertexAttribDivisor` 
		// locally only inside this specific VAO context footprint.
		BufferLayout instanceDataLayout = {
			{ ShaderDataType::Float3, "a_InstanceWorldPosition" },
			{ ShaderDataType::Float2, "a_InstanceScale"         },
			{ ShaderDataType::Float4, "a_InstanceColor"         },
			{ ShaderDataType::Float,  "a_InstanceThickness"     },
			{ ShaderDataType::Float,  "a_InstanceFade"          }
		};

		// Tag elements as instanced streams so VertexArray natively enables divisors automatically
		for (auto& element : instanceDataLayout)
		{
			element.Instanced = true;
		}

		s_Data.InstancedCircleInstanceVBO->SetLayout(instanceDataLayout);
		s_Data.InstancedCircleVAO->AddVertexBuffer(s_Data.InstancedCircleInstanceVBO);

		// Load specialized pipeline hardware shader companion program
		s_Data.DefaultInstancedCircleShader = Shader::Create("assets/shaders/CircleInstance.glsl");
		if (!s_Data.DefaultInstancedCircleShader)
			CS_CORE_ERROR("Renderer2D: Failed to load Engine Default Instanced Circle shader (CircleInstance.glsl)!");



		// =========================================================================
		// --- Instanced Quad Hardware Pipeline Initialization ---
		// =========================================================================
		s_Data.InstancedQuadVAO = VertexArray::Create();

		// Shared base geometry: a unit quad with corners at [-0.5, 0.5].
		// Matches the local-position convention expected by QuadInstance.glsl.
		glm::vec2 quadCorners[4] = {
			{ -0.5f, -0.5f },  // Bottom-left
			{  0.5f, -0.5f },  // Bottom-right
			{  0.5f,  0.5f },  // Top-right
			{ -0.5f,  0.5f }   // Top-left
		};

		s_Data.InstancedQuadBaseVBO = VertexBuffer::Create(sizeof(quadCorners));
		s_Data.InstancedQuadBaseVBO->SetLayout({
			{ ShaderDataType::Float2, "a_LocalPosition" }  // location 0, divisor 0
			});
		s_Data.InstancedQuadVAO->AddVertexBuffer(s_Data.InstancedQuadBaseVBO);
		s_Data.InstancedQuadBaseVBO->SetData(quadCorners, sizeof(quadCorners));

		// Reuse the pre-built quad index buffer (0,1,2,2,3,0 winding)
		uint32_t instancedQuadIndices[6] = { 0, 1, 2, 2, 3, 0 };
		Ref<IndexBuffer> instancedQuadIB = IndexBuffer::Create(instancedQuadIndices, 6);
		s_Data.InstancedQuadVAO->SetIndexBuffer(instancedQuadIB);

		// Per-instance stream — allocate for MaxInstancedQuads entries.
		s_Data.InstancedQuadInstanceVBO = VertexBuffer::Create(
			s_Data.MaxInstancedQuads * sizeof(Renderer2D::InstanceQuadData));

		// Build the per-instance layout.
		// Every element is flagged Instanced = true so AddVertexBuffer sets
		// glVertexAttribDivisor(location, 1) for each attribute.
		// Attribute indices start at 1 because location 0 is the base VBO above.
		BufferLayout instancedQuadLayout = {
			{ ShaderDataType::Float3, "a_InstanceWorldPosition" },   // loc 1
			{ ShaderDataType::Float2, "a_InstanceScale"          },  // loc 2
			{ ShaderDataType::Float4, "a_InstanceColor"          },  // loc 3
			{ ShaderDataType::Float2, "a_InstanceTexCoordOffset" },  // loc 4
			{ ShaderDataType::Float2, "a_InstanceTexCoordScale"  },  // loc 5
			{ ShaderDataType::Float,  "a_InstanceTexIndex"       },  // loc 6
			{ ShaderDataType::Float,  "a_InstanceTilingFactor"   }   // loc 7
		};

		// Mark every element as per-instance so OpenGLVertexArray::AddVertexBuffer
		// calls glVertexAttribDivisor(n, 1) for each location, exactly mirroring
		// the circle instancing setup.
		for (auto& element : instancedQuadLayout)
			element.Instanced = true;

		s_Data.InstancedQuadInstanceVBO->SetLayout(instancedQuadLayout);
		s_Data.InstancedQuadVAO->AddVertexBuffer(s_Data.InstancedQuadInstanceVBO);

		// Load the instanced quad shader
		s_Data.DefaultInstancedQuadShader = Shader::Create("assets/shaders/QuadInstance.glsl");
		if (!s_Data.DefaultInstancedQuadShader)
			CS_CORE_ERROR("Renderer2D: Failed to load QuadInstance.glsl!");


		CS_CORE_INFO("Renderer2D initialized successfully.");
	}

	void Renderer2D::Shutdown()
	{
		CS_CORE_TRACE("Shutting down Renderer2D");

		delete[] s_Data.QuadVertexBufferBase;
		delete[] s_Data.LineVertexBufferBase;
		delete[] s_Data.CircleVertexBufferBase;

		s_Data.QuadVertexBufferBase   = nullptr;
		s_Data.QuadVertexPtr          = nullptr;
		s_Data.LineVertexBufferBase   = nullptr;
		s_Data.LineVertexBufferPtr    = nullptr;
		s_Data.CircleVertexBufferBase = nullptr;
		s_Data.CircleVertexBufferPtr  = nullptr;

		s_Data.RenderPassStack.clear();

		// Instanced circle pipeline
		s_Data.InstancedCircleVAO.reset();
		s_Data.InstancedCircleQuadVBO.reset();
		s_Data.InstancedCircleInstanceVBO.reset();

		// Instanced quad pipeline
		s_Data.InstancedQuadVAO.reset();
		s_Data.InstancedQuadBaseVBO.reset();
		s_Data.InstancedQuadInstanceVBO.reset();
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
		RenderCommand::SetViewport(
			static_cast<uint32_t>(viewportBounds.x),
			static_cast<uint32_t>(viewportBounds.y),
			static_cast<uint32_t>(viewportBounds.z),
			static_cast<uint32_t>(viewportBounds.w)
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

		// Reset circle batch counters and restore state to the default core fallback shader
		s_Data.CircleIndexCount = 0;
		s_Data.CircleVertexBufferPtr = s_Data.CircleVertexBufferBase;
		s_Data.ActiveCircleShader = s_Data.DefaultCircleShader; // Prevent custom shader leakage
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

			RenderCommand::SetViewport(
				static_cast<uint32_t>(restored.ViewportBounds.x),
				static_cast<uint32_t>(restored.ViewportBounds.y),
				static_cast<uint32_t>(restored.ViewportBounds.z),
				static_cast<uint32_t>(restored.ViewportBounds.w)
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

		// Reset circle batch counters and restore state to the default core fallback shader
		s_Data.CircleIndexCount = 0;
		s_Data.CircleVertexBufferPtr = s_Data.CircleVertexBufferBase;
		s_Data.ActiveCircleShader = s_Data.DefaultCircleShader; // Prevent custom shader leakage
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
		s_Data.ActiveCircleShader = s_Data.DefaultCircleShader;
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
			if (s_Data.StatsEnabled) s_Data.Stats.DrawCalls++;
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
			if (s_Data.StatsEnabled) s_Data.Stats.DrawCalls++;
		}

		// --- Draw Circles (SDF) ---
		if (s_Data.CircleIndexCount != 0)
		{
			uint32_t dataSize = (uint32_t)((uint8_t*)s_Data.CircleVertexBufferPtr - (uint8_t*)s_Data.CircleVertexBufferBase);
			s_Data.CircleVertexBuffer->SetData(s_Data.CircleVertexBufferBase, dataSize);

			// FIX: Fall back safely to DefaultCircleShader if ActiveCircleShader was cleared out 
			// by the instancing system or not yet assigned.
			Ref<Shader> activeCircleShader = s_Data.ActiveCircleShader ? s_Data.ActiveCircleShader : s_Data.DefaultCircleShader;

			activeCircleShader->Bind();
			activeCircleShader->SetMat4("u_ViewProjection", s_Data.ViewProjectionMatrix);

			// OPTIMIZATION CONTRACT: Provide standard uniform contexts so that 
			// client shaders can sample layout properties and match your quad contract
			activeCircleShader->SetFloat2("u_ViewportSize", s_Data.ViewportDimensions);

			s_Data.CircleVertexArray->Bind();
			RenderCommand::DrawIndexed(s_Data.CircleVertexArray, s_Data.CircleIndexCount);
			if (s_Data.StatsEnabled) s_Data.Stats.DrawCalls++;
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

		// -------------------------------------------------------------------------
		// SHADER PIPELINE PERSISTENCE TRACKING
		// -------------------------------------------------------------------------
		// Preserve the active circle shader state across the hardware batch fence.
		// If a client passed a custom shader, we want to continue streaming into 
		// that same shader block after the flush without forcing an immediate, 
		// redundant state-change flush on the very next element.
		Ref<Shader> activeCircleShader = s_Data.ActiveCircleShader;

		Flush();

		// Reset counters only — do NOT pop/push the render pass stack
		s_Data.QuadIndexCount = 0;
		s_Data.QuadVertexPtr = s_Data.QuadVertexBufferBase;
		s_Data.TextureSlotIndex = 1;

		s_Data.LineVertexCount = 0;
		s_Data.LineVertexBufferPtr = s_Data.LineVertexBufferBase;

		// Reset circle buffer positions back to their origins
		s_Data.CircleIndexCount = 0;
		s_Data.CircleVertexBufferPtr = s_Data.CircleVertexBufferBase;

		// Restore tracking anchors safely to prevent state contamination 
		s_Data.CurrentMaterial = activeMaterial;
		s_Data.ActiveCircleShader = activeCircleShader;
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
		if (s_Data.StatsEnabled) s_Data.Stats.QuadCount++;
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
		if (s_Data.StatsEnabled) s_Data.Stats.QuadCount++;
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
		if (!tex) tex = s_Data.WhiteTexture;

		glm::vec4 color = material->GetVector4("u_Color");

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
		if (s_Data.StatsEnabled) s_Data.Stats.QuadCount++;
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
		if (s_Data.StatsEnabled) s_Data.Stats.QuadCount++;
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
		if (s_Data.StatsEnabled) s_Data.Stats.QuadCount++;
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
		if (s_Data.StatsEnabled) s_Data.Stats.QuadCount++;
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
		if (!tex) tex = s_Data.WhiteTexture;

		glm::vec4 color = material->GetVector4("u_Color");

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
		if (s_Data.StatsEnabled) s_Data.Stats.QuadCount++;
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
		if (s_Data.StatsEnabled) s_Data.Stats.QuadCount++;
	}

	void Renderer2D::DrawRotatedQuad(const glm::vec2& pos, const glm::vec2& size, float rot, const Ref<SubTexture2D>& subTexture, const glm::vec4& tint)
	{
		DrawRotatedQuad({ pos.x, pos.y, 0.0f }, size, rot, subTexture, tint);
	}

	/////////////////////////////////////////////////////////////////////////////////
	// DrawCircle (SDF)
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer2D::DrawCircle(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color, float thickness, float fade, Cosmic::Ref<Cosmic::Shader> customShader)
	{
		// Determine what shader we should be using for this batch call
		Cosmic::Ref<Cosmic::Shader> targetShader = customShader ? customShader : s_Data.DefaultCircleShader;

		// CRITICAL EDGE-CASE FALLBACK WORKAROUND:
		// If a client layer accidently drops scope on a custom shader handle during hot-reloads,
		// guarantee we fall back cleanly to preventing nullptr asset dereferencing exceptions.
		if (!targetShader)
		{
			targetShader = s_Data.DefaultCircleShader;
		}

		// State verification tracking
		if (s_Data.ActiveCircleShader != targetShader)
		{
			FlushAndReset();
			s_Data.ActiveCircleShader = targetShader;
		}

		// Flush geometry automatically if internal batch limits are saturated
		if (s_Data.CircleIndexCount >= Renderer2DData::MaxCircleIndices)
			FlushAndReset();

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
			* glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

		// Classic batching maps local texture spaces from [-1.0, 1.0] for the SDF fragment layout calculation
		constexpr glm::vec2 localPositions[4] = {
			{ -1.0f, -1.0f },
			{  1.0f, -1.0f },
			{  1.0f,  1.0f },
			{ -1.0f,  1.0f }
		};

		// Stage unique vertex entries sequentially to the batch stream array
		for (uint32_t i = 0; i < 4; i++)
		{
			s_Data.CircleVertexBufferPtr->WorldPosition = transform * s_Data.QuadVertexPositions[i];
			s_Data.CircleVertexBufferPtr->LocalPosition = localPositions[i];
			s_Data.CircleVertexBufferPtr->Color = color;
			s_Data.CircleVertexBufferPtr->Thickness = thickness;
			s_Data.CircleVertexBufferPtr->Fade = fade;
			s_Data.CircleVertexBufferPtr++;
		}

		s_Data.CircleIndexCount += 6;
		if (s_Data.StatsEnabled) s_Data.Stats.CircleCount++;
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Debug Geometry
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer2D::DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color)
	{
		if (s_Data.LineVertexCount >= Renderer2DData::MaxLineVertices - 1) FlushAndReset();

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





	//////////////////////////////
	///////
	void Renderer2D::DrawInstancedCircles(const InstanceCircleData* instances, uint32_t count, Ref<Shader> customShader)
	{
		if (!instances || count == 0) return;

		// =====================================================================
		// 1. PIPELINE ISOLATION
		// Flush all pending batched geometry (quads, lines, batch circles) before
		// changing VAO / shader bindings. Prevents state leakage in either
		// direction between the batch and instanced pipelines.
		// =====================================================================
		FlushAndReset();

		// =====================================================================
		// 2. SHADER SELECTION
		// Fall back to the default instanced circle shader if none is supplied.
		// If even the default is missing, bail with a log rather than crash.
		// =====================================================================
		Ref<Shader> targetShader = customShader
			? customShader
			: s_Data.DefaultInstancedCircleShader;

		if (!targetShader)
		{
			CS_CORE_ERROR("Renderer2D::DrawInstancedCircles: No valid shader available. "
				"Ensure CircleInstance.glsl loaded correctly during Init.");
			return;
		}

		targetShader->Bind();
		targetShader->SetMat4("u_ViewProjection", s_Data.ViewProjectionMatrix);

		// =====================================================================
		// 3. CHUNKED STREAMING LOOP
		// Stream instance data in MaxInstancedCircles-sized chunks, issuing one
		// abstracted draw call per chunk. Keeps GPU buffer usage bounded
		// regardless of how many instances are submitted per frame.
		// =====================================================================
		uint32_t remaining = count;
		uint32_t offset = 0;

		s_Data.InstancedCircleVAO->Bind();

		while (remaining > 0)
		{
			const uint32_t batchSize = (remaining < Renderer2DData::MaxInstancedCircles)
				? remaining
				: Renderer2DData::MaxInstancedCircles;

			// Upload only the slice of instance data needed for this chunk.
			// SetData calls glBufferSubData internally — no reallocation occurs.
			s_Data.InstancedCircleInstanceVBO->SetData(
				instances + offset,
				batchSize * static_cast<uint32_t>(sizeof(InstanceCircleData)));

			// Route through RenderCommand — zero raw GL calls in Renderer2D.
			// The index buffer holds 6 indices (two triangles) for the unit quad.
			// DrawIndexedInstanced maps to glDrawElementsInstanced internally.
			RenderCommand::DrawIndexedInstanced(s_Data.InstancedCircleVAO, 6, batchSize);

			if (s_Data.StatsEnabled) s_Data.Stats.DrawCalls++;
			if (s_Data.StatsEnabled) s_Data.Stats.CircleCount += batchSize;

			remaining -= batchSize;
			offset += batchSize;
		}

		s_Data.InstancedCircleVAO->Unbind();

		// =====================================================================
		// 4. STATE CLEANUP
		// Signal to the batch circle system that its pipeline binding was altered
		// so it explicitly re-binds on its next draw command. Reset the current
		// material to DefaultMaterial so the next DrawQuad does not trigger a
		// spurious FlushAndReset due to nullptr != DefaultMaterial.
		// =====================================================================
		s_Data.ActiveCircleShader = nullptr;
		s_Data.CurrentMaterial = s_Data.DefaultMaterial;
	}


	//////////////////////////////////////////////////////////////////////////////////////

	void Renderer2D::DrawInstancedQuads(const InstanceQuadData* instances,
		uint32_t count,
		Ref<Shader> customShader)
	{
		if (!instances || count == 0) return;

		// =====================================================================
		// 1. PIPELINE ISOLATION
		// Flush all active traditional batches (quads, lines, circles) before
		// altering global VAO / shader bindings. This mirrors the circle path
		// and prevents state leakage in either direction.
		// =====================================================================
		FlushAndReset();

		// =====================================================================
		// 2. SHADER SELECTION
		// Fall back to the default instanced quad shader if none is supplied.
		// If even the default is missing (failed to load), bail early with a log.
		// =====================================================================
		Ref<Shader> targetShader = customShader
			? customShader
			: s_Data.DefaultInstancedQuadShader;

		if (!targetShader)
		{
			CS_CORE_ERROR("Renderer2D::DrawInstancedQuads: No valid shader available. "
				"Ensure QuadInstance.glsl loaded correctly during Init.");
			return;
		}

		targetShader->Bind();
		targetShader->SetMat4("u_ViewProjection", s_Data.ViewProjectionMatrix);

		// Upload the sampler array so the shader can access u_Textures[].
		// White texture occupies slot 0; callers bind additional textures before
		// calling this function when TexIndex > 0.
		int32_t samplers[Renderer2DData::MaxTextureSlots];
		for (int32_t i = 0; i < static_cast<int32_t>(Renderer2DData::MaxTextureSlots); ++i)
			samplers[i] = i;
		targetShader->SetIntArray("u_Textures", samplers, Renderer2DData::MaxTextureSlots);

		// Ensure the white texture is always present in slot 0 so solid-color
		// quads (TexIndex = 0) render correctly without caller setup.
		s_Data.WhiteTexture->Bind(0);

		// =====================================================================
		// 3. CHUNKED STREAMING LOOP
		// Stream instance data in chunks bounded by MaxInstancedQuads, issuing
		// one GPU draw call per chunk. This keeps GPU buffer usage bounded even
		// when thousands of instances are submitted per frame.
		// =====================================================================
		uint32_t remaining = count;
		uint32_t offset = 0;

		s_Data.InstancedQuadVAO->Bind();

		while (remaining > 0)
		{
			const uint32_t batchSize = (remaining < Renderer2DData::MaxInstancedQuads)
				? remaining
				: Renderer2DData::MaxInstancedQuads;

			// Upload only the slice of instance data for this chunk.
			// SetData calls glBufferSubData internally — zero reallocation.
			s_Data.InstancedQuadInstanceVBO->SetData(
				instances + offset,
				batchSize * static_cast<uint32_t>(sizeof(InstanceQuadData)));

			// Issue the draw through the engine abstraction.
			// The index buffer holds 6 indices (two triangles) for the unit quad.
			// DrawIndexedInstanced calls glDrawElementsInstanced internally.
			RenderCommand::DrawIndexedInstanced(s_Data.InstancedQuadVAO, 6, batchSize);

			if (s_Data.StatsEnabled) s_Data.Stats.DrawCalls++;
			if (s_Data.StatsEnabled) s_Data.Stats.QuadCount += batchSize;

			remaining -= batchSize;
			offset += batchSize;
		}

		s_Data.InstancedQuadVAO->Unbind();

		// =====================================================================
		// 4. STATE CLEANUP
		// Reset to DefaultMaterial so the first subsequent DrawQuad does not
		// trigger a spurious FlushAndReset due to nullptr != DefaultMaterial.
		// =====================================================================
		s_Data.CurrentMaterial = s_Data.DefaultMaterial;
	}


} // namespace Cosmic