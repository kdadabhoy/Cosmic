// NavigationCube.cpp
// See NavigationCube.h — SolidWorks-style orientation cube (S5.3).

#include "camera/NavigationCube.h"
#include "renderer/Renderer3D.h"
#include "renderer/RenderCommand.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Cosmic
{
	Ref<NavigationCube> NavigationCube::Create(uint32_t pixelSize)
	{
		return std::make_shared<NavigationCube>(pixelSize);
	}

	NavigationCube::NavigationCube(uint32_t pixelSize)
		: m_Size(pixelSize == 0 ? 140 : pixelSize)
	{
		FramebufferSpecification spec;
		spec.Width  = m_Size;
		spec.Height = m_Size;
		spec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::DEPTH24STENCIL8 };
		m_Fbo = FrameBuffer::Create(spec);

		m_Box = Mesh::CreateBox({ 1.0f, 1.0f, 1.0f });   // ±0.5 unit cube

		// Orthographic so the cube reads with no perspective skew. The half-extent r
		// must exceed the cube's half-diagonal (0.5·√3 ≈ 0.866) so no rotation clips
		// it; near/far bracket the cube once it's pushed back by m_ViewDistance.
		const float r = 0.95f;
		m_Projection = glm::ortho(-r, r, -r, r, 1.0f, 3.0f);
	}

	void NavigationCube::Render(const glm::mat4& cameraView)
	{
		if (!m_Fbo || !m_Box)
			return;

		// Rotation-only view (strip the camera's translation), then push the cube
		// back so it sits inside the ortho near/far band. Rotating the VIEW (not the
		// cube) keeps the cube axis-aligned in world space, which makes PickFace a
		// plain ray-vs-AABB test.
		const glm::mat4 rot = glm::mat4(glm::mat3(cameraView));
		m_LastView = glm::translate(glm::mat4(1.0f), { 0.0f, 0.0f, -m_ViewDistance }) * rot;
		const glm::mat4 vp = m_Projection * m_LastView;

		// The cube camera's eye (for the Lambert view/spec terms) — inverse-view origin.
		const glm::vec3 eye = glm::vec3(glm::inverse(m_LastView)[3]);

		m_Fbo->Bind();
		RenderCommand::SetViewport(0, 0, m_Size, m_Size);
		RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 0.0f });   // transparent — cube floats
		RenderCommand::Clear();

		Renderer3D::BeginScene(vp, eye);

		// Shaded faces (Lambert picks out orientation via per-face shading), bright
		// edges just outside the surface to avoid z-fighting, and an RGB tripod so
		// +X/+Y/+Z read at a glance.
		Renderer3D::DrawMesh(m_Box, glm::mat4(1.0f), { 0.60f, 0.64f, 0.72f, 1.0f });
		Renderer3D::DrawWireBox(glm::scale(glm::mat4(1.0f), glm::vec3(1.01f)), { 0.90f, 0.92f, 0.96f, 1.0f });
		Renderer3D::DrawAxes(glm::mat4(1.0f), 0.85f);

		Renderer3D::EndScene();

		m_Fbo->Unbind();
	}

	uint32_t NavigationCube::GetTextureID() const
	{
		return m_Fbo ? m_Fbo->GetColorAttachmentRendererID(0) : 0;
	}

	bool NavigationCube::PickFace(float u, float v, ViewPreset& outPreset) const
	{
		// Uses the SAME view-projection the last Render used, so pixels and picks agree.
		return PickFaceFromViewProjection(m_Projection * m_LastView, u, v, outPreset);
	}

	bool NavigationCube::PickFaceFromViewProjection(const glm::mat4& viewProjection,
	                                                float u, float v, ViewPreset& outPreset)
	{
		// Panel (u,v)  (v down) → NDC. Unproject a ray, then intersect the axis-aligned
		// unit cube.
		const glm::vec2 ndc(u * 2.0f - 1.0f, 1.0f - v * 2.0f);

		const glm::mat4 invVP = glm::inverse(viewProjection);
		glm::vec4 pN = invVP * glm::vec4(ndc, -1.0f, 1.0f);
		glm::vec4 pF = invVP * glm::vec4(ndc,  1.0f, 1.0f);
		if (std::abs(pN.w) < 1e-8f || std::abs(pF.w) < 1e-8f)
			return false;

		const glm::vec3 ro = glm::vec3(pN) / pN.w;
		const glm::vec3 farPt = glm::vec3(pF) / pF.w;   // ('far' is a legacy Windows macro)
		if (glm::length(farPt - ro) < 1e-8f)
			return false;
		const glm::vec3 rd = glm::normalize(farPt - ro);

		// Ray vs AABB [-0.5, 0.5]³ (slab method), tracking the entry face.
		const glm::vec3 bmin(-0.5f), bmax(0.5f);
		float tmin = -1e30f, tmax = 1e30f;
		int   enterAxis = 2;
		float enterSign = 1.0f;

		for (int i = 0; i < 3; ++i)
		{
			if (std::abs(rd[i]) < 1e-8f)
			{
				if (ro[i] < bmin[i] || ro[i] > bmax[i])
					return false;                 // parallel and outside the slab
				continue;
			}

			const float inv = 1.0f / rd[i];
			float t1 = (bmin[i] - ro[i]) * inv;   // min plane → outward normal -axis
			float t2 = (bmax[i] - ro[i]) * inv;   // max plane → outward normal +axis
			float sign = -1.0f;
			if (t1 > t2) { std::swap(t1, t2); sign = 1.0f; }

			if (t1 > tmin) { tmin = t1; enterAxis = i; enterSign = sign; }
			tmax = std::min(tmax, t2);
			if (tmin > tmax)
				return false;
		}
		if (tmax < 0.0f)
			return false;                         // box entirely behind the ray

		// Entry face normal = enterSign along enterAxis → view preset.
		if (enterAxis == 0)      outPreset = enterSign > 0.0f ? ViewPreset::Right : ViewPreset::Left;
		else if (enterAxis == 1) outPreset = enterSign > 0.0f ? ViewPreset::Top   : ViewPreset::Bottom;
		else                     outPreset = enterSign > 0.0f ? ViewPreset::Front : ViewPreset::Back;
		return true;
	}
}
