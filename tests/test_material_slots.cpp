// test_material_slots.cpp — Phase 24 / M5: multi-material meshes. Headless (no
// GL): the serializer COMPAT GATE (empty MaterialPaths ⇒ byte-identical, no key
// written) + the slot round-trip. The submesh render split needs a GL context
// (Mesh::Create uploads) and is covered on-GPU + by GL-conformance; the import
// side (per-part MaterialIndex) is covered by test_meshimport.

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/SceneSerializer.h"

#include <string>

using namespace Cosmic;

TEST_SUITE("Material slots (M5)")
{
    TEST_CASE("empty MaterialPaths writes NO key (compat: byte-identical)")
    {
        Scene s;
        Entity e = s.CreateEntity("Box");
        auto& mr = e.AddComponent<MeshRendererComponent>();
        mr.MaterialPath = "project://materials/Legacy.cmat";   // the pre-M5 single slot
        CHECK(mr.MaterialPaths.empty());

        const std::string text = SceneSerializer::SaveToString(s);
        // The legacy single MaterialPath still serializes (reflected field)…
        CHECK(text.find("MaterialPath") != std::string::npos);
        // …but the M5 array is absent when the vector is empty — an existing scene
        // is written exactly as before this feature.
        CHECK(text.find("MaterialPaths") == std::string::npos);
    }

    TEST_CASE("non-empty MaterialPaths serializes + round-trips")
    {
        Scene s;
        Entity e = s.CreateEntity("Gun");
        auto& mr = e.AddComponent<MeshRendererComponent>();
        mr.MeshPath = "project://models/gun.glb";
        mr.MaterialPaths = { "project://materials/Body.cmat",
                             "",                                   // an empty slot (falls back at draw)
                             "project://materials/Scope.cmat" };

        const std::string text = SceneSerializer::SaveToString(s);
        CHECK(text.find("MaterialPaths") != std::string::npos);
        CHECK(text.find("Body.cmat") != std::string::npos);
        CHECK(text.find("Scope.cmat") != std::string::npos);

        Scene s2;
        REQUIRE(SceneSerializer::LoadFromString(s2, text));
        bool found = false;
        for (auto ent : s2.GetRegistry().view<MeshRendererComponent>())
        {
            const auto& r = s2.GetRegistry().get<MeshRendererComponent>(ent);
            REQUIRE(r.MaterialPaths.size() == 3);
            CHECK(r.MaterialPaths[0] == "project://materials/Body.cmat");
            CHECK(r.MaterialPaths[1].empty());
            CHECK(r.MaterialPaths[2] == "project://materials/Scope.cmat");
            // Runtime resolution is lazy (needs the AssetLibrary/GL) — untouched here.
            CHECK(r.MaterialPathsResolved == false);
            found = true;
        }
        CHECK(found);
    }

    TEST_CASE("MeshData submesh table travels with the geometry (CPU side)")
    {
        // The pure MeshData half (Mesh::Create's GPU upload copies this into the
        // Mesh; that path needs GL). Two ranges over one index buffer, two slots.
        MeshData data;
        data.Submeshes.push_back({ 0,  36, 0 });
        data.Submeshes.push_back({ 36, 12, 1 });
        REQUIRE(data.Submeshes.size() == 2);
        CHECK(data.Submeshes[0].IndexOffset == 0);
        CHECK(data.Submeshes[0].IndexCount == 36);
        CHECK(data.Submeshes[0].MaterialIndex == 0);
        CHECK(data.Submeshes[1].IndexOffset == 36);
        CHECK(data.Submeshes[1].MaterialIndex == 1);
    }
}
