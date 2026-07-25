// test_light2d.cpp — 2D lighting (Phase 29 W2 / §9.2), the X5 pass.
//
// Headless (no GL). Two halves, following the test_particle_noise.cpp twin
// pattern:
//
//  1. A CPU twin of the falloff in assets/shaders/Light2D.glsl's fragment stage
//     (`pow(clamp(1 - d, 0, 1), u_Falloff)`), asserted for boundary zeros,
//     monotonic decay and additive accumulation. The shader is the one line the
//     twin mirrors; the on-GPU proof is the `light2d` golden image.
//
//  2. Scene::OnRender2DLights' COMPAT GATE — no lights + white ambient returns
//     before any GL call, so a 2D scene without lights is byte-identical. That
//     gate is the invariant the light2d A/B golden pins on the GPU, and the one
//     thing about this pass that IS provable headless.
//
// Note on coverage: the pass itself needs a live context (Light2DRenderer::
// Composite creates shaders + an FBO), so this suite deliberately exercises
// only the paths that make NO GL calls — the gate, and Composite's own
// zero-size early-out. Everything downstream of those is golden-image territory.

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace Cosmic;

namespace
{
    // CPU twin of Light2D.glsl's fragment stage. `local` is the [-1,1]^2 quad
    // coordinate the vertex stage interpolates; the result is the additive HDR
    // contribution of one light at that point.
    glm::vec3 LightContribution(const glm::vec2& local, const glm::vec3& color,
                                float intensity, float falloff)
    {
        const float d = glm::length(local);
        const float f = std::pow(std::min(std::max(1.0f - d, 0.0f), 1.0f), falloff);
        return color * (intensity * f);
    }

    // The same in world space: `p` relative to a light at `center` with `radius`
    // maps to the quad's local coordinate (the vertex stage's inverse).
    glm::vec3 LightAt(const glm::vec2& p, const glm::vec2& center, float radius,
                      const glm::vec3& color, float intensity, float falloff)
    {
        if (radius <= 0.0f)
            return glm::vec3(0.0f);
        return LightContribution((p - center) / radius, color, intensity, falloff);
    }
}

TEST_SUITE("2D lighting (X5) — falloff twin")
{
    TEST_CASE("the centre is full intensity and the rim is exactly zero")
    {
        const glm::vec3 c{ 1.0f, 0.5f, 0.25f };

        const glm::vec3 centre = LightContribution({ 0.0f, 0.0f }, c, 2.0f, 2.0f);
        CHECK(centre.r == doctest::Approx(2.0f));
        CHECK(centre.g == doctest::Approx(1.0f));
        CHECK(centre.b == doctest::Approx(0.5f));

        // d == 1 on the inscribed circle -> clamp(1-d) == 0 -> no light at all.
        for (glm::vec2 rim : { glm::vec2{ 1.0f, 0.0f }, glm::vec2{ 0.0f, -1.0f },
                               glm::vec2{ 0.70710678f, 0.70710678f } })
        {
            const glm::vec3 v = LightContribution(rim, c, 2.0f, 2.0f);
            CHECK(v.r == doctest::Approx(0.0f).epsilon(1e-5));
            CHECK(v.g == doctest::Approx(0.0f).epsilon(1e-5));
            CHECK(v.b == doctest::Approx(0.0f).epsilon(1e-5));
        }

        // The quad CORNERS are outside the inscribed circle (d = sqrt(2) > 1);
        // the clamp is what keeps pow() from producing garbage there.
        const glm::vec3 corner = LightContribution({ 1.0f, 1.0f }, c, 2.0f, 2.0f);
        CHECK(corner.r == 0.0f);
        CHECK(corner.g == 0.0f);
        CHECK(corner.b == 0.0f);
    }

    TEST_CASE("intensity decays monotonically with distance, for every falloff")
    {
        for (float falloff : { 0.5f, 1.0f, 2.0f, 4.0f })
        {
            float prev = 1e9f;
            for (int i = 0; i <= 20; ++i)
            {
                const float d = i / 20.0f;
                const float v = LightContribution({ d, 0.0f }, glm::vec3(1.0f), 1.0f, falloff).r;
                CHECK(v <= prev);
                prev = v;
            }
            CHECK(prev == doctest::Approx(0.0f).epsilon(1e-5));   // reached the rim
        }
    }

    TEST_CASE("a higher falloff exponent tightens the light")
    {
        // At any interior point the tighter light is dimmer (base < 1 => a larger
        // exponent shrinks it); at the centre both are full brightness.
        const float loose = LightContribution({ 0.5f, 0.0f }, glm::vec3(1.0f), 1.0f, 1.0f).r;
        const float tight = LightContribution({ 0.5f, 0.0f }, glm::vec3(1.0f), 1.0f, 4.0f).r;
        CHECK(tight < loose);
        CHECK(LightContribution({ 0, 0 }, glm::vec3(1.0f), 1.0f, 1.0f).r ==
              doctest::Approx(LightContribution({ 0, 0 }, glm::vec3(1.0f), 1.0f, 4.0f).r));
    }

    TEST_CASE("lights accumulate additively and are order-independent")
    {
        const glm::vec2 p{ 1.0f, 0.0f };
        const glm::vec3 a = LightAt(p, { 0.0f, 0.0f }, 4.0f, { 1.0f, 0.0f, 0.0f }, 1.5f, 2.0f);
        const glm::vec3 b = LightAt(p, { 2.0f, 0.0f }, 4.0f, { 0.0f, 1.0f, 0.0f }, 1.5f, 2.0f);
        CHECK(a.r > 0.0f);
        CHECK(b.g > 0.0f);

        const glm::vec3 ab = a + b;
        const glm::vec3 ba = b + a;
        CHECK(ab.r == ba.r);
        CHECK(ab.g == ba.g);
        CHECK(ab.b == ba.b);

        // A light whose reach does not cover the point adds exactly nothing, so
        // the accumulation is unchanged (the "lights are local" property).
        // (`distant`, not `far` — <windows.h> defines `far` as a macro.)
        const glm::vec3 distant = LightAt(p, { 50.0f, 0.0f }, 4.0f, glm::vec3(1.0f), 1.5f, 2.0f);
        CHECK(distant.r == 0.0f);
        CHECK((ab + distant).r == ab.r);
    }

    TEST_CASE("a zero radius contributes nothing instead of dividing by zero")
    {
        const glm::vec3 v = LightAt({ 0.0f, 0.0f }, { 0.0f, 0.0f }, 0.0f, glm::vec3(1.0f), 2.0f, 2.0f);
        CHECK(v.r == 0.0f);
        CHECK(std::isfinite(v.r));
    }

    TEST_CASE("the ambient multiply is identity at white — the compat invariant")
    {
        // What Light2DRenderer does to the scene colour: multiply by the light
        // buffer, which is CLEARED to ambient and has nothing added where no
        // light reaches. White ambient + no lights => scene * 1 == scene.
        const glm::vec3 scene{ 0.3f, 0.62f, 0.9f };
        const glm::vec3 white{ 1.0f };
        CHECK((scene * white).r == scene.r);
        CHECK((scene * white).g == scene.g);
        CHECK((scene * white).b == scene.b);

        // A darker ambient really does darken; a light restores brightness.
        const glm::vec3 night{ 0.2f };
        CHECK((scene * night).r < scene.r);
        const glm::vec3 lit = night + LightAt({ 0, 0 }, { 0, 0 }, 4.0f, glm::vec3(1.0f), 1.0f, 2.0f);
        CHECK(lit.r > night.r);
    }
}

TEST_SUITE("2D lighting (X5) — Scene::OnRender2DLights gate")
{
    TEST_CASE("no lights + white ambient makes no GL calls (headless no-crash)")
    {
        // The gate at Scene::OnRender2DLights returns before Light2DRenderer is
        // reached; reaching it without a context would fault, so surviving this
        // call IS the assertion.
        Scene s;
        s.CreateEntity("empty");
        s.OnRender2DLights(glm::mat4(1.0f), 320, 180);

        // Explicit white ambient — same gate, taken through FindEnvironment().
        Entity env = s.CreateEntity("Environment");
        env.AddComponent<EnvironmentComponent>().Ambient2D = glm::vec3(1.0f);
        s.OnRender2DLights(glm::mat4(1.0f), 320, 180);

        // A DISABLED / inactive light still leaves the list empty, so the gate
        // holds — the T12/T13 gates run before the emptiness test.
        Entity off = s.CreateEntity("off");
        off.AddComponent<Light2DComponent>().Enabled = false;
        Entity inactive = s.CreateEntity("inactive");
        inactive.AddComponent<Light2DComponent>();
        inactive.GetComponent<TagComponent>().Active = false;
        s.OnRender2DLights(glm::mat4(1.0f), 320, 180);

        CHECK(true);   // no fault reaching here
    }

    TEST_CASE("an active light or a non-white ambient dispatches to the pass")
    {
        // Past the gate, Light2DRenderer::Composite's own zero-size early-out is
        // what keeps this headless-safe; a 0x0 target is the only way to reach
        // the dispatch without a context. The real pass is the light2d golden.
        Scene s;
        Entity l = s.CreateEntity("light");
        l.GetComponent<TransformComponent>().Position = { 1.0f, 2.0f, 0.0f };
        auto& lc = l.AddComponent<Light2DComponent>();
        lc.Radius = 5.0f; lc.Intensity = 2.0f; lc.Falloff = 3.0f;
        s.OnRender2DLights(glm::mat4(1.0f), 0, 0);

        Scene dark;
        Entity env = dark.CreateEntity("Environment");
        env.AddComponent<EnvironmentComponent>().Ambient2D = glm::vec3(0.15f);
        dark.OnRender2DLights(glm::mat4(1.0f), 0, 0);

        CHECK(true);   // no fault reaching here
    }
}
