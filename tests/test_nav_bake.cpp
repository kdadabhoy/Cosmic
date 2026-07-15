// test_nav_bake.cpp — N2 acceptance. Authors a scene with a NavMeshComponent + a
// collision-view floor, bakes through SceneNav (the honest physics source), and
// asserts: recipe -> bake -> `.cnav` sidecar round-trips identical (bytes + path);
// editing a wall + rebake changes the path; the BuiltSignature gates regeneration;
// and SourceMode=FromChildren filters to the navmesh entity's descendants. Headless.

#include "doctest.h"

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/SceneNav.h"
#include "nav/NavWorld.h"

#include <glm/glm.hpp>
#include <filesystem>
#include <string>

using namespace Cosmic;

namespace
{
    Entity AddGround(Scene& scene, const char* name, const glm::vec3& pos, const glm::vec3& halfExtents)
    {
        Entity g = scene.CreateEntity(name);
        g.GetComponent<TransformComponent>().Position = pos;
        g.AddComponent<BoxColliderComponent>().HalfExtents = halfExtents;   // collider-only => implicit static
        return g;
    }

    Entity AddNav(Scene& scene, NavSourceMode mode)
    {
        Entity nav = scene.CreateEntity("Nav");
        auto& nm = nav.AddComponent<NavMeshComponent>();
        nm.SourceMode   = mode;
        nm.CellSize     = 0.2f;
        nm.CellHeight   = 0.2f;
        nm.AgentRadius  = 0.4f;
        nm.AgentHeight  = 1.8f;
        nm.AgentMaxClimb = 0.4f;
        return nav;
    }
}

TEST_SUITE("Nav / scene bake + .cnav (N2)")
{
    TEST_CASE("Bakes from the collision view and finds a path")
    {
        Scene scene;
        AddGround(scene, "Ground", { 0, -0.5f, 0 }, { 12, 0.5f, 12 });
        Entity nav = AddNav(scene, NavSourceMode::WholeScene);

        REQUIRE(SceneNav::BakeSync(scene, (entt::entity)nav));
        auto& nm = nav.GetComponent<NavMeshComponent>();
        REQUIRE(nm.Nav);
        REQUIRE(nm.Nav->IsBuilt());
        CHECK(nm.BuiltSignature != 0);

        NavPath p = nm.Nav->FindPath({ -8, 0, 0 }, { 8, 0, 0 });
        CHECK(p.Reached);
        CHECK(p.Length == doctest::Approx(16.0f).epsilon(0.15));
    }

    TEST_CASE("Recipe -> bake -> .cnav sidecar round-trips identical")
    {
        Scene scene;
        AddGround(scene, "Ground", { 0, -0.5f, 0 }, { 12, 0.5f, 12 });
        Entity nav = AddNav(scene, NavSourceMode::WholeScene);
        REQUIRE(SceneNav::BakeSync(scene, (entt::entity)nav));
        auto& nm = nav.GetComponent<NavMeshComponent>();

        const std::string path =
            (std::filesystem::temp_directory_path() / "cosmic_nav_roundtrip.cnav").string();
        REQUIRE(SceneNav::SaveSidecar(nm, path));

        NavMeshComponent loaded;              // a fresh component (recipe defaults)
        REQUIRE(SceneNav::LoadSidecar(loaded, path));
        REQUIRE(loaded.Nav);
        REQUIRE(loaded.Nav->IsBuilt());

        // Byte-identical serialized navmesh + identical path.
        CHECK(nm.Nav->Serialize().Bytes == loaded.Nav->Serialize().Bytes);
        NavPath a = nm.Nav->FindPath({ -8, 0, 0 }, { 8, 0, 0 });
        NavPath b = loaded.Nav->FindPath({ -8, 0, 0 }, { 8, 0, 0 });
        CHECK(b.Reached);
        CHECK(b.Length == doctest::Approx(a.Length));

        std::error_code ec; std::filesystem::remove(std::filesystem::u8path(path), ec);
    }

    TEST_CASE("Editing a wall + rebake changes the path; signature gates regen")
    {
        Scene scene;
        AddGround(scene, "Ground", { 0, -0.5f, 0 }, { 12, 0.5f, 12 });
        Entity nav = AddNav(scene, NavSourceMode::WholeScene);
        auto& nm = nav.GetComponent<NavMeshComponent>();

        REQUIRE(SceneNav::BakeSync(scene, (entt::entity)nav));
        const std::size_t sig0 = nm.BuiltSignature;
        const float directLen = nm.Nav->FindPath({ -8, 0, 0 }, { 8, 0, 0 }).Length;

        // Re-gathering the SAME scene yields the SAME signature (regen would no-op).
        {
            std::vector<float> v; std::vector<int> t;
            SceneNav::GatherGeometry(scene, (entt::entity)nav, v, t);
            CHECK(SceneNav::Signature(nm, v, t) == sig0);
        }

        // Add a long thin wall across the middle (leaving gaps at |z| > 8): the
        // straight route is blocked, so the path must detour and get LONGER.
        Entity wall = scene.CreateEntity("Wall");
        wall.GetComponent<TransformComponent>().Position = { 0, 1.0f, 0 };
        wall.AddComponent<BoxColliderComponent>().HalfExtents = { 0.3f, 1.0f, 8.0f };

        REQUIRE(SceneNav::BakeSync(scene, (entt::entity)nav));
        CHECK(nm.BuiltSignature != sig0);   // geometry changed -> signature changed

        NavPath detour = nm.Nav->FindPath({ -8, 0, 0 }, { 8, 0, 0 });
        CHECK(detour.Reached);
        CHECK(detour.Length > directLen + 1.0f);   // forced around the wall
    }

    TEST_CASE("Async BeginBake/FinishBake installs the navmesh (the no-stall path)")
    {
        Scene scene;
        AddGround(scene, "Ground", { 0, -0.5f, 0 }, { 12, 0.5f, 12 });
        Entity nav = AddNav(scene, NavSourceMode::WholeScene);
        auto& nm = nav.GetComponent<NavMeshComponent>();

        NavBakeJob job = SceneNav::BeginBake(scene, (entt::entity)nav);
        REQUIRE(job.Valid);
        CHECK(nm.Baking);

        // Headless (no JobSystem pool) => the build ran inline and is already done.
        REQUIRE(job.IsDone());
        REQUIRE(SceneNav::FinishBake(scene, (entt::entity)nav, job));
        CHECK_FALSE(nm.Baking);
        REQUIRE(nm.Nav);
        CHECK(nm.Nav->IsBuilt());
        CHECK(nm.BuiltSignature != 0);
        CHECK(nm.Nav->FindPath({ -8, 0, 0 }, { 8, 0, 0 }).Reached);
    }

    TEST_CASE("SourceMode=FromChildren bakes only the navmesh entity's descendants")
    {
        Scene scene;
        Entity nav = AddNav(scene, NavSourceMode::FromChildren);

        // A ground parented UNDER the nav entity (in scope) ...
        Entity child = AddGround(scene, "ChildGround", { 0, -0.5f, 0 }, { 6, 0.5f, 6 });
        scene.SetParent(child, nav, /*keepWorldPose*/ false);

        // ... and a far ground NOT parented under it (out of scope).
        AddGround(scene, "OtherGround", { 40, -0.5f, 0 }, { 6, 0.5f, 6 });

        REQUIRE(SceneNav::BakeSync(scene, (entt::entity)nav));
        auto& nm = nav.GetComponent<NavMeshComponent>();
        REQUIRE(nm.Nav->IsBuilt());

        // The child ground is navigable; the out-of-scope ground is not on the mesh.
        CHECK(nm.Nav->NearestPoint({ 0, 0, 0 }).has_value());
        CHECK_FALSE(nm.Nav->NearestPoint({ 40, 0, 0 }, glm::vec3(2, 4, 2)).has_value());
    }
}
