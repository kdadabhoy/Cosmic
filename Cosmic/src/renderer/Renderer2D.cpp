#include "renderer/Renderer2D.h"
#include "graphics/VertexArray.h"
#include "graphics/Shader.h"
#include "renderer/RenderCommand.h"
#include "core/Log.h"

#include <glm/gtc/matrix_transform.hpp>
#include <array>


namespace Cosmic
{
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
		float TexIndex;
		float TilingFactor;
	};

	struct Renderer2DData
	{
		static const uint32_t MaxQuads = 10000;
		static const uint32_t MaxVertices = MaxQuads * 4;
		static const uint32_t MaxIndices = MaxQuads * 6;
		static const uint32_t MaxTextureSlots = 32;

		Ref<VertexArray> QuadVertexArray;
		Ref<VertexBuffer> QuadVertexBuffer;
		Ref<Shader> TextureShader;
		Ref<Texture> WhiteTexture;

		// --- Material System ---
		Ref<Material> CurrentMaterial = nullptr;
		Ref<Material> DefaultMaterial = nullptr;

		uint32_t QuadIndexCount = 0;
		QuadVertex* QuadVertexBufferBase = nullptr;
		QuadVertex* QuadVertexPtr = nullptr;

		std::array<Ref<Texture>, MaxTextureSlots> TextureSlots;
		uint32_t TextureSlotIndex = 1; // 0 = White Texture

		glm::vec4 QuadVertexPositions[4];
		Renderer2D::Statistics Stats;
		bool StatsEnabled = false;

		// --- Line Data ---
		Ref<VertexArray> LineVertexArray;
		Ref<VertexBuffer> LineVertexBuffer;
		Ref<Shader> LineShader;

		uint32_t LineVertexCount = 0;
		LineVertex* LineVertexBufferBase = nullptr;
		LineVertex* LineVertexBufferPtr = nullptr;

		glm::mat4 ViewProjectionMatrix;
	};

	static Renderer2DData s_Data;

	void Renderer2D::Init()
	{
		CS_CORE_TRACE("Initializing Renderer2D...");

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

		// REPLACED: std::cerr with CS_CORE_ERROR
		if (!s_Data.LineShader)
			CS_CORE_ERROR("Renderer2D: Failed to load Line shader!");
	}

	void Renderer2D::Shutdown()
	{
		CS_CORE_TRACE("Shutting down Renderer2D");
		delete[] s_Data.QuadVertexBufferBase;
		delete[] s_Data.LineVertexBufferBase;
	}

	void Renderer2D::BeginScene(const OrthographicCamera& camera)
	{
		s_Data.ViewProjectionMatrix = camera.GetViewProjectionMatrix();

		s_Data.QuadIndexCount = 0;
		s_Data.QuadVertexPtr = s_Data.QuadVertexBufferBase;
		s_Data.TextureSlotIndex = 1;
		s_Data.CurrentMaterial = s_Data.DefaultMaterial;

		s_Data.LineVertexCount = 0;
		s_Data.LineVertexBufferPtr = s_Data.LineVertexBufferBase;
	}

	void Renderer2D::EndScene()
	{
		Flush();
	}

	void Renderer2D::Flush()
	{
		if (s_Data.QuadIndexCount != 0)
		{
			uint32_t dataSize = (uint32_t)((uint8_t*)s_Data.QuadVertexPtr - (uint8_t*)s_Data.QuadVertexBufferBase);
			s_Data.QuadVertexBuffer->SetData(s_Data.QuadVertexBufferBase, dataSize);

			// 1. Bind all textures to their slots
			for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
				s_Data.TextureSlots[i]->Bind(i);

			// 2. Bind Material and Update its internal Shader
			if (s_Data.CurrentMaterial)
			{
				s_Data.CurrentMaterial->Bind();
				auto shader = s_Data.CurrentMaterial->GetShader();

				// Standard Uniforms
				shader->SetMat4("u_ViewProjection", s_Data.ViewProjectionMatrix);

				// CRITICAL: Custom materials don't know about batching by default.
				// We must upload the sampler array [0, 1, 2... 31] every time we switch materials.
				int32_t samplers[32];
				for (uint32_t i = 0; i < 32; i++) samplers[i] = i;
				shader->SetIntArray("u_Textures", samplers, 32);
			}

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
	}

	void Renderer2D::FlushAndReset()
	{
		EndScene();

		s_Data.QuadIndexCount = 0;
		s_Data.QuadVertexPtr = s_Data.QuadVertexBufferBase;
		s_Data.TextureSlotIndex = 1;

		s_Data.LineVertexCount = 0;
		s_Data.LineVertexBufferPtr = s_Data.LineVertexBufferBase;
	}

	// --- Drawing Implementation ---

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
			s_Data.QuadVertexPtr->TexIndex = 0.0f; // White Texture
			s_Data.QuadVertexPtr->TilingFactor = 1.0f;
			s_Data.QuadVertexPtr++;
		}
		s_Data.QuadIndexCount += 6;
		s_Data.Stats.QuadCount++;
	}

	void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Texture>& texture, float tilingFactor, const glm::vec4& tintColor)
	{
		if (!texture)
		{
			CS_CORE_WARN("Renderer2D: DrawQuad received null texture. Falling back to White.");
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
			s_Data.TextureSlots[s_Data.TextureSlotIndex] = texture;
			s_Data.TextureSlotIndex++;
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

	void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Material>& material)
	{
		if (!material) return;
		if (s_Data.CurrentMaterial != material) FlushAndReset();
		s_Data.CurrentMaterial = material;

		Ref<Texture> tex = material->GetTexture("u_Texture");
		if (!tex) tex = s_Data.WhiteTexture;

		// If the texture is STILL null after the fallback, that's an actual error.
		if (!tex)
		{
			CS_CORE_ERROR("Renderer2D: Material '{0}' - 'u_Texture' is NULL and fallback failed!", material->GetName());
			return;
		}

		glm::vec4 color = material->GetVector("u_Color");
		DrawQuad(position, size, tex, 1.0f, color);
	}




	void Renderer2D::DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<Material>& material)
	{
		if (!material) return;
		if (s_Data.CurrentMaterial != material) FlushAndReset();
		s_Data.CurrentMaterial = material;

		Ref<Texture> tex = material->GetTexture("u_Texture");
		if (!tex) tex = s_Data.WhiteTexture;

		glm::vec4 color = material->GetVector("u_Color");

		DrawRotatedQuad(position, size, rotation, tex, 1.0f, color);
	}

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

	void Renderer2D::DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color)
	{
		if (s_Data.LineVertexCount >= Renderer2DData::MaxVertices) FlushAndReset();

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

	// --- Helpers / Overloads ---
	void Renderer2D::DrawQuad(const glm::vec2& pos, const glm::vec2& size, const glm::vec4& col) { DrawQuad({ pos.x, pos.y, 0.0f }, size, col); }
	void Renderer2D::DrawQuad(const glm::vec2& pos, const glm::vec2& size, const Ref<Texture>& tex, float tiling, const glm::vec4& tint) { DrawQuad({ pos.x, pos.y, 0.0f }, size, tex, tiling, tint); }
	void Renderer2D::DrawRotatedQuad(const glm::vec2& pos, const glm::vec2& size, float rot, const glm::vec4& col) { DrawRotatedQuad({ pos.x, pos.y, 0.0f }, size, rot, col); }
	void Renderer2D::DrawRotatedQuad(const glm::vec2& pos, const glm::vec2& size, float rot, const Ref<Texture>& tex, float tiling, const glm::vec4& tint) { DrawRotatedQuad({ pos.x, pos.y, 0.0f }, size, rot, tex, tiling, tint); }

	// --- Statistics ---
	void Renderer2D::SetStatsStatus(bool enabled) { s_Data.StatsEnabled = enabled; }
	Renderer2D::Statistics Renderer2D::GetStats() { return s_Data.Stats; }
	void Renderer2D::ResetStats() { memset(&s_Data.Stats, 0, sizeof(Statistics)); }
}