// Renderer3D.cpp
// Last Modified: 7/1/2026

#include "renderer/Renderer3D.h"
#include "renderer/RenderCommand.h"
#include "camera/PerspectiveCamera.h"
#include "graphics/VertexArray.h"
#include "graphics/Buffer.h"
#include "graphics/Shader.h"
#include "core/Log.h"

#include <glm/glm.hpp>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////
	// Internal Vertex Structures & State Storage
	/////////////////////////////////////////////////////////////////////////////////

	struct LineVertex3D
	{
		glm::vec3 Position;
		glm::vec4 Color;
	};

	struct Renderer3DData
	{
		// =====================================================================
		// Pipeline Allocation Limits (line batch cloned from Renderer2D's)
		// =====================================================================
		static const uint32_t MaxLines        = 20000;
		static const uint32_t MaxLineVertices = MaxLines * 2;

		// =====================================================================
		// --- Line Batch Data ---
		// =====================================================================
		Ref<VertexArray>  LineVertexArray;
		Ref<VertexBuffer> LineVertexBuffer;
		Ref<Shader>       LineShader;

		uint32_t      LineVertexCount      = 0;
		LineVertex3D* LineVertexBufferBase = nullptr;
		LineVertex3D* LineVertexBufferPtr  = nullptr;

		// =====================================================================
		// --- Mesh Pipeline ---
		// =====================================================================
		Ref<Shader> MeshShader;

		// =====================================================================
		// --- Per-Scene Camera State (set by BeginScene) ---
		// =====================================================================
		glm::mat4 ViewProjection{ 1.0f };
		glm::vec3 CameraPos{ 0.0f };
		bool      InScene = false;

		// =====================================================================
		// --- Scene Lighting (S2: one directional light + ambient floor) ---
		// =====================================================================
		glm::vec3 LightDirection = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.25f));
		float     Ambient        = 0.25f;
	};

	static Renderer3DData s_Data;

	/////////////////////////////////////////////////////////////////////////////////
	// System Lifecycle
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer3D::Init()
	{
		// --- Line Batch Initialization (same staging pattern as Renderer2D) ---
		s_Data.LineVertexArray  = VertexArray::Create();
		s_Data.LineVertexBuffer = VertexBuffer::Create(Renderer3DData::MaxLineVertices * sizeof(LineVertex3D));
		s_Data.LineVertexBuffer->SetLayout({
			{ ShaderDataType::Float3, "a_Position" },
			{ ShaderDataType::Float4, "a_Color"    }
			});
		s_Data.LineVertexArray->AddVertexBuffer(s_Data.LineVertexBuffer);

		s_Data.LineVertexBufferBase = new LineVertex3D[Renderer3DData::MaxLineVertices];
		s_Data.LineVertexBufferPtr  = s_Data.LineVertexBufferBase;

		s_Data.LineShader = Shader::Create("assets/shaders/Line3D.glsl");
		if (!s_Data.LineShader)
			CS_CORE_ERROR("Renderer3D: Failed to load Line3D shader!");

		// --- Mesh Pipeline Initialization ---
		s_Data.MeshShader = Shader::Create("assets/shaders/Mesh3D.glsl");
		if (!s_Data.MeshShader)
			CS_CORE_ERROR("Renderer3D: Failed to load Mesh3D shader!");
	}

	void Renderer3D::Shutdown()
	{
		delete[] s_Data.LineVertexBufferBase;
		s_Data.LineVertexBufferBase = nullptr;
		s_Data.LineVertexBufferPtr  = nullptr;

		// Release GPU handles while the GL context is still current (same
		// teardown ordering contract as Renderer2D::Shutdown — see README §24).
		s_Data.LineVertexArray.reset();
		s_Data.LineVertexBuffer.reset();
		s_Data.LineShader.reset();
		s_Data.MeshShader.reset();
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Scene Boundaries
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer3D::BeginScene(const glm::mat4& viewProjection, const glm::vec3& cameraPos)
	{
		if (s_Data.InScene)
			CS_CORE_WARN("Renderer3D::BeginScene called while a scene is already open — missing EndScene?");

		s_Data.ViewProjection = viewProjection;
		s_Data.CameraPos      = cameraPos;
		s_Data.InScene        = true;

		s_Data.LineVertexCount     = 0;
		s_Data.LineVertexBufferPtr = s_Data.LineVertexBufferBase;
	}

	void Renderer3D::BeginScene(const PerspectiveCamera& camera)
	{
		BeginScene(camera.GetViewProjectionMatrix(), camera.GetPosition());
	}

	// Uploads and draws the staged line batch. Internal — called by EndScene and
	// by the overflow guard in DrawLine.
	static void FlushLines()
	{
		if (s_Data.LineVertexCount == 0)
			return;

		const uint32_t dataSize = static_cast<uint32_t>(
			reinterpret_cast<uint8_t*>(s_Data.LineVertexBufferPtr) -
			reinterpret_cast<uint8_t*>(s_Data.LineVertexBufferBase));
		s_Data.LineVertexBuffer->SetData(s_Data.LineVertexBufferBase, dataSize);

		s_Data.LineShader->Bind();
		s_Data.LineShader->SetMat4("u_ViewProjection", s_Data.ViewProjection);

		s_Data.LineVertexArray->Bind();
		RenderCommand::DrawLines(s_Data.LineVertexArray, s_Data.LineVertexCount);

		s_Data.LineVertexCount     = 0;
		s_Data.LineVertexBufferPtr = s_Data.LineVertexBufferBase;
	}

	void Renderer3D::EndScene()
	{
		if (!s_Data.InScene)
		{
			CS_CORE_WARN("Renderer3D::EndScene called without a matching BeginScene.");
			return;
		}

		FlushLines();
		s_Data.InScene = false;

		// State contract (rule 5): the only GL state this pass may have touched
		// beyond shader/VAO bindings is none — depth test/write and blending were
		// left at engine defaults, so Renderer2D's assumptions hold untouched.
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Batched Line Primitives
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer3D::DrawLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color)
	{
		if (!s_Data.InScene)
		{
			CS_CORE_WARN("Renderer3D::DrawLine called outside BeginScene/EndScene — ignored.");
			return;
		}

		// Flush-on-full keeps the API unbounded, exactly like Renderer2D's batches.
		if (s_Data.LineVertexCount >= Renderer3DData::MaxLineVertices - 1)
			FlushLines();

		s_Data.LineVertexBufferPtr->Position = a;
		s_Data.LineVertexBufferPtr->Color    = color;
		s_Data.LineVertexBufferPtr++;

		s_Data.LineVertexBufferPtr->Position = b;
		s_Data.LineVertexBufferPtr->Color    = color;
		s_Data.LineVertexBufferPtr++;

		s_Data.LineVertexCount += 2;
	}

	void Renderer3D::DrawPolyline(const glm::vec3* points, size_t count, const glm::vec4& color)
	{
		if (!points || count < 2)
			return;

		for (size_t i = 0; i + 1 < count; ++i)
			DrawLine(points[i], points[i + 1], color);
	}

	void Renderer3D::DrawPolyline(const std::vector<glm::vec3>& points, const glm::vec4& color)
	{
		DrawPolyline(points.data(), points.size(), color);
	}

	void Renderer3D::DrawGrid(float extent, float step, const glm::vec4& color)
	{
		DrawGrid(extent, step, color, color, 0);
	}

	void Renderer3D::DrawGrid(float extent, float step,
	                          const glm::vec4& minorColor, const glm::vec4& majorColor,
	                          int majorEvery)
	{
		if (extent <= 0.0f || step <= 0.0f)
			return;

		const int lineCount = static_cast<int>(extent / step);

		for (int i = -lineCount; i <= lineCount; ++i)
		{
			const float d = static_cast<float>(i) * step;
			const bool major = (i == 0) || (majorEvery > 0 && i % majorEvery == 0);
			const glm::vec4& c = major ? majorColor : minorColor;

			// Lines parallel to X (varying Z) and parallel to Z (varying X).
			DrawLine({ -extent, 0.0f, d }, { extent, 0.0f, d }, c);
			DrawLine({ d, 0.0f, -extent }, { d, 0.0f, extent }, c);
		}
	}

	void Renderer3D::DrawAxes(const glm::mat4& transform, float size)
	{
		const glm::vec3 origin = glm::vec3(transform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
		const glm::vec3 x      = glm::vec3(transform * glm::vec4(size, 0.0f, 0.0f, 1.0f));
		const glm::vec3 y      = glm::vec3(transform * glm::vec4(0.0f, size, 0.0f, 1.0f));
		const glm::vec3 z      = glm::vec3(transform * glm::vec4(0.0f, 0.0f, size, 1.0f));

		DrawLine(origin, x, { 0.9f, 0.2f, 0.2f, 1.0f });   // +X red
		DrawLine(origin, y, { 0.2f, 0.9f, 0.2f, 1.0f });   // +Y green
		DrawLine(origin, z, { 0.2f, 0.4f, 0.9f, 1.0f });   // +Z blue
	}

	void Renderer3D::DrawWireBox(const glm::mat4& transform, const glm::vec4& color)
	{
		// Unit cube corners (±0.5), transformed once.
		glm::vec3 c[8];
		int idx = 0;
		for (int xi = 0; xi < 2; ++xi)
			for (int yi = 0; yi < 2; ++yi)
				for (int zi = 0; zi < 2; ++zi)
				{
					const glm::vec4 corner(xi ? 0.5f : -0.5f, yi ? 0.5f : -0.5f, zi ? 0.5f : -0.5f, 1.0f);
					c[idx++] = glm::vec3(transform * corner);
				}

		// Corner index bit layout: (x << 2) | (y << 1) | z. The 12 edges connect
		// corners differing in exactly one bit.
		static const int edges[12][2] = {
			{ 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },   // Z-direction edges
			{ 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },   // Y-direction edges
			{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }    // X-direction edges
		};
		for (const auto& e : edges)
			DrawLine(c[e[0]], c[e[1]], color);
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Mesh Submission
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer3D::DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const glm::vec4& color)
	{
		if (!mesh)
			return;
		if (!s_Data.InScene)
		{
			CS_CORE_WARN("Renderer3D::DrawMesh called outside BeginScene/EndScene — ignored.");
			return;
		}
		if (!s_Data.MeshShader)
			return;

		// Meshes are opaque and depth-tested; the staged line batch draws at
		// EndScene and depth-sorts against them correctly. Uniform names below
		// are the engine-wide mesh shader convention (doc 05 S4.2).
		s_Data.MeshShader->Bind();
		s_Data.MeshShader->SetMat4("u_ViewProjection", s_Data.ViewProjection);
		s_Data.MeshShader->SetMat4("u_Model", transform);
		s_Data.MeshShader->SetFloat4("u_Color", color);
		s_Data.MeshShader->SetFloat3("u_CameraPos", s_Data.CameraPos);
		s_Data.MeshShader->SetFloat3("u_LightDir", s_Data.LightDirection);
		s_Data.MeshShader->SetFloat("u_Ambient", s_Data.Ambient);

		mesh->GetVertexArray()->Bind();
		RenderCommand::DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Scene Lighting
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer3D::SetLightDirection(const glm::vec3& direction)
	{
		const float len = glm::length(direction);
		if (len < 1e-6f)
		{
			CS_CORE_WARN("Renderer3D::SetLightDirection: zero-length direction ignored.");
			return;
		}
		s_Data.LightDirection = direction / len;
	}

	const glm::vec3& Renderer3D::GetLightDirection()
	{
		return s_Data.LightDirection;
	}

	void Renderer3D::SetAmbient(float ambient)
	{
		s_Data.Ambient = glm::clamp(ambient, 0.0f, 1.0f);
	}

	float Renderer3D::GetAmbient()
	{
		return s_Data.Ambient;
	}

	/////////////////////////////////////////////////////////////////////////////////
}
