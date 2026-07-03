#pragma once

// Scatter.h
//
// Frontier prop scattering (Phase 11, doc 10 F12c + F5). Two pieces:
//
//   Scatter::Generate — deterministic placement of props over a Cosmic::Terrain
//     using the engine PCG32 RNG: rejects steep slopes + out-of-band altitudes +
//     an app keep-out (lake/river), then builds a rigid+uniform-scale transform
//     (Y snapped to the ground) and a jittered albedo tint per instance.
//
//   ScatterField — owns the mesh + PBRInstanced material + the full instance list
//     and a Cosmic::InstanceSet, and each frame culls against the MAIN camera
//     frustum (F5 policy: cull once, draw the survivors in every pass) before an
//     instanced draw. One draw scatters a whole forest.
//
// App-side content; the engine ships the generic InstanceSet + Frustum + instanced
// draw path. Uniform scale only (the instanced shader derives normals from
// mat3(Model)) — see InstanceSet.h.

#include <Cosmic.h>

#include "common/ProceduralMeshes.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <functional>
#include <vector>

namespace Frontier
{
    struct ScatterParams
    {
        uint32_t Count = 2000;
        uint32_t Seed  = 1u;

        float MinHeight   = 2.0f;    // world-Y band the prop may sit in (dry land)
        float MaxHeight   = 300.0f;
        float MinNormalY  = 0.72f;   // reject slopes steeper than this (N.y below → skip)
        float MinScale    = 0.8f;
        float MaxScale    = 1.3f;
        float YOffset     = -0.3f;   // sink the base slightly into the ground
        float EdgeInset   = 0.03f;   // keep this fraction of the rect clear of the border

        glm::vec3 BaseTint{ 1.0f };
        float     TintJitter = 0.12f;

        // Optional keep-out in world XZ (lake / river footprints). Null = accept all.
        std::function<bool(glm::vec2)> RejectXZ;
    };

    struct ScatterInstance
    {
        glm::mat4 Transform{ 1.0f };
        glm::vec4 Tint{ 1.0f };
        glm::vec3 Position{ 0.0f };   // for frustum culling
    };

    struct Scatter
    {
        static std::vector<ScatterInstance> Generate(const Cosmic::Terrain& terrain,
                                                     const ScatterParams& p)
        {
            std::vector<ScatterInstance> out;
            out.reserve(p.Count);

            const glm::vec2 minCorner = terrain.GetWorldMinCorner();
            const float     size      = terrain.GetWorldSize();
            const float     inset     = size * p.EdgeInset;

            Cosmic::Random rng(0x5ca77e00u ^ p.Seed);
            const uint32_t maxAttempts = p.Count * 12u + 64u;

            for (uint32_t a = 0; a < maxAttempts && out.size() < p.Count; ++a)
            {
                const glm::vec2 xz(rng.Range(minCorner.x + inset, minCorner.x + size - inset),
                                   rng.Range(minCorner.y + inset, minCorner.y + size - inset));

                if (p.RejectXZ && p.RejectXZ(xz))
                    continue;

                const float h = terrain.SampleHeight(xz.x, xz.y);
                if (h < p.MinHeight || h > p.MaxHeight)
                    continue;
                if (terrain.SampleNormal(xz.x, xz.y).y < p.MinNormalY)
                    continue;

                const float scale = rng.Range(p.MinScale, p.MaxScale);
                const float yaw   = rng.Range(0.0f, 6.2831853f);

                ScatterInstance inst;
                inst.Position  = { xz.x, h, xz.y };
                inst.Transform = glm::translate(glm::mat4(1.0f), { xz.x, h + p.YOffset, xz.y });
                inst.Transform = glm::rotate(inst.Transform, yaw, { 0.0f, 1.0f, 0.0f });
                inst.Transform = glm::scale(inst.Transform, glm::vec3(scale));

                const float j = p.TintJitter;
                inst.Tint = glm::vec4(glm::clamp(p.BaseTint + glm::vec3(rng.Range(-j, j),
                                                                        rng.Range(-j, j),
                                                                        rng.Range(-j, j)),
                                                 0.0f, 1.0f), 1.0f);
                out.push_back(inst);
            }
            return out;
        }
    };

    /** Owns an instanced mesh field: full instance list + a GPU InstanceSet.
     *  CullAndUpload() picks the survivors against a frustum (call once/frame);
     *  Draw() issues the instanced draw for whatever pass ctx names. */
    class ScatterField
    {
    public:
        void Build(const Cosmic::Ref<Cosmic::Mesh>& mesh, const Cosmic::Ref<Cosmic::Material>& material,
                   std::vector<ScatterInstance> instances, float cullRadius)
        {
            m_Mesh       = mesh;
            m_Material   = material;
            m_Instances  = std::move(instances);
            m_CullRadius = cullRadius;
            m_Set        = Cosmic::InstanceSet::Create(static_cast<uint32_t>(m_Instances.size() > 0 ? m_Instances.size() : 1));
            m_Xforms.reserve(m_Instances.size());
            m_Tints.reserve(m_Instances.size());
        }

        void CullAndUpload(const Cosmic::Frustum& frustum)
        {
            if (!m_Set)
                return;
            m_Xforms.clear();
            m_Tints.clear();
            for (const ScatterInstance& inst : m_Instances)
                if (frustum.IntersectsSphere(inst.Position + glm::vec3(0.0f, m_CullRadius, 0.0f), m_CullRadius))
                {
                    m_Xforms.push_back(inst.Transform);
                    m_Tints.push_back(inst.Tint);
                }
            m_Set->SetInstances(m_Xforms.data(), m_Tints.data(),
                                static_cast<uint32_t>(m_Xforms.size()));
        }

        void Draw(const Cosmic::SceneDrawContext& ctx, int entityID = -1) const
        {
            if (m_Set && m_Mesh && m_Material && m_Set->GetCount() > 0)
                ctx.DrawMeshInstanced(m_Mesh, m_Material, m_Set, m_Set->GetCount(), entityID);
        }

        uint32_t TotalCount()   const { return static_cast<uint32_t>(m_Instances.size()); }
        uint32_t VisibleCount() const { return m_Set ? m_Set->GetCount() : 0u; }

        void Reset()
        {
            m_Set.reset(); m_Mesh.reset(); m_Material.reset();
            m_Instances.clear(); m_Xforms.clear(); m_Tints.clear();
        }

    private:
        Cosmic::Ref<Cosmic::Mesh>        m_Mesh;
        Cosmic::Ref<Cosmic::Material>    m_Material;
        Cosmic::Ref<Cosmic::InstanceSet> m_Set;
        std::vector<ScatterInstance>     m_Instances;
        std::vector<glm::mat4>           m_Xforms;
        std::vector<glm::vec4>           m_Tints;
        float                            m_CullRadius = 8.0f;
    };
}
