// test_scene_serializer.cpp — UUIDs + JSON scene serialization (Phase 13 / E2).
// Headless: null mesh/material refs, no GL context.
//
// Acceptance (plan doc 11 E2): save->load->save produces identical JSON (incl.
// an unknown-component blob and an EntityRef); UUID collision test over 1e6
// draws; headless.

#include <doctest.h>

#include "core/UUID.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/SceneSerializer.h"
#include "scene/ComponentRegistry.h"
#include "reflect/TypeRegistry.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>

using namespace Cosmic;

// Test-only component with an EntityRef field (no engine component has one until
// E3's RelationshipComponent), used to prove EntityRef hex round-trips.
namespace
{
    struct LinkComponent
    {
        uint64_t Target = 0;   // holds a UUID value; reflected as EntityRef
    };
}
CS_REGISTER_COMPONENT(LinkComponent)

TEST_CASE("E2: UUID hex round-trips and 0 is the null value")
{
    UUID u;
    CHECK(u.IsValid());
    CHECK(UUID::FromString(u.ToString()) == u);
    CHECK(u.ToString().size() == 16);

    CHECK(UUID::FromString("") == UUID(0));
    CHECK_FALSE(UUID(0).IsValid());
    CHECK(UUID(0x1234abcd).ToString() == "000000001234abcd");
}

TEST_CASE("E2: one million UUID draws collide zero times")
{
    std::unordered_set<uint64_t> seen;
    seen.reserve(1'000'000);
    size_t collisions = 0;
    for (int i = 0; i < 1'000'000; ++i)
    {
        UUID u;
        if (!seen.insert(u.Value()).second)
            ++collisions;
    }
    CHECK(collisions == 0);
}

TEST_CASE("E2: scene round-trips with a stable EntityRef and identical JSON")
{
    Reflect::Class<LinkComponent>("Link", "Test")
        .Field("Target", &LinkComponent::Target).AsEntityRef();

    Scene scene;
    Entity a = scene.CreateEntity("Alpha");
    a.GetComponent<TransformComponent>().Position = { 1.0f, 2.0f, 3.0f };
    auto& mr = a.AddComponent<MeshRendererComponent>();   // null mesh ref (headless)
    mr.Color = { 0.2f, 0.4f, 0.8f, 1.0f };

    Entity b = scene.CreateEntity("Beta");
    b.AddComponent<DirectionalLightComponent>().Intensity = 2.5f;

    const UUID aID = a.GetComponent<IDComponent>().ID;
    const UUID bID = b.GetComponent<IDComponent>().ID;

    // Alpha references Beta by UUID.
    a.AddComponent<LinkComponent>().Target = bID.Value();

    const std::string save1 = SceneSerializer::SaveToString(scene);

    // The EntityRef must be emitted as Beta's hex string.
    CHECK(save1.find(bID.ToString()) != std::string::npos);

    // Load into a fresh scene and confirm the reference resolves to the same id.
    Scene scene2;
    REQUIRE(SceneSerializer::LoadFromString(scene2, save1));

    Entity a2 = scene2.FindByUUID(aID);
    Entity b2 = scene2.FindByUUID(bID);
    REQUIRE(a2);
    REQUIRE(b2);
    CHECK(a2.GetComponent<TagComponent>().Tag == "Alpha");
    CHECK(a2.GetComponent<TransformComponent>().Position.y == doctest::Approx(2.0f));
    CHECK(a2.GetComponent<MeshRendererComponent>().Color.z == doctest::Approx(0.8f));
    CHECK(a2.GetComponent<LinkComponent>().Target == bID.Value());
    CHECK(b2.GetComponent<DirectionalLightComponent>().Intensity == doctest::Approx(2.5f));

    // save -> load -> save is byte-identical.
    const std::string save2 = SceneSerializer::SaveToString(scene2);
    CHECK(save1 == save2);
}

TEST_CASE("E2: unknown component blocks survive a round-trip verbatim")
{
    // A scene authored by a build that knew a component this build does not.
    const std::string src = R"({
      "cosmic_scene": 1,
      "entities": [
        {
          "id": "00000000000000aa",
          "components": {
            "Transform": {
              "Position": [4.0, 5.0, 6.0], "Rotation": [0.0, 0.0, 0.0],
              "Scale": [1.0, 1.0, 1.0], "RotationQuat": [1.0, 0.0, 0.0, 0.0],
              "UseQuatRotation": false
            },
            "Tag": { "Tag": "Legacy" },
            "MysteryComponent": { "customField": 42, "nested": { "a": 1, "b": 2 } }
          }
        }
      ]
    })";

    Scene scene;
    REQUIRE(SceneSerializer::LoadFromString(scene, src));

    // The unknown block was stashed opaquely on the entity.
    Entity e = scene.FindByUUID(UUID(0xaa));
    REQUIRE(e);
    REQUIRE(e.HasComponent<OpaqueComponentsComponent>());
    CHECK(e.GetComponent<OpaqueComponentsComponent>().Blocks.size() == 1);

    const std::string save1 = SceneSerializer::SaveToString(scene);
    CHECK(save1.find("MysteryComponent") != std::string::npos);
    CHECK(save1.find("customField") != std::string::npos);

    // Re-load and re-save: the opaque data must still be byte-identical.
    Scene scene2;
    REQUIRE(SceneSerializer::LoadFromString(scene2, save1));
    const std::string save2 = SceneSerializer::SaveToString(scene2);
    CHECK(save1 == save2);
}

TEST_CASE("E21: Save rotates the previous file to a single .bak (crash-safe)")
{
    namespace fs = std::filesystem;

    auto readFile = [](const fs::path& p)
    {
        std::ifstream is(p, std::ios::binary);
        std::stringstream ss; ss << is.rdbuf();
        return ss.str();
    };

    const fs::path path = fs::temp_directory_path() / "cosmic_bak_test.cscene";
    const fs::path bak  = fs::path(path).concat(".bak");
    std::error_code ec;
    fs::remove(path, ec);
    fs::remove(bak, ec);

    // First save: no prior file, so NO backup is produced.
    Scene s1;
    s1.CreateEntity("First");
    const std::string json1 = SceneSerializer::SaveToString(s1);
    REQUIRE(SceneSerializer::Save(s1, path.string()));
    CHECK(fs::exists(path));
    CHECK_FALSE(fs::exists(bak));

    // Second save over the same path: the previous file rotates to .bak, which
    // must hold the FIRST scene; the live file now holds the SECOND.
    Scene s2;
    s2.CreateEntity("Second");
    const std::string json2 = SceneSerializer::SaveToString(s2);
    REQUIRE(SceneSerializer::Save(s2, path.string()));
    REQUIRE(fs::exists(bak));
    CHECK(readFile(bak)  == json1);
    CHECK(readFile(path) == json2);

    // Third save keeps exactly ONE backup — .bak now holds the SECOND scene
    // (not the first), proving the rotation replaces rather than accumulates.
    Scene s3;
    s3.CreateEntity("Third");
    const std::string json3 = SceneSerializer::SaveToString(s3);
    REQUIRE(SceneSerializer::Save(s3, path.string()));
    CHECK(readFile(bak)  == json2);
    CHECK(readFile(path) == json3);

    fs::remove(path, ec);
    fs::remove(bak, ec);
}

TEST_CASE("E14: a prefab subtree instantiates with fresh UUIDs and preserved hierarchy")
{
    namespace fs = std::filesystem;

    Scene scene;
    Entity parent = scene.CreateEntity("Rover");
    parent.GetComponent<TransformComponent>().Position = { 1.0f, 0.0f, 0.0f };
    Entity wheelL = scene.CreateEntity("WheelL");
    Entity wheelR = scene.CreateEntity("WheelR");
    scene.SetParent(wheelL, parent, /*keepWorldPose=*/false);
    scene.SetParent(wheelR, parent, /*keepWorldPose=*/false);
    wheelL.GetComponent<TransformComponent>().Position = { -1.0f, 0.0f, 0.0f };

    const UUID origParent = parent.GetComponent<IDComponent>().ID;

    const std::string path = (fs::temp_directory_path() / "cosmic_prefab_test.cprefab").string();
    REQUIRE(SceneSerializer::SavePrefab(scene, parent, path));

    // Instantiate twice into the same scene.
    Entity a = SceneSerializer::InstantiatePrefab(scene, path);
    Entity b = SceneSerializer::InstantiatePrefab(scene, path);
    REQUIRE(a);
    REQUIRE(b);

    const UUID aID = a.GetComponent<IDComponent>().ID;
    const UUID bID = b.GetComponent<IDComponent>().ID;
    CHECK_FALSE(aID == origParent);   // fresh UUIDs, distinct from the source + each other
    CHECK_FALSE(bID == origParent);
    CHECK_FALSE(aID == bID);

    // Root identity + prefab link + transform survived.
    CHECK(a.GetComponent<TagComponent>().Tag == "Rover");
    REQUIRE(a.HasComponent<PrefabComponent>());
    CHECK(a.GetComponent<PrefabComponent>().SourcePath == path);
    CHECK(a.GetComponent<TransformComponent>().Position.x == doctest::Approx(1.0f));

    // Each clone got its own two children.
    REQUIRE(a.HasComponent<RelationshipComponent>());
    REQUIRE(b.HasComponent<RelationshipComponent>());
    CHECK(a.GetComponent<RelationshipComponent>().Children.size() == 2);
    CHECK(b.GetComponent<RelationshipComponent>().Children.size() == 2);

    // The first child's local transform (WheelL at x=-1) round-tripped, and its
    // UUID differs from the original scene's WheelL.
    const UUID childAid = a.GetComponent<RelationshipComponent>().Children[0];
    Entity childA = scene.FindByUUID(childAid);
    REQUIRE(childA);
    CHECK(childA.GetComponent<TransformComponent>().Position.x == doctest::Approx(-1.0f));
    CHECK_FALSE(childAid == wheelL.GetComponent<IDComponent>().ID);

    std::error_code ec;
    fs::remove(path, ec);
}
