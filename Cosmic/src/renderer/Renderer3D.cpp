// Renderer3D.cpp
// Last Modified: 7/1/2026

#include "renderer/Renderer3D.h"
#include "renderer/RenderCommand.h"
#include "renderer/BindingPoints.h"
#include "renderer/CameraUniforms.h"
#include "camera/Camera.h"
#include "camera/PerspectiveCamera.h"
#include "graphics/VertexArray.h"
#include "graphics/Buffer.h"
#include "graphics/Shader.h"
#include "graphics/Material.h"
#include "graphics/Model.h"
#include "graphics/UniformBuffer.h"
#include "renderer/InstanceSet.h"
#include "core/Log.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>

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

	// std140 mirror of MeshLit.glsl's LightsBlock (binding: Bindings::LightsUbo).
	// vec4-only — a bare vec3 here would silently misalign every following member
	// under std140.
	struct GpuLightsBlock
	{
		glm::vec4 SunDirection_Ambient{ -0.4f, -1.0f, -0.3f, 0.25f };
		glm::vec4 SunColor_Intensity{ 1.0f, 1.0f, 1.0f, 1.0f };
		glm::vec4 PointCount{ 0.0f };
		glm::vec4 PointPos_Radius[Renderer3D::kMaxPointLights]{};
		glm::vec4 PointColor_Intensity[Renderer3D::kMaxPointLights]{};
	};
	static_assert(sizeof(GpuLightsBlock) == 560, "GpuLightsBlock must match the std140 LightsBlock (560 bytes).");

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
		// --- Lighting v1 (S4.5): binding-0 lights UBO ---
		// =====================================================================
		Ref<UniformBuffer> LightsUBO;

		// =====================================================================
		// --- Per-frame camera UBO (S6.2): binding-1 CameraBlock ---
		// Replaces the per-draw loose u_ViewProjection / u_CameraPos uniforms;
		// uploaded once per BeginScene.
		// =====================================================================
		Ref<UniformBuffer> CameraUBO;

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

		// =====================================================================
		// --- Image-based lighting (S6.3): handles registered by SetIBL ---
		// Bound to reserved units on every material DrawMesh so PBR materials
		// sample them; the flat-color Lambert path ignores them.
		// =====================================================================
		bool     IblActive        = false;
		uint32_t IblIrradianceID   = 0;
		uint32_t IblPrefilterID    = 0;
		uint32_t IblBrdfLutID      = 0;
		float    IblPrefilterMaxLod = 0.0f;

		// =====================================================================
		// --- Directional shadows (S6.4): handles registered by SetShadow ---
		// =====================================================================
		bool      ShadowActive = false;
		uint32_t  ShadowMapID   = 0;
		glm::mat4 ShadowLightViewProj{ 1.0f };
		float     ShadowBias   = 0.0015f;
	};

	// Reserved fragment texture units — allocated in renderer/BindingPoints.h
	// (single source of truth; also the seed for a future backend's descriptor
	// layout). Local aliases keep the draw-path code short.
	namespace
	{
		constexpr uint32_t kIblIrradianceUnit = Bindings::TexUnitIblIrradiance;
		constexpr uint32_t kIblPrefilterUnit  = Bindings::TexUnitIblPrefilter;
		constexpr uint32_t kIblBrdfLutUnit    = Bindings::TexUnitIblBrdfLut;
		constexpr uint32_t kShadowMapUnit     = Bindings::TexUnitShadowMap;
	}

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

		// --- Lighting v1 (S4.5): allocate the lights UBO on its registry slot and
		//     seed it with defaults so lit shaders read a sane block even before
		//     SetLights. ---
		s_Data.LightsUBO = UniformBuffer::Create(sizeof(GpuLightsBlock), Bindings::LightsUbo);
		if (s_Data.LightsUBO)
		{
			GpuLightsBlock defaults{};
			s_Data.LightsUBO->SetData(&defaults, sizeof(defaults));
		}

		// --- Per-frame camera UBO (S6.2): allocate on binding 1. Every 3D shader
		//     reads the CameraBlock here instead of loose u_ViewProjection /
		//     u_CameraPos uniforms; BeginScene fills it each pass. ---
		s_Data.CameraUBO = UniformBuffer::Create(sizeof(GpuCameraBlock), Bindings::CameraUbo);
		if (s_Data.CameraUBO)
		{
			GpuCameraBlock defaults{};
			s_Data.CameraUBO->SetData(&defaults, sizeof(defaults));
		}
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
		s_Data.LightsUBO.reset();
		s_Data.CameraUBO.reset();
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

		// Upload the per-frame camera UBO (binding 1) once — every 3D shader reads
		// CameraBlock from here now (S6.2), so no shader needs a per-draw
		// u_ViewProjection / u_CameraPos setter. Re-Bind() re-asserts the base
		// binding in case app code bound another UBO to the slot.
		if (s_Data.CameraUBO)
		{
			GpuCameraBlock cam;
			cam.ViewProjection = viewProjection;
			cam.CameraPosition = glm::vec4(cameraPos, 1.0f);
			s_Data.CameraUBO->Bind();
			s_Data.CameraUBO->SetData(&cam, sizeof(cam));
		}
	}

	void Renderer3D::BeginScene(const PerspectiveCamera& camera)
	{
		BeginScene(camera.GetViewProjectionMatrix(), camera.GetPosition());
	}

	void Renderer3D::BeginScene(const Camera& camera)
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
		// u_ViewProjection now comes from the binding-1 CameraBlock UBO (S6.2).

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

	void Renderer3D::DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const glm::vec4& color,
	                          int entityID)
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
		// u_ViewProjection + u_CameraPos come from the binding-1 CameraBlock UBO (S6.2).
		s_Data.MeshShader->SetMat4("u_Model", transform);
		s_Data.MeshShader->SetFloat4("u_Color", color);
		s_Data.MeshShader->SetFloat3("u_LightDir", s_Data.LightDirection);
		s_Data.MeshShader->SetFloat("u_Ambient", s_Data.Ambient);
		s_Data.MeshShader->SetInt("u_EntityID", entityID);   // S4.6 (silent no-op if undeclared)

		// Directional shadows (S6.4): the flat Lambert path receives them too so a
		// shadow lands on plain-colored geometry (ground pad, ECS meshes). The
		// sampler uniform is assigned its reserved unit UNCONDITIONALLY — a sampler
		// left at its default unit 0 can alias a different sampler type there,
		// which the GL spec makes a draw-time INVALID_OPERATION on strict drivers
		// even when the branch never samples it (portability contract).
		s_Data.MeshShader->SetInt("u_ShadowMap", (int)kShadowMapUnit);
		if (s_Data.ShadowActive)
		{
			RenderCommand::BindTextureSlot(kShadowMapUnit, s_Data.ShadowMapID);
			s_Data.MeshShader->SetMat4("u_LightViewProj", s_Data.ShadowLightViewProj);
			s_Data.MeshShader->SetFloat("u_ShadowBias", s_Data.ShadowBias);
			s_Data.MeshShader->SetFloat("u_HasShadow", 1.0f);
		}
		else
		{
			s_Data.MeshShader->SetFloat("u_HasShadow", 0.0f);
		}

		mesh->GetVertexArray()->Bind();
		RenderCommand::DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());
	}

	void Renderer3D::DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform,
	                          const Ref<Material>& material, int entityID)
	{
		if (!mesh || !material)
			return;
		if (!s_Data.InScene)
		{
			CS_CORE_WARN("Renderer3D::DrawMesh (material) called outside BeginScene/EndScene — ignored.");
			return;
		}

		const Ref<Shader>& shader = material->GetShader();
		if (!shader)
			return;

		// 1) Bind the material first: activates its shader and uploads its cached
		//    floats/vecs plus binds textures to sequential slots (Material::BindFull).
		material->BindFull();

		// 2) Layer the engine-owned convention uniforms on TOP so they always win
		//    over any stale material value. Set unconditionally — a shader that
		//    doesn't declare one simply no-ops on location -1 (silent-ignore rule).
		//    u_Color is deliberately NOT set here (material-owned).
		const glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(transform));

		// u_ViewProjection + u_CameraPos come from the binding-1 CameraBlock UBO (S6.2);
		// the per-draw transform/normal/light uniforms stay loose.
		shader->SetMat4("u_Model", transform);
		shader->SetMat3("u_NormalMatrix", normalMatrix);
		shader->SetFloat3("u_LightDir", s_Data.LightDirection);
		shader->SetFloat("u_Ambient", s_Data.Ambient);
		shader->SetInt("u_EntityID", entityID);   // S4.6 (silent no-op if undeclared)

		// 3) Scene-level lighting resources (S6.3 IBL + S6.4 shadow) — shared with
		//    the engine systems that draw their own shaders (terrain/water).
		ApplySceneBindings(shader);

		mesh->GetVertexArray()->Bind();
		RenderCommand::DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());
	}

	void Renderer3D::ApplySceneBindings(const Ref<Shader>& shader)
	{
		if (!shader)
			return;

		// Image-based lighting (S6.3): point the IBL samplers at their reserved
		// units UNCONDITIONALLY, then bind + enable only when active. Without the
		// unconditional assignment the samplerCube uniforms sit at their default
		// unit 0 alongside the shader's sampler2Ds — two sampler TYPES on one
		// unit is a draw-time INVALID_OPERATION per the GL spec (lenient on
		// NVIDIA, fatal on Mesa/ANGLE-class drivers). Shaders that don't declare
		// these uniforms no-op on location -1 (silent-ignore rule). Feature
		// textures bind to low units, so the high reserved units never collide.
		shader->SetInt("u_IrradianceMap", (int)kIblIrradianceUnit);
		shader->SetInt("u_PrefilterMap",  (int)kIblPrefilterUnit);
		shader->SetInt("u_BrdfLut",       (int)kIblBrdfLutUnit);
		if (s_Data.IblActive)
		{
			RenderCommand::BindTextureCubeSlot(kIblIrradianceUnit, s_Data.IblIrradianceID);
			RenderCommand::BindTextureCubeSlot(kIblPrefilterUnit,  s_Data.IblPrefilterID);
			RenderCommand::BindTextureSlot(kIblBrdfLutUnit,        s_Data.IblBrdfLutID);
			shader->SetFloat("u_PrefilterMaxLod", s_Data.IblPrefilterMaxLod);
			shader->SetFloat("u_HasIBL", 1.0f);
		}
		else
		{
			shader->SetFloat("u_HasIBL", 0.0f);
		}

		// Directional shadows (S6.4): same unconditional-unit rule as the IBL set.
		shader->SetInt("u_ShadowMap", (int)kShadowMapUnit);
		if (s_Data.ShadowActive)
		{
			RenderCommand::BindTextureSlot(kShadowMapUnit, s_Data.ShadowMapID);
			shader->SetMat4("u_LightViewProj", s_Data.ShadowLightViewProj);
			shader->SetFloat("u_ShadowBias", s_Data.ShadowBias);
			shader->SetFloat("u_HasShadow", 1.0f);
		}
		else
		{
			shader->SetFloat("u_HasShadow", 0.0f);
		}
	}

	void Renderer3D::DrawMeshInstanced(const Ref<Mesh>& mesh, const Ref<Material>& material,
	                                   const Ref<InstanceSet>& instances, uint32_t count, int entityID)
	{
		if (!mesh || !material || !instances || count == 0)
			return;
		if (!s_Data.InScene)
		{
			CS_CORE_WARN("Renderer3D::DrawMeshInstanced called outside BeginScene/EndScene — ignored.");
			return;
		}

		const Ref<Shader>& shader = material->GetShader();
		if (!shader)
			return;

		const uint32_t drawCount = std::min(count, instances->GetCount());
		if (drawCount == 0)
			return;

		// 1) Material shader + its cached uniforms/textures (low units). The SSBO
		//    supplies the per-instance transform, so — unlike the single-mesh path
		//    — NO u_Model / u_NormalMatrix is set (PBRInstanced derives both from
		//    the instance's Model matrix).
		material->BindFull();
		shader->SetInt("u_EntityID", entityID);   // S4.6 (silent no-op if undeclared)

		// 2) Scene-level lighting resources (S6.3 IBL + S6.4 shadow), reserved units.
		ApplySceneBindings(shader);

		// 3) Per-instance pool (binding 9) + the instanced draw.
		instances->Bind();
		mesh->GetVertexArray()->Bind();
		RenderCommand::DrawIndexedInstanced(mesh->GetVertexArray(), mesh->GetIndexCount(), drawCount);
	}

	void Renderer3D::DrawModel(const Ref<Model>& model, const glm::mat4& transform, int entityID)
	{
		if (!model)
			return;

		// S6.2: parts imported with a glTF material draw through their PBR material
		// (factors + textures); parts without one fall back to the flat base color.
		for (const ModelPart& part : model->GetParts())
		{
			if (part.PbrMaterial)
				DrawMesh(part.Geometry, transform, part.PbrMaterial, entityID);
			else
				DrawMesh(part.Geometry, transform, part.BaseColor, entityID);
		}
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

	void Renderer3D::SetLights(const SceneLightsDesc& lights)
	{
		if (!s_Data.LightsUBO)
			return;

		GpuLightsBlock block;

		const glm::vec3 sunDir = glm::length(lights.SunDirection) > 1e-6f
			? glm::normalize(lights.SunDirection)
			: glm::vec3(0.0f, -1.0f, 0.0f);
		block.SunDirection_Ambient = glm::vec4(sunDir, glm::clamp(lights.Ambient, 0.0f, 1.0f));
		block.SunColor_Intensity   = glm::vec4(lights.SunColor, lights.SunIntensity);

		if (lights.Points.size() > kMaxPointLights)
		{
			// Once per run — this fires every frame in scenes over the cap.
			static bool s_WarnedTruncation = false;
			if (!s_WarnedTruncation)
			{
				CS_CORE_WARN("Renderer3D::SetLights: {} point lights supplied; only the first {} are uploaded (kMaxPointLights).",
				             lights.Points.size(), kMaxPointLights);
				s_WarnedTruncation = true;
			}
		}

		const uint32_t count = static_cast<uint32_t>(
			std::min<size_t>(lights.Points.size(), kMaxPointLights));
		block.PointCount = glm::vec4(static_cast<float>(count), 0.0f, 0.0f, 0.0f);
		for (uint32_t i = 0; i < count; ++i)
		{
			const PointLightDesc& p = lights.Points[i];
			block.PointPos_Radius[i]     = glm::vec4(p.Position, p.Radius);
			block.PointColor_Intensity[i] = glm::vec4(p.Color, p.Intensity);
		}

		// Re-assert the binding before uploading — cheap insurance in case app
		// code bound another UBO to the lights slot since Init.
		s_Data.LightsUBO->Bind();
		s_Data.LightsUBO->SetData(&block, sizeof(block));
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Image-based lighting (S6.3)
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer3D::SetIBL(uint32_t irradianceCubeID, uint32_t prefilterCubeID,
	                        uint32_t brdfLutID, float prefilterMaxLod)
	{
		s_Data.IblActive          = irradianceCubeID != 0 && prefilterCubeID != 0 && brdfLutID != 0;
		s_Data.IblIrradianceID    = irradianceCubeID;
		s_Data.IblPrefilterID     = prefilterCubeID;
		s_Data.IblBrdfLutID       = brdfLutID;
		s_Data.IblPrefilterMaxLod = prefilterMaxLod;
	}

	void Renderer3D::ClearIBL()
	{
		s_Data.IblActive = false;
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Directional shadows (S6.4)
	/////////////////////////////////////////////////////////////////////////////////

	void Renderer3D::SetShadow(uint32_t shadowMapID, const glm::mat4& lightViewProj, float bias)
	{
		s_Data.ShadowActive        = shadowMapID != 0;
		s_Data.ShadowMapID         = shadowMapID;
		s_Data.ShadowLightViewProj = lightViewProj;
		s_Data.ShadowBias          = bias;
	}

	void Renderer3D::ClearShadow()
	{
		s_Data.ShadowActive = false;
	}

	/////////////////////////////////////////////////////////////////////////////////
}
