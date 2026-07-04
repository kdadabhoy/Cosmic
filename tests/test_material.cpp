// test_material.cpp — E17: the .cmat MaterialAsset round-trips through the generic
// reflected-struct serializer (the same visitor that powers .cscene). Headless —
// building a live Ref<Material> needs GL and is exercised in the editor.

#include "doctest.h"

#include "graphics/MaterialAsset.h"
#include "scene/SceneSerializer.h"
#include "reflect/TypeRegistry.h"

#include <entt/entt.hpp>
#include <string>

using namespace Cosmic;

TEST_SUITE("MaterialAsset / reflected serializer (E17)")
{
    TEST_CASE(".cmat round-trips every field")
    {
        MaterialAsset a;
        a.Albedo      = { 0.2f, 0.4f, 0.8f, 1.0f };
        a.Metallic    = 0.9f;
        a.Roughness   = 0.15f;
        a.AO          = 0.5f;
        a.Emissive    = { 1.0f, 0.2f, 0.0f };
        a.Transparent = true;
        a.AlbedoMap   = "project://textures/rust_albedo.png";
        a.NormalMap   = "project://textures/rust_n.png";

        const uint32_t tid = entt::type_hash<MaterialAsset>::value();

        const std::string json = SceneSerializer::SaveReflectedToString(tid, &a);
        CHECK(json.find("Albedo") != std::string::npos);
        CHECK(json.find("cosmic_type") != std::string::npos);

        MaterialAsset b;
        REQUIRE(SceneSerializer::LoadReflectedFromString(tid, &b, json));

        CHECK(b.Albedo.r == doctest::Approx(0.2f));
        CHECK(b.Albedo.b == doctest::Approx(0.8f));
        CHECK(b.Metallic == doctest::Approx(0.9f));
        CHECK(b.Roughness == doctest::Approx(0.15f));
        CHECK(b.AO == doctest::Approx(0.5f));
        CHECK(b.Emissive.r == doctest::Approx(1.0f));
        CHECK(b.Transparent == true);
        CHECK(b.AlbedoMap == "project://textures/rust_albedo.png");
        CHECK(b.NormalMap == "project://textures/rust_n.png");
        CHECK(b.MetalRoughMap.empty());
    }

    TEST_CASE("Unknown type id is a safe no-op")
    {
        MaterialAsset a;
        CHECK(SceneSerializer::SaveReflectedToString(0xC0FFEEu, &a) == "{}");
        CHECK_FALSE(SceneSerializer::LoadReflectedFromString(0xC0FFEEu, &a, "{}"));
    }

    TEST_CASE("Bare-object form (no fields wrapper) still loads")
    {
        const uint32_t tid = entt::type_hash<MaterialAsset>::value();
        MaterialAsset b;
        REQUIRE(SceneSerializer::LoadReflectedFromString(tid, &b, R"({"Metallic":0.33})"));
        CHECK(b.Metallic == doctest::Approx(0.33f));
    }
}
