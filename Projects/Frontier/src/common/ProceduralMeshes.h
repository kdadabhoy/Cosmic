#pragma once

// ProceduralMeshes.h
//
// Frontier flora/fauna geometry (Phase 11, doc 10 F12c). Pure, header-only mesh
// builders for the instanced scatter (F5) and the boids: a low-poly pine, a
// noise-displaced boulder, and a stretched-tetrahedron bird. All emit the
// engine's canonical MeshVertex (position/normal/uv/tangent) so they draw with
// PBRInstanced.glsl. App-side content — the engine ships the primitives + the
// instanced path; these compose the showcase's props.

#include <Cosmic.h>

#include <glm/glm.hpp>

#include <cmath>
#include <vector>

namespace Frontier::ProceduralMeshes
{
    namespace detail
    {
        // Append a cone (apex up) with an outward-normal side + base cap.
        inline void AppendCone(std::vector<Cosmic::MeshVertex>& v, std::vector<uint32_t>& idx,
                               glm::vec3 baseCenter, float radius, float height, uint32_t seg)
        {
            const uint32_t start = static_cast<uint32_t>(v.size());
            const float twoPi = 6.28318530718f;
            const glm::vec3 apex = baseCenter + glm::vec3(0.0f, height, 0.0f);

            // Side: a fan of apex + base ring (per-segment normals point outward+up).
            for (uint32_t i = 0; i < seg; ++i)
            {
                const float a0 = static_cast<float>(i)       / seg * twoPi;
                const float a1 = static_cast<float>(i + 1)   / seg * twoPi;
                const float am = 0.5f * (a0 + a1);

                const glm::vec3 p0 = baseCenter + glm::vec3(std::cos(a0) * radius, 0.0f, std::sin(a0) * radius);
                const glm::vec3 p1 = baseCenter + glm::vec3(std::cos(a1) * radius, 0.0f, std::sin(a1) * radius);
                const glm::vec3 n  = glm::normalize(glm::vec3(std::cos(am) * height, radius, std::sin(am) * height));

                const uint32_t b = static_cast<uint32_t>(v.size());
                v.push_back({ p0,   n, { 0.0f, 0.0f }, {} });
                v.push_back({ p1,   n, { 1.0f, 0.0f }, {} });
                v.push_back({ apex, n, { 0.5f, 1.0f }, {} });
                idx.insert(idx.end(), { b, b + 1, b + 2 });
            }

            // Base cap (normal -Y).
            const uint32_t c = static_cast<uint32_t>(v.size());
            v.push_back({ baseCenter, { 0.0f, -1.0f, 0.0f }, { 0.5f, 0.5f }, {} });
            for (uint32_t i = 0; i < seg; ++i)
            {
                const float a0 = static_cast<float>(i)     / seg * twoPi;
                const float a1 = static_cast<float>(i + 1) / seg * twoPi;
                const glm::vec3 p0 = baseCenter + glm::vec3(std::cos(a0) * radius, 0.0f, std::sin(a0) * radius);
                const glm::vec3 p1 = baseCenter + glm::vec3(std::cos(a1) * radius, 0.0f, std::sin(a1) * radius);
                const uint32_t b = static_cast<uint32_t>(v.size());
                v.push_back({ p0, { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f }, {} });
                v.push_back({ p1, { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f }, {} });
                idx.insert(idx.end(), { c, b + 1, b });
            }
            (void)start;
        }

        inline void AppendCylinder(std::vector<Cosmic::MeshVertex>& v, std::vector<uint32_t>& idx,
                                   glm::vec3 baseCenter, float radius, float height, uint32_t seg)
        {
            const float twoPi = 6.28318530718f;
            for (uint32_t i = 0; i < seg; ++i)
            {
                const float a0 = static_cast<float>(i)     / seg * twoPi;
                const float a1 = static_cast<float>(i + 1) / seg * twoPi;
                const glm::vec3 d0(std::cos(a0), 0.0f, std::sin(a0));
                const glm::vec3 d1(std::cos(a1), 0.0f, std::sin(a1));

                const glm::vec3 b0 = baseCenter + d0 * radius;
                const glm::vec3 b1 = baseCenter + d1 * radius;
                const glm::vec3 t0 = b0 + glm::vec3(0.0f, height, 0.0f);
                const glm::vec3 t1 = b1 + glm::vec3(0.0f, height, 0.0f);

                const uint32_t b = static_cast<uint32_t>(v.size());
                v.push_back({ b0, d0, { 0.0f, 0.0f }, {} });
                v.push_back({ b1, d1, { 1.0f, 0.0f }, {} });
                v.push_back({ t0, d0, { 0.0f, 1.0f }, {} });
                v.push_back({ t1, d1, { 1.0f, 1.0f }, {} });
                idx.insert(idx.end(), { b, b + 1, b + 2,  b + 2, b + 1, b + 3 });
            }
        }
    } // namespace detail

    /** A low-poly pine: a short trunk + three stacked foliage cones merged into one
     *  mesh (drawn with a single green PBRInstanced material — per-instance tint
     *  jitter gives variety). ~8 m tall, origin at the base. */
    inline Cosmic::Ref<Cosmic::Mesh> MakePine(uint32_t seed)
    {
        Cosmic::Random rng(0x91E5u ^ static_cast<uint64_t>(seed) * 0x9e3779b9u);
        const float h = rng.Range(6.5f, 9.0f);

        std::vector<Cosmic::MeshVertex> v;
        std::vector<uint32_t>           idx;

        detail::AppendCylinder(v, idx, { 0.0f, 0.0f, 0.0f }, 0.28f, h * 0.28f, 8);
        detail::AppendCone(v, idx, { 0.0f, h * 0.16f, 0.0f }, h * 0.24f, h * 0.42f, 9);
        detail::AppendCone(v, idx, { 0.0f, h * 0.42f, 0.0f }, h * 0.18f, h * 0.38f, 9);
        detail::AppendCone(v, idx, { 0.0f, h * 0.66f, 0.0f }, h * 0.12f, h * 0.34f, 9);

        return Cosmic::Mesh::Create(v, idx);
    }

    /** A rocky boulder: a UV sphere radially displaced by value noise, re-emitted
     *  with per-triangle FLAT normals for a faceted read. ~1.5 m across. */
    inline Cosmic::Ref<Cosmic::Mesh> MakeBoulder(uint32_t seed)
    {
        Cosmic::Noise noise(seed);
        const uint32_t rings = 8, seg = 12;
        const float baseR = 0.75f;

        // 1) Build the displaced sphere as an indexed grid.
        std::vector<glm::vec3> pos;
        std::vector<uint32_t>  tri;
        const float pi = 3.14159265359f, twoPi = 6.28318530718f;
        for (uint32_t r = 0; r <= rings; ++r)
        {
            const float phi = pi * static_cast<float>(r) / rings;   // 0..pi
            for (uint32_t s = 0; s <= seg; ++s)
            {
                const float th = twoPi * static_cast<float>(s) / seg;
                glm::vec3 dir(std::sin(phi) * std::cos(th), std::cos(phi), std::sin(phi) * std::sin(th));
                const float disp = 0.75f + 0.5f * noise.Fbm2D(dir.x * 2.0f + 5.0f, dir.z * 2.0f + 9.0f, 3)
                                         + 0.15f * noise.Perlin2D(dir.y * 3.0f, dir.x * 3.0f);
                pos.push_back(dir * baseR * glm::clamp(disp, 0.55f, 1.4f));
            }
        }
        const uint32_t stride = seg + 1;
        for (uint32_t r = 0; r < rings; ++r)
            for (uint32_t s = 0; s < seg; ++s)
            {
                const uint32_t a = r * stride + s, b = a + stride;
                tri.insert(tri.end(), { a, b, a + 1,  a + 1, b, b + 1 });
            }

        // 2) Re-emit each triangle with a flat face normal.
        std::vector<Cosmic::MeshVertex> v;
        std::vector<uint32_t>           idx;
        v.reserve(tri.size());
        idx.reserve(tri.size());
        for (size_t i = 0; i + 2 < tri.size(); i += 3)
        {
            const glm::vec3 p0 = pos[tri[i]], p1 = pos[tri[i + 1]], p2 = pos[tri[i + 2]];
            glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
            if (glm::dot(n, n) < 1e-12f) continue;
            n = glm::normalize(n);
            const uint32_t base = static_cast<uint32_t>(v.size());
            v.push_back({ p0, n, { 0.0f, 0.0f }, {} });
            v.push_back({ p1, n, { 1.0f, 0.0f }, {} });
            v.push_back({ p2, n, { 0.5f, 1.0f }, {} });
            idx.insert(idx.end(), { base, base + 1, base + 2 });
        }

        return Cosmic::Mesh::Create(v, idx);
    }

    /** A stretched-tetrahedron bird pointing +Z (nose forward), ~1.4 m wingspan.
     *  Boids orient it along its velocity. Flat-shaded. */
    inline Cosmic::Ref<Cosmic::Mesh> MakeBird()
    {
        const glm::vec3 nose(0.0f, 0.0f, 0.9f);
        const glm::vec3 wl(-0.7f, 0.0f, -0.4f);
        const glm::vec3 wr( 0.7f, 0.0f, -0.4f);
        const glm::vec3 top(0.0f, 0.18f, -0.2f);

        const uint32_t faces[4][3] = { {0,1,2}, {0,2,3}, {0,3,1}, {1,3,2} };
        const glm::vec3 P[4] = { nose, wl, wr, top };

        std::vector<Cosmic::MeshVertex> v;
        std::vector<uint32_t>           idx;
        for (auto& f : faces)
        {
            const glm::vec3 p0 = P[f[0]], p1 = P[f[1]], p2 = P[f[2]];
            glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
            if (glm::dot(n, n) < 1e-12f) n = glm::vec3(0.0f, 1.0f, 0.0f);
            else n = glm::normalize(n);
            const uint32_t b = static_cast<uint32_t>(v.size());
            v.push_back({ p0, n, { 0.0f, 0.0f }, {} });
            v.push_back({ p1, n, { 1.0f, 0.0f }, {} });
            v.push_back({ p2, n, { 0.5f, 1.0f }, {} });
            idx.insert(idx.end(), { b, b + 1, b + 2 });
        }
        return Cosmic::Mesh::Create(v, idx);
    }
}
