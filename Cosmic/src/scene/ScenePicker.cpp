// ScenePicker.cpp
// See ScenePicker.h — entity-ID 3D picking (S5.4).

#include "scene/ScenePicker.h"
#include "scene/Scene.h"
#include "scene/Components.h"
#include "scene/Components3D.h"   // W4 — mesh + primitive pick geometry
#include "renderer/Renderer3D.h"
#include "renderer/RenderCommand.h"
#include "camera/Camera.h"
#include "voxel/VoxelVolume.h"
#include "voxel/VoxelRender.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace Cosmic
{
	Ref<ScenePicker> ScenePicker::Create()
	{
		return std::make_shared<ScenePicker>();
	}

	ScenePicker::ScenePicker()
	{
		FramebufferSpecification spec;
		spec.Width  = 1;   // grown to the viewport size on the first RenderIdPass
		spec.Height = 1;
		spec.Attachments = {
			FramebufferTextureFormat::RGBA8,
			FramebufferTextureFormat::RED_INTEGER,
			FramebufferTextureFormat::DEPTH24STENCIL8
		};
		m_Fbo = FrameBuffer::Create(spec);
	}

	void ScenePicker::RenderIdPass(Scene& scene, const Camera& camera, uint32_t width, uint32_t height,
	                               const std::vector<entt::entity>* only)
	{
		if (!m_Fbo || width == 0 || height == 0)
			return;

		if (m_Fbo->GetWidth() != width || m_Fbo->GetHeight() != height)
			m_Fbo->Resize(width, height);

		m_Fbo->Bind();
		RenderCommand::SetViewport(0, 0, width, height);
		RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
		RenderCommand::Clear();
		m_Fbo->ClearAttachment(1, -1);   // glClear does not touch integer attachments

		if (!only)
		{
			// OnRender3D writes each entity's id into attachment 1 (u_EntityID →
			// o_EntityID, S4.6). Color lands in attachment 0 (debug view only).
			scene.OnRender3D(camera);
		}
		else
		{
			// K12 — selection-filtered pass: draw ONLY the given entities (the
			// same shapes the full pass covers) so the ID attachment becomes a
			// selection mask for the outline pass. Flat color path — the color
			// attachment is irrelevant here; only the ids matter.
			auto& reg = scene.GetRegistry();
			Renderer3D::BeginScene(camera);
			for (entt::entity h : *only)
			{
				Entity e(h, &scene);
				if (!e || !e.HasComponent<TransformComponent>())
					continue;
				const int id = (int)(uint32_t)h;

				if (e.HasComponent<MeshRendererComponent>())
				{
					const auto& mr = e.GetComponent<MeshRendererComponent>();
					if (mr.MeshAsset)
						Renderer3D::DrawMesh(mr.MeshAsset, scene.GetWorldTransform(e),
						                     glm::vec4(1.0f), id);
				}
				if (e.HasComponent<LODGroupComponent>())
				{
					const auto& lod = reg.get<LODGroupComponent>(h);
					const auto& t   = reg.get<TransformComponent>(h);
					const int level = LODGroupComponent::SelectLevel(
						lod.Levels, glm::distance(camera.GetPosition(), t.Position));
					if (level >= 0 && lod.Levels[level].MeshAsset)
						Renderer3D::DrawMesh(lod.Levels[level].MeshAsset,
						                     scene.GetWorldTransform(e), glm::vec4(1.0f), id);
				}
				if (e.HasComponent<VoxelVolumeComponent>())
				{
					const auto& vc = reg.get<VoxelVolumeComponent>(h);
					if (vc.Volume && vc.Render)
					{
						const glm::mat4 xf =
							glm::translate(glm::mat4(1.0f), vc.Volume->GetOrigin()) *
							glm::scale(glm::mat4(1.0f), glm::vec3(vc.Volume->GetVoxelSize()));
						for (const auto& kv : vc.Render->ChunkMeshes)
							if (kv.second)
								Renderer3D::DrawMesh(kv.second, xf, glm::vec4(1.0f), id);
					}
				}
			}
			Renderer3D::EndScene();
		}

		m_Fbo->Unbind();
	}

	Entity ScenePicker::Pick(Scene& scene, int xFromLeft, int yFromTop) const
	{
		if (!m_Fbo)
			return {};

		const int w = (int)m_Fbo->GetWidth();
		const int h = (int)m_Fbo->GetHeight();
		if (xFromLeft < 0 || yFromTop < 0 || xFromLeft >= w || yFromTop >= h)
			return {};

		// GL's origin is bottom-left, so flip y for the read (same as the S4.6 demo).
		const int glY = h - 1 - yFromTop;

		m_Fbo->Bind();
		const int id = m_Fbo->ReadPixel(1, xFromLeft, glY);
		m_Fbo->Unbind();

		if (id < 0)
			return {};

		// Map the id back to a handle and validity-check against the live registry
		// (Entity::operator bool rejects stale/recycled slots).
		Entity picked{ (entt::entity)(uint32_t)id, &scene };
		return picked ? picked : Entity{};
	}

	bool ScenePicker::WorldPoint(const Camera& camera, int xFromLeft, int yFromTop, glm::vec3& out) const
	{
		if (!m_Fbo)
			return false;

		const int w = (int)m_Fbo->GetWidth();
		const int h = (int)m_Fbo->GetHeight();
		if (xFromLeft < 0 || yFromTop < 0 || xFromLeft >= w || yFromTop >= h)
			return false;

		const int glY = h - 1 - yFromTop;   // GL bottom-left flip

		m_Fbo->Bind();
		const float depth = m_Fbo->ReadDepth(xFromLeft, glY);
		m_Fbo->Unbind();

		if (depth >= 1.0f)
			return false;   // far plane — nothing under the cursor

		// Pixel (+ depth) → NDC → world via the inverse view-projection. GL's default
		// depth range is [0, 1] in the window and [-1, 1] in clip space.
		const float ndcX = ((float)xFromLeft + 0.5f) / (float)w * 2.0f - 1.0f;
		const float ndcY = 1.0f - ((float)yFromTop + 0.5f) / (float)h * 2.0f;
		const float ndcZ = depth * 2.0f - 1.0f;

		const glm::mat4 invVP = glm::inverse(camera.GetViewProjectionMatrix());
		glm::vec4 world = invVP * glm::vec4(ndcX, ndcY, ndcZ, 1.0f);
		if (std::abs(world.w) < 1e-8f)
			return false;

		out = glm::vec3(world) / world.w;
		return true;
	}

	uint32_t ScenePicker::GetColorTextureID() const { return m_Fbo ? m_Fbo->GetColorAttachmentRendererID(0) : 0; }
	uint32_t ScenePicker::GetIdTextureID() const    { return m_Fbo ? m_Fbo->GetColorAttachmentRendererID(1) : 0; }
	uint32_t ScenePicker::GetWidth() const          { return m_Fbo ? m_Fbo->GetWidth()  : 0; }
	uint32_t ScenePicker::GetHeight() const         { return m_Fbo ? m_Fbo->GetHeight() : 0; }
}
