// scene/Components.h — S4.3 TransformComponent changes (vec3 Scale + optional
// quaternion rotation). Pure matrix math, headless (no GL context needed).

#include <doctest.h>

#include "scene/Components.h"
#ifndef COSMIC_2D_ONLY
#include "scene/Components3D.h"
#endif

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

using Cosmic::TransformComponent;
#ifndef COSMIC_2D_ONLY
using Cosmic::MeshRendererComponent;
#endif

static bool Mat4Near(const glm::mat4& a, const glm::mat4& b, float eps = 1e-4f)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (std::abs(a[c][r] - b[c][r]) > eps)
                return false;
    return true;
}

TEST_CASE("TransformComponent: a vec3 scale lands on the matrix diagonal")
{
    TransformComponent tc;
    tc.Scale = { 2.0f, 3.0f, 4.0f };   // was vec2 before S4.3

    const glm::mat4 m = tc.GetTransform();

    CHECK(m[0][0] == doctest::Approx(2.0f));
    CHECK(m[1][1] == doctest::Approx(3.0f));
    CHECK(m[2][2] == doctest::Approx(4.0f));

    // No translation/rotation → strictly diagonal.
    CHECK(m[3][0] == doctest::Approx(0.0f));
    CHECK(m[3][1] == doctest::Approx(0.0f));
    CHECK(m[3][2] == doctest::Approx(0.0f));
}

TEST_CASE("TransformComponent: quaternion rotation matches the Euler product")
{
    // Quaternion mode: a 45-degree rotation about +Y.
    TransformComponent q;
    q.UseQuatRotation = true;
    q.RotationQuat    = glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    // Euler mode: the same rotation expressed as Euler degrees {0, 45, 0}.
    TransformComponent e;
    e.Rotation = { 0.0f, 45.0f, 0.0f };

    CHECK(Mat4Near(q.GetTransform(), e.GetTransform()));
}

TEST_CASE("TransformComponent: UseQuatRotation defaults off (Euler path is the default)")
{
    TransformComponent tc;
    CHECK(tc.UseQuatRotation == false);
    // Default transform is identity (no pos/rot, unit scale).
    CHECK(Mat4Near(tc.GetTransform(), glm::mat4(1.0f)));
}

#ifndef COSMIC_2D_ONLY
TEST_CASE("MeshRendererComponent: sane defaults")
{
    MeshRendererComponent mr;
    CHECK(mr.MeshAsset == nullptr);
    CHECK(mr.MaterialAsset == nullptr);
    CHECK(mr.CastShadows == true);
    CHECK(mr.Color == glm::vec4(1.0f));
}
#endif   // COSMIC_2D_ONLY — W4 moved MeshRendererComponent to Components3D.h
