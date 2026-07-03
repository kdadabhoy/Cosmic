#pragma once

// VolcanoScene.h
//
// Frontier's reusable volcano assembly (Phase 11, doc 10 F12b + F13). Owns every
// piece of "a realistic volcano" on top of a Cosmic::Terrain caldera: FlowEmissive
// lava-flow ribbons + a caldera lava lake, a bent smoke column, rising embers, a
// heat-haze distortion field, steam fumaroles on the flanks, a triggerable lava-
// bomb fountain (ERUPTION), warm pulsing point lights, and the F10 rumble loop
// that swells with proximity.
//
// SCALE-AWARE: the caldera radius sets the scene scale, so every emitter's particle
// SIZE and SPEED is derived from it — a 9 cm ember preset is sub-pixel from 500 m,
// so smoke/embers/fountain scale up with the volcano. LOD-safe lava: flow ribbons
// are lifted well clear of the terrain (FlowLift) so the coarse-LOD rendered surface
// can't clip through them ("glitching in/out"); a small march step keeps segments
// hugging bumps.
//
// App-side by design (master-roadmap rule 8): the engine ships the generic
// FlowEmissive material, particle presets, point-light block, distortion field
// and audio verbs — the volcano is CONTENT composed here so both the seamless
// island (F12b) and the Night Volcano variant (F13) share one builder.
//
// USAGE (world-side, once the terrain is on the GPU / main thread):
//   m_Volcano.Build(terrain, cfg);                 // one-time; needs a GL context
//   ... per frame:
//   m_Volcano.Update(dt, timeSeconds, camPos);     // advances emitters + lights + audio
//   m_Volcano.Submit(desc);                        // appends emitters + point lights
//   desc.DrawOpaque = [&](const SceneDrawContext& c){ m_Volcano.DrawLava(c); ... };
//   // desc.Settings.HeatHaze must be true for the shimmer to appear.
//   m_Volcano.TriggerEruption();                   // on a button / key — lava fountain + surge

#include <Cosmic.h>

#include "common/LavaFlowBuilder.h"
#include "common/DistanceLoop.h"
#include "common/ProceduralAudio.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Frontier
{
    /** Where + how big the volcano is, and how loud/busy its effects run. All
     *  world-space; the caller derives CenterXZ/CalderaRadius from its terrain. */
    struct VolcanoConfig
    {
        glm::vec2 CenterXZ{ 0.0f };        // caldera centre (world X, Z)
        float     CalderaRadiusWorld = 180.0f;
        float     SeaLevelY = 0.0f;        // flows stop here
        uint32_t  Seed      = 1337u;

        int   FlowCount     = 3;           // downhill lava ribbons off the rim
        float FlowWidth     = 12.0f;
        float FlowStep      = 5.0f;        // small = ribbon hugs bumps (fewer gaps)
        float FlowLift      = 3.0f;        // metres above terrain (clears LOD clipping)
        int   FlowMaxSteps  = 180;

        float EmberRate     = 300.0f;      // additive spark rate
        float SmokeRate     = 40.0f;
        int   FumaroleCount = 4;           // steam vents on the flanks

        int   ColumnLights  = 0;           // pulsing lights stacked up the smoke column (F13)
        float LightRadius   = 70.0f;
        float LightIntensity = 6.0f;       // base intensity of the warm lava lights

        float RumbleRadius  = 900.0f;
        float RumbleVolume  = 1.0f;

        float EruptionDuration = 9.0f;     // seconds a triggered eruption sustains
    };

    class VolcanoScene
    {
    public:
        VolcanoScene() = default;
        ~VolcanoScene() { Shutdown(); }

        VolcanoScene(const VolcanoScene&)            = delete;
        VolcanoScene& operator=(const VolcanoScene&) = delete;

        bool      IsBuilt() const { return m_Built; }
        bool      IsErupting() const { return m_EruptionTime > 0.0f; }
        glm::vec3 CalderaWorld() const { return { m_Cfg.CenterXZ.x, m_LavaLakeY, m_Cfg.CenterXZ.y }; }

        // ---------------------------------------------------------------------
        void Build(const Cosmic::Ref<Cosmic::Terrain>& terrain, const VolcanoConfig& cfg)
        {
            if (m_Built || !terrain)
                return;
            m_Cfg = cfg;
            Cosmic::Random rng(0x1a7a0000u ^ cfg.Seed);

            const float S = std::max(cfg.CalderaRadiusWorld, 10.0f);   // scene scale (metres)

            // Caldera floor: the terrain height at the centre = the lava-lake level.
            const float floorY = terrain->SampleHeight(cfg.CenterXZ.x, cfg.CenterXZ.y);
            m_LavaLakeY = floorY + 2.0f;

            // --- Materials (FlowEmissive). Lower lake emissive so bloom doesn't clip
            //     it to a featureless white blob; flows a touch hotter (HDR glow). ---
            m_FlowEmissiveBase = 6.5f;
            m_LakeEmissiveBase = 3.2f;
            m_FlowMat = MakeLavaMaterial("LavaFlow", 0.05f, 0.85f, 0.8f, 0.15f, m_FlowEmissiveBase);
            m_LakeMat = MakeLavaMaterial("LavaLake", 0.03f, 0.80f, 0.15f, 0.05f, m_LakeEmissiveBase);

            // --- Caldera lava lake disc ---
            m_LavaLake = LavaFlowBuilder::BuildLavaDisc(cfg.CenterXZ, m_LavaLakeY,
                                                        cfg.CalderaRadiusWorld * 0.8f, 64);

            // --- Downhill lava flows off the rim (seeded launch angles) ---
            const float launchR = cfg.CalderaRadiusWorld * 1.12f;   // just outside the rim lip
            for (int i = 0; i < cfg.FlowCount; ++i)
            {
                const float a = (static_cast<float>(i) / std::max(cfg.FlowCount, 1)) * 6.2831853f
                              + rng.Range(-0.4f, 0.4f);
                const glm::vec2 start = cfg.CenterXZ + glm::vec2(std::cos(a), std::sin(a)) * launchR;

                std::vector<glm::vec3> path =
                    LavaFlowBuilder::TracePath(*terrain, start, cfg.FlowStep, cfg.FlowMaxSteps, cfg.SeaLevelY);
                Cosmic::Ref<Cosmic::Mesh> mesh =
                    LavaFlowBuilder::BuildRibbon(*terrain, path, cfg.FlowWidth, cfg.FlowLift);
                if (!mesh)
                    continue;
                m_Flows.push_back(mesh);

                // A warm pulsing light partway down each flow (still hot there).
                if (path.size() >= 2)
                {
                    const glm::vec3 p = path[path.size() * 2 / 5];   // ~40% down
                    m_Lights.push_back({ p + glm::vec3(0.0f, 4.0f, 0.0f),
                                         cfg.LightIntensity, { 1.0f, 0.42f, 0.12f },
                                         cfg.LightRadius, rng.Range(1.4f, 2.6f), rng.Range(0.0f, 6.28f) });
                }
            }

            // Lake light (brightest, warm) at the caldera centre.
            m_Lights.push_back({ CalderaWorld() + glm::vec3(0.0f, 6.0f, 0.0f),
                                 cfg.LightIntensity * 1.6f, { 1.0f, 0.45f, 0.15f },
                                 cfg.CalderaRadiusWorld * 1.8f, 1.1f, 0.0f });

            // Optional lights stacked up the smoke column (F13 money shot).
            for (int i = 0; i < cfg.ColumnLights; ++i)
            {
                const float h = 30.0f + static_cast<float>(i) * 55.0f;
                m_Lights.push_back({ CalderaWorld() + glm::vec3(0.0f, h, 0.0f),
                                     cfg.LightIntensity * 1.2f, { 1.0f, 0.5f, 0.2f },
                                     cfg.LightRadius * 1.4f, rng.Range(1.6f, 3.0f), rng.Range(0.0f, 6.28f) });
            }

            // --- Emitters (all sizes/speeds scaled by the scene scale S) ---
            const glm::mat4 atCaldera = glm::translate(glm::mat4(1.0f),
                                        CalderaWorld() + glm::vec3(0.0f, 3.0f, 0.0f));

            // Smoke column: big soft grey puffs, buoyant + wind-bent.
            {
                Cosmic::ParticleEmitterSpec s = Cosmic::Presets::SmokeColumn(cfg.SmokeRate);
                s.MaxParticles = 4096;
                s.Shape        = Cosmic::EmitterShape::Cone;
                s.ShapeRadius  = S * 0.28f;
                s.ConeAngleDeg = 16.0f;
                s.SpeedMin = S * 0.05f; s.SpeedMax = S * 0.12f;
                s.LifeMin  = 7.0f;      s.LifeMax  = 14.0f;
                s.Gravity  = { 0.0f, S * 0.03f, 0.0f };
                s.Drag     = 0.4f;
                s.Wind     = { S * 0.05f, 0.0f, S * 0.02f };
                s.SizeStart = S * 0.18f; s.SizeEnd = S * 0.75f;
                s.ColorStart = { 0.32f, 0.30f, 0.30f, 0.60f };   // warm-dark, silhouettes the sky
                s.ColorEnd   = { 0.18f, 0.18f, 0.20f, 0.0f };
                s.SoftFadeDistance = S * 0.05f;
                m_Smoke = Cosmic::ParticleEmitter::Create(s);
                if (m_Smoke) m_Smoke->SetTransform(atCaldera);
            }
            // Rising embers over the lava lake (additive, HDR warm → blooms).
            {
                Cosmic::ParticleEmitterSpec s = Cosmic::Presets::Embers(cfg.EmberRate);
                s.MaxParticles = 8192;
                s.Shape        = Cosmic::EmitterShape::Cone;
                s.ShapeRadius  = S * 0.55f;
                s.ConeAngleDeg = 26.0f;
                s.SpeedMin = S * 0.08f; s.SpeedMax = S * 0.22f;
                s.LifeMin  = 2.5f;      s.LifeMax  = 5.5f;
                s.Gravity  = { 0.0f, S * 0.02f, 0.0f };   // buoyant lift
                s.Drag     = 0.35f;
                s.Wind     = { S * 0.04f, 0.0f, 0.0f };
                s.SizeStart = S * 0.035f; s.SizeEnd = S * 0.006f;
                s.ColorStart = { 2.4f, 1.1f, 0.35f, 1.0f };   // HDR ember → bloom
                s.ColorEnd   = { 1.2f, 0.20f, 0.03f, 0.0f };
                s.Blend = Cosmic::ParticleBlend::Additive;
                s.SoftFadeDistance = S * 0.02f;
                m_Embers = Cosmic::ParticleEmitter::Create(s);
                if (m_Embers) m_Embers->SetTransform(glm::translate(glm::mat4(1.0f),
                                         CalderaWorld() + glm::vec3(0.0f, 2.0f, 0.0f)));
            }
            // Lava-bomb fountain: burst-only (SpawnRate 0), fast up, real gravity → arc.
            {
                Cosmic::ParticleEmitterSpec s;
                s.MaxParticles = 8192;
                s.SpawnRate    = 0.0f;                     // Burst()-driven (eruption)
                s.Shape        = Cosmic::EmitterShape::Cone;
                s.ShapeRadius  = S * 0.3f;
                s.ConeAngleDeg = 30.0f;
                s.SpeedMin = S * 0.35f; s.SpeedMax = S * 0.7f;   // fires bombs tens of m up
                s.LifeMin  = 3.0f;      s.LifeMax  = 6.5f;
                s.Gravity  = { 0.0f, -9.81f, 0.0f };       // real gravity: parabolic arc
                s.Drag     = 0.05f;
                s.SizeStart = S * 0.05f; s.SizeEnd = S * 0.015f;
                s.ColorStart = { 3.0f, 1.4f, 0.4f, 1.0f };
                s.ColorEnd   = { 1.4f, 0.15f, 0.02f, 0.0f };
                s.Blend = Cosmic::ParticleBlend::Additive;
                s.SoftFadeDistance = S * 0.02f;
                m_Fountain = Cosmic::ParticleEmitter::Create(s);
                if (m_Fountain) m_Fountain->SetTransform(glm::translate(glm::mat4(1.0f),
                                          CalderaWorld() + glm::vec3(0.0f, 4.0f, 0.0f)));
            }
            // Heat-haze distortion field: big slow soft puffs over the caldera.
            {
                Cosmic::ParticleEmitterSpec s = Cosmic::Presets::SoftPuff();
                s.MaxParticles = 256;
                s.SpawnRate    = 40.0f;
                s.Shape        = Cosmic::EmitterShape::Box;
                s.BoxExtents   = { S * 1.6f, 8.0f, S * 1.6f };
                s.SpeedMin = S * 0.01f; s.SpeedMax = S * 0.06f;
                s.LifeMin  = 1.5f; s.LifeMax  = 3.0f;
                s.SizeStart = S * 0.12f; s.SizeEnd = S * 0.28f;
                s.SoftFadeDistance = S * 0.02f;
                m_Haze = Cosmic::ParticleEmitter::Create(s);
                if (m_Haze) m_Haze->SetTransform(glm::translate(glm::mat4(1.0f),
                                         CalderaWorld() + glm::vec3(0.0f, 6.0f, 0.0f)));
            }
            // Steam fumaroles on the flanks (white soft puffs).
            for (int i = 0; i < cfg.FumaroleCount; ++i)
            {
                const float a = rng.Range(0.0f, 6.2831853f);
                const float r = cfg.CalderaRadiusWorld * rng.Range(1.3f, 2.2f);
                const glm::vec2 xz = cfg.CenterXZ + glm::vec2(std::cos(a), std::sin(a)) * r;
                const float y = terrain->SampleHeight(xz.x, xz.y);

                Cosmic::ParticleEmitterSpec s = Cosmic::Presets::SoftPuff();
                s.SpawnRate = 12.0f;
                s.SizeStart = S * 0.04f; s.SizeEnd = S * 0.16f;
                s.SpeedMin = S * 0.02f;  s.SpeedMax = S * 0.06f;
                s.LifeMin = 2.0f; s.LifeMax = 4.5f;
                s.Gravity = { 0.0f, S * 0.02f, 0.0f };
                Cosmic::Ref<Cosmic::ParticleEmitter> f = Cosmic::ParticleEmitter::Create(s);
                if (f)
                {
                    f->SetTransform(glm::translate(glm::mat4(1.0f), { xz.x, y + 1.0f, xz.y }));
                    m_Fumaroles.push_back(f);
                }
            }

            // --- Ambience: caldera rumble (swells as the camera approaches) ---
            m_Rumble.Start(ProceduralAudio::Ensure("rumble"));

            m_Built = true;
        }

        /** Kick off an eruption: a lava-bomb fountain + a surge of embers/smoke and
         *  a glow/light boost that decays over EruptionDuration. Safe if not built. */
        void TriggerEruption()
        {
            if (!m_Built)
                return;
            m_EruptionTime = m_Cfg.EruptionDuration;
            if (m_Fountain) m_Fountain->Burst(450);
            if (m_Embers)   m_Embers->Burst(1600);
            if (m_Smoke)    m_Smoke->Burst(140);
        }

        // ---------------------------------------------------------------------
        void Update(float dt, float timeSeconds, const glm::vec3& listener)
        {
            if (!m_Built)
                return;

            // Eruption envelope: quick, then decays; sustains emission + glow while active.
            float erupt = 0.0f;
            if (m_EruptionTime > 0.0f)
            {
                m_EruptionTime = std::max(0.0f, m_EruptionTime - dt);
                erupt = glm::clamp(m_EruptionTime / std::max(m_Cfg.EruptionDuration, 0.001f), 0.0f, 1.0f);
                if (m_Fountain) m_Fountain->Burst(static_cast<uint32_t>(280.0f * erupt * dt));
                if (m_Embers)   m_Embers->Burst(static_cast<uint32_t>(600.0f * erupt * dt));
                if (m_Smoke)    m_Smoke->Burst(static_cast<uint32_t>(60.0f  * erupt * dt));
            }

            // Lava glow surges during an eruption (material HDR emissive ↑ → more bloom).
            if (m_FlowMat) m_FlowMat->Set("u_Time", timeSeconds);
            if (m_LakeMat) m_LakeMat->Set("u_Time", timeSeconds);
            if (m_FlowMat) m_FlowMat->Set("u_EmissiveIntensity", m_FlowEmissiveBase * (1.0f + 0.9f * erupt));
            if (m_LakeMat) m_LakeMat->Set("u_EmissiveIntensity", m_LakeEmissiveBase * (1.0f + 1.6f * erupt));

            for (LavaLight& L : m_Lights)
                L.Current = L.BaseIntensity * (0.72f + 0.28f * std::sin(timeSeconds * L.PulseSpeed + L.PulsePhase))
                          * (1.0f + 2.2f * erupt);

            if (m_Smoke)    m_Smoke->Update(dt, timeSeconds);
            if (m_Embers)   m_Embers->Update(dt, timeSeconds);
            if (m_Fountain) m_Fountain->Update(dt, timeSeconds);
            if (m_Haze)     m_Haze->Update(dt, timeSeconds);
            for (auto& f : m_Fumaroles)
                if (f) f->Update(dt, timeSeconds);

            m_Rumble.Update(listener, CalderaWorld(), m_Cfg.RumbleRadius,
                            m_Cfg.RumbleVolume * (1.0f + 0.4f * erupt));
        }

        // Append the volcano's emitters + point lights to a frame description. The
        // heat-haze needs desc.Settings.HeatHaze = true (the CALLER's choice).
        void Submit(Cosmic::SceneRenderDesc& desc)
        {
            if (!m_Built)
                return;

            if (m_Smoke)    desc.Emitters.push_back(m_Smoke.get());
            if (m_Embers)   desc.Emitters.push_back(m_Embers.get());
            if (m_Fountain) desc.Emitters.push_back(m_Fountain.get());
            for (auto& f : m_Fumaroles)
                if (f) desc.Emitters.push_back(f.get());
            if (m_Haze) desc.DistortionEmitters.push_back(m_Haze.get());

            for (const LavaLight& L : m_Lights)
            {
                if (desc.Lights.Points.size() >= Cosmic::Renderer3D::kMaxPointLights)
                    break;
                Cosmic::Renderer3D::PointLightDesc p;
                p.Position  = L.Position;
                p.Radius    = L.Radius;
                p.Color     = L.Color;
                p.Intensity = L.Current;
                desc.Lights.Points.push_back(p);
            }
        }

        // Draw the lava geometry — call from inside desc.DrawOpaque (all passes).
        void DrawLava(const Cosmic::SceneDrawContext& ctx) const
        {
            if (!m_Built)
                return;
            const glm::mat4 I(1.0f);
            for (const auto& flow : m_Flows)
                if (flow) ctx.DrawMesh(flow, I, m_FlowMat);
            if (m_LavaLake)
                ctx.DrawMesh(m_LavaLake, I, m_LakeMat);
        }

        void Shutdown()
        {
            m_Rumble.Stop();
            m_Flows.clear();
            m_Fumaroles.clear();
            m_LavaLake.reset();
            m_Smoke.reset(); m_Embers.reset(); m_Fountain.reset(); m_Haze.reset();
            m_FlowMat.reset(); m_LakeMat.reset();
            m_Lights.clear();
            m_EruptionTime = 0.0f;
            m_Built = false;
        }

    private:
        struct LavaLight
        {
            glm::vec3 Position{ 0.0f };
            float     BaseIntensity = 1.0f;
            glm::vec3 Color{ 1.0f };
            float     Radius = 60.0f;
            float     PulseSpeed = 1.5f;
            float     PulsePhase = 0.0f;
            float     Current = 1.0f;
        };

        static Cosmic::Ref<Cosmic::Material> MakeLavaMaterial(const char* name, float flowSpeed,
                                                              float heat, float edgeCool,
                                                              float coolAlong, float emissive)
        {
            Cosmic::Ref<Cosmic::Shader> shader =
                Cosmic::AssetLibrary::GetShader("assets/shaders/FlowEmissive.glsl");
            Cosmic::Ref<Cosmic::Material> m = Cosmic::Material::Create(shader, name);
            m->Set("u_FlowSpeed",         flowSpeed);
            m->Set("u_NoiseScale",        glm::vec2(3.0f, 1.5f));
            m->Set("u_Heat",              heat);
            m->Set("u_EmissiveIntensity", emissive);
            m->Set("u_CrustColor",        glm::vec3(0.035f, 0.025f, 0.025f));
            m->Set("u_EdgeCool",          edgeCool);
            m->Set("u_CoolAlongLength",   coolAlong);
            m->Set("u_CrackScale",        14.0f);
            m->Set("u_RippleAmp",         0.15f);
            m->Set("u_Time",              0.0f);
            return m;
        }

        VolcanoConfig m_Cfg;
        bool  m_Built     = false;
        float m_LavaLakeY = 0.0f;
        float m_FlowEmissiveBase = 6.0f;
        float m_LakeEmissiveBase = 3.0f;
        float m_EruptionTime     = 0.0f;

        Cosmic::Ref<Cosmic::Material> m_FlowMat, m_LakeMat;
        std::vector<Cosmic::Ref<Cosmic::Mesh>> m_Flows;
        Cosmic::Ref<Cosmic::Mesh> m_LavaLake;

        Cosmic::Ref<Cosmic::ParticleEmitter> m_Smoke, m_Embers, m_Fountain, m_Haze;
        std::vector<Cosmic::Ref<Cosmic::ParticleEmitter>> m_Fumaroles;

        std::vector<LavaLight> m_Lights;
        DistanceLoop           m_Rumble;
    };
}
