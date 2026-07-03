#pragma once

// Boids.h
//
// Frontier wildlife (Phase 11, doc 10 F12c): a small flock that wheels around an
// anchor with the classic Reynolds rules (separation / alignment / cohesion) plus
// a loose orbit pull, producing an oriented transform per bird each frame for one
// instanced draw (Renderer3D::DrawMeshInstanced via a ScatterField-less path — the
// world uploads Transforms() into an InstanceSet). Header-only, app-side.

#include <Cosmic.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <vector>

namespace Frontier
{
    class Boids
    {
    public:
        void Init(uint32_t count, glm::vec3 anchor, float radius, float altitude,
                  float speed, float scale, uint32_t seed)
        {
            m_Anchor = anchor; m_Radius = radius; m_Altitude = altitude;
            m_Speed = speed; m_Scale = scale;

            Cosmic::Random rng(0xB01D0000u ^ seed);
            m_Birds.clear();
            m_Birds.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                const float a = rng.Range(0.0f, 6.2831853f);
                const float r = radius * rng.Range(0.6f, 1.1f);
                Bird b;
                b.Pos = anchor + glm::vec3(std::cos(a) * r, rng.Range(-0.3f, 0.3f) * altitude, std::sin(a) * r);
                // Start moving tangentially (counter-clockwise) around the anchor.
                b.Vel = glm::vec3(-std::sin(a), 0.0f, std::cos(a)) * speed;
                m_Birds.push_back(b);
            }
            m_Xforms.assign(count, glm::mat4(1.0f));
        }

        void Update(float dt)
        {
            const float sepR = 12.0f, sepR2 = sepR * sepR;
            const float neiR = 45.0f, neiR2 = neiR * neiR;

            for (size_t i = 0; i < m_Birds.size(); ++i)
            {
                glm::vec3 sep(0.0f), aliVel(0.0f), cohPos(0.0f);
                int neighbors = 0;

                for (size_t j = 0; j < m_Birds.size(); ++j)
                {
                    if (i == j) continue;
                    const glm::vec3 d = m_Birds[i].Pos - m_Birds[j].Pos;
                    const float d2 = glm::dot(d, d);
                    if (d2 < sepR2 && d2 > 1e-4f)
                        sep += d / d2;
                    if (d2 < neiR2)
                    {
                        aliVel += m_Birds[j].Vel;
                        cohPos += m_Birds[j].Pos;
                        ++neighbors;
                    }
                }

                glm::vec3 accel(0.0f);
                accel += sep * 40.0f;                                      // separation
                if (neighbors > 0)
                {
                    aliVel /= static_cast<float>(neighbors);
                    cohPos /= static_cast<float>(neighbors);
                    accel += (aliVel - m_Birds[i].Vel) * 1.2f;            // alignment
                    accel += (cohPos - m_Birds[i].Pos) * 0.6f;           // cohesion
                }

                // Loose orbit pull: hold the flock at ~m_Radius / m_Altitude around
                // the anchor with a gentle tangential bias so it circles.
                const glm::vec3 toC   = m_Anchor - m_Birds[i].Pos;
                const glm::vec2 flat(toC.x, toC.z);
                const float     rDist = glm::length(flat) + 1e-3f;
                const glm::vec2 radial = flat / rDist;
                const glm::vec2 tangent(-radial.y, radial.x);
                accel += glm::vec3(radial.x, 0.0f, radial.y) * (rDist - m_Radius) * 0.5f;
                accel += glm::vec3(tangent.x, 0.0f, tangent.y) * m_Speed * 1.5f;
                accel.y += (m_Anchor.y + m_Altitude - m_Birds[i].Pos.y) * 0.8f;

                m_Birds[i].Vel += accel * dt;

                // Clamp speed to a band so the flock looks alive but coherent.
                float sp = glm::length(m_Birds[i].Vel);
                if (sp > 1e-3f)
                {
                    const float clamped = glm::clamp(sp, m_Speed * 0.6f, m_Speed * 1.6f);
                    m_Birds[i].Vel *= clamped / sp;
                }
                m_Birds[i].Pos += m_Birds[i].Vel * dt;
            }

            // Build oriented transforms (+Z of the bird mesh → velocity).
            for (size_t i = 0; i < m_Birds.size(); ++i)
            {
                glm::vec3 fwd = m_Birds[i].Vel;
                const float sp = glm::length(fwd);
                fwd = (sp > 1e-4f) ? fwd / sp : glm::vec3(0.0f, 0.0f, 1.0f);
                glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
                glm::vec3 right = glm::cross(up, fwd);
                if (glm::dot(right, right) < 1e-6f) right = glm::vec3(1.0f, 0.0f, 0.0f);
                right = glm::normalize(right);
                up = glm::cross(fwd, right);

                glm::mat4 m(1.0f);
                m[0] = glm::vec4(right * m_Scale, 0.0f);
                m[1] = glm::vec4(up    * m_Scale, 0.0f);
                m[2] = glm::vec4(fwd   * m_Scale, 0.0f);
                m[3] = glm::vec4(m_Birds[i].Pos, 1.0f);
                m_Xforms[i] = m;
            }
        }

        const std::vector<glm::mat4>& Transforms() const { return m_Xforms; }
        uint32_t Count() const { return static_cast<uint32_t>(m_Birds.size()); }

    private:
        struct Bird { glm::vec3 Pos{ 0.0f }; glm::vec3 Vel{ 0.0f }; };
        std::vector<Bird>      m_Birds;
        std::vector<glm::mat4> m_Xforms;
        glm::vec3 m_Anchor{ 0.0f };
        float m_Radius = 200.0f, m_Altitude = 120.0f, m_Speed = 18.0f, m_Scale = 3.0f;
    };
}
