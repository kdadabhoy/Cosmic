#pragma once

// LavaFlowBuilder.h
//
// Frontier volcano geometry (Phase 11, doc 10 F12b). Pure, header-only builders
// that turn a Cosmic::Terrain query surface into meshes for the FlowEmissive
// material (assets/shaders/FlowEmissive.glsl):
//
//   BuildFlow    — a downhill lava RIBBON. From a rim start point it marches down
//                  the slope (following the terrain's downhill direction), hugging
//                  the surface, and emits a triangle ribbon whose UVs are authored
//                  the way FlowEmissive wants them: +U = arclength / width (grows
//                  downstream so the shader's exp(-U*coolAlongLength) cools the
//                  flow as it descends), V spans [0,1] across the flow.
//   BuildLavaDisc— a flat RADIAL disc for the caldera lava lake: U = r/radius
//                  (0 at the hot centre, 1 at the cooling rim), V = angle.
//
// App-side by design: the engine ships the generic FlowEmissive material feature;
// the lava-flow SHAPE is app content. All world-space (drawn with u_Model = I).
//
// DOWNHILL NOTE: the terrain's CPU normal is N = normalize(-dh/dx, 1, -dh/dz), so
// N.xz already points DOWNHILL (= -gradient). We march `pos += normalize(N.xz)*step`
// (the doc's "-=" was a sign slip — we march actually-downhill and verify height
// descends, stopping at a flat / local minimum or the sea).

#include <Cosmic.h>

#include <glm/glm.hpp>

#include <cmath>
#include <vector>

namespace Frontier
{
    struct LavaFlowBuilder
    {
        /**
         * March downhill from `startXZ` (world X,Z), returning the world-space
         * surface points of the descent (Y from the terrain). Stops at a flat spot,
         * the terrain edge, or once the surface drops to/below `stopY` (the sea).
         */
        static std::vector<glm::vec3> TracePath(const Cosmic::Terrain& terrain,
                                                glm::vec2 startXZ, float step,
                                                int maxSteps, float stopY)
        {
            std::vector<glm::vec3> path;
            path.reserve(static_cast<size_t>(maxSteps) + 1);

            glm::vec2 pos = startXZ;
            path.push_back({ pos.x, terrain.SampleHeight(pos.x, pos.y), pos.y });

            for (int i = 0; i < maxSteps; ++i)
            {
                const glm::vec3 n = terrain.SampleNormal(pos.x, pos.y);
                glm::vec2 down(n.x, n.z);                 // = -gradient (downhill)
                const float dl = glm::length(down);
                if (dl < 1e-4f)
                    break;                                // flat spot / summit — stop
                down /= dl;

                const glm::vec2 next = pos + down * step;
                if (!terrain.Contains(next.x, next.y))
                    break;

                const float hNext = terrain.SampleHeight(next.x, next.y);
                pos = next;
                path.push_back({ next.x, hNext, next.y });

                if (hNext <= stopY)
                    break;                                // reached the shoreline
            }
            return path;
        }

        /** Build a ribbon mesh over a traced `path` (world-space). UVs: +U =
         *  arclength/width, V spans [0,1] across. `lift` holds it above the surface. */
        static Cosmic::Ref<Cosmic::Mesh> BuildRibbon(const Cosmic::Terrain& terrain,
                                                     const std::vector<glm::vec3>& path,
                                                     float width, float lift)
        {
            if (path.size() < 2)
                return nullptr;

            const float halfW = width * 0.5f;
            std::vector<Cosmic::MeshVertex> verts;
            std::vector<uint32_t>           indices;
            verts.reserve(path.size() * 2);
            indices.reserve((path.size() - 1) * 6);

            float arc = 0.0f;
            for (size_t i = 0; i < path.size(); ++i)
            {
                if (i > 0)
                    arc += glm::length(glm::vec2(path[i].x - path[i - 1].x,
                                                 path[i].z - path[i - 1].z));

                // Flow tangent (central where possible) → across = its XZ perpendicular.
                const glm::vec3 a = path[i > 0 ? i - 1 : i];
                const glm::vec3 b = path[i + 1 < path.size() ? i + 1 : i];
                glm::vec2 flow(b.x - a.x, b.z - a.z);
                if (glm::dot(flow, flow) < 1e-8f)
                    flow = glm::vec2(1.0f, 0.0f);
                flow = glm::normalize(flow);
                const glm::vec2 across(-flow.y, flow.x);

                const float uCoord = arc / std::max(width, 0.01f);
                for (int side = 0; side < 2; ++side)
                {
                    const float s  = (side == 0) ? -1.0f : 1.0f;
                    const glm::vec2 xz(path[i].x + across.x * (s * halfW),
                                       path[i].z + across.y * (s * halfW));
                    const float y  = terrain.SampleHeight(xz.x, xz.y) + lift;

                    Cosmic::MeshVertex v;
                    v.Position = { xz.x, y, xz.y };
                    v.Normal   = terrain.SampleNormal(xz.x, xz.y);
                    v.TexCoord = { uCoord, (side == 0) ? 0.0f : 1.0f };
                    verts.push_back(v);
                }
            }

            for (uint32_t i = 0; i + 1 < static_cast<uint32_t>(path.size()); ++i)
            {
                const uint32_t l0 = i * 2, r0 = i * 2 + 1, l1 = i * 2 + 2, r1 = i * 2 + 3;
                indices.insert(indices.end(), { l0, l1, r0,  r0, l1, r1 });
            }

            return Cosmic::Mesh::Create(verts, indices);
        }

        /** Convenience: trace a downhill path from `startXZ` and build its ribbon. */
        static Cosmic::Ref<Cosmic::Mesh> BuildFlow(const Cosmic::Terrain& terrain,
                                                   glm::vec2 startXZ, float width, float step,
                                                   int maxSteps, float lift, float stopY)
        {
            return BuildRibbon(terrain, TracePath(terrain, startXZ, step, maxSteps, stopY), width, lift);
        }

        /**
         * Flat radial disc for a caldera lava LAKE, centred at world (centerXZ, y).
         * U = r / radius (0 hot centre → 1 cooler rim), V = angle/2π. Normal +Y.
         */
        static Cosmic::Ref<Cosmic::Mesh> BuildLavaDisc(glm::vec2 centerXZ, float y,
                                                       float radius, uint32_t segments)
        {
            segments = segments < 6 ? 6 : segments;
            std::vector<Cosmic::MeshVertex> verts;
            std::vector<uint32_t>           indices;
            verts.reserve(segments + 1);
            indices.reserve(segments * 3);

            Cosmic::MeshVertex c;
            c.Position = { centerXZ.x, y, centerXZ.y };
            c.Normal   = { 0.0f, 1.0f, 0.0f };
            c.TexCoord = { 0.0f, 0.5f };
            verts.push_back(c);

            const float twoPi = 6.28318530718f;
            for (uint32_t i = 0; i < segments; ++i)
            {
                const float a = static_cast<float>(i) / static_cast<float>(segments) * twoPi;
                Cosmic::MeshVertex v;
                v.Position = { centerXZ.x + std::cos(a) * radius, y, centerXZ.y + std::sin(a) * radius };
                v.Normal   = { 0.0f, 1.0f, 0.0f };
                v.TexCoord = { 1.0f, static_cast<float>(i) / static_cast<float>(segments) };
                verts.push_back(v);
            }

            for (uint32_t i = 0; i < segments; ++i)
            {
                const uint32_t a = 1 + i;
                const uint32_t b = 1 + (i + 1) % segments;
                indices.insert(indices.end(), { 0u, a, b });
            }

            return Cosmic::Mesh::Create(verts, indices);
        }
    };
}
