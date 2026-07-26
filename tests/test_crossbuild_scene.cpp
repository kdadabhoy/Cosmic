// test_crossbuild_scene.cpp — W9 (plan doc 28 §9.6).
//
// The cross-build data-safety proof: a scene authored on the 3D engine must
// survive a trip through the 2D engine unharmed. That is what makes the split
// safe to ship — a user can open a 3D project in the 2D editor, save it, and
// reopen it in 3D with nothing lost.
//
// The mechanism is OpaqueComponentsComponent (Components.h:56): any component
// block whose name is unknown to THIS build is stashed as verbatim JSON text at
// load and re-emitted at save. On the 2D engine every 3D block takes that path.
//
// WHAT IS ASSERTED IN WHICH BUILD
//   both  — the round-trip is idempotent (save->load->save is byte-stable), and
//           a block no build knows survives verbatim.
//   2D    — every 3D block survives verbatim, key for key and value for value.
//   3D    — every 3D block is actually parsed into its real component, with the
//           authored values intact (verbatim is the WRONG assertion there: real
//           reflected components legitimately re-serialize their full field set).
//
// Byte-comparing the AUTHORED text against a saved dump would be wrong in both
// builds: nlohmann's objects are std::map-backed, so a dump always comes out
// key-sorted regardless of the order the fields were written in. Every
// comparison below is therefore either dump-vs-dump (both sorted, so stable) or
// a value lookup.

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#ifndef COSMIC_2D_ONLY
#include "scene/Components3D.h"
#endif
#include "scene/SceneSerializer.h"

#include <string>

using namespace Cosmic;

namespace
{
    // A scene carrying one entity per 3D subsystem the 2D build drops, plus a
    // component name NO build will ever know. Authored by hand (not produced by
    // the serializer) so it represents a real file written by the 3D editor.
    //
    // Deliberately messy key order — if anything here compared bytes against a
    // dump, this would catch it.
    const char* kAuthoredScene = R"JSON({
      "cosmic_scene": 1,
      "entities": [
        {
          "id": "00000000000000a1",
          "components": {
            "Tag": { "Tag": "MeshHolder" },
            "Transform": { "Position": [1.0, 2.0, 3.0] },
            "MeshRenderer": {
              "MeshPath": "assets/models/crate.obj",
              "MaterialPath": "assets/materials/crate.cmat",
              "CastShadows": false,
              "Color": [0.25, 0.5, 0.75, 1.0]
            }
          }
        },
        {
          "id": "00000000000000a2",
          "components": {
            "Tag": { "Tag": "Sun" },
            "DirectionalLight": {
              "Intensity": 3.5,
              "Direction": [-0.4, -1.0, -0.2],
              "Color": [1.0, 0.95, 0.85, 1.0]
            }
          }
        },
        {
          "id": "00000000000000a3",
          "components": {
            "Tag": { "Tag": "Ground" },
            "Terrain": {
              "WorldSize": 512.0,
              "Resolution": 257.0,
              "HeightScale": 80.0,
              "Seed": 1337.0,
              "Octaves": 6.0
            },
            "TotallyUnknownFutureComponent": {
              "Nested": { "Deep": [1, 2, 3] },
              "Flag": true,
              "Text": "survives every build"
            }
          }
        }
      ]
    })JSON";

    std::string RoundTrip(const std::string& text)
    {
        Scene scene;
        REQUIRE(SceneSerializer::LoadFromString(scene, text));
        return SceneSerializer::SaveToString(scene);
    }

    // SaveToString emits dump(2) — pretty-printed, two-space indent — and an
    // opaque block is re-parsed and re-dumped along with the rest of the tree, so
    // it comes back indented too. Strip whitespace that sits OUTSIDE string
    // literals so the needles below can be written compactly instead of pinning
    // an exact indentation depth that says nothing about correctness.
    std::string Squeeze(const std::string& text)
    {
        std::string out;
        out.reserve(text.size());

        bool inString = false;
        bool escaped  = false;
        for (char c : text)
        {
            if (inString)
            {
                out.push_back(c);
                if (escaped)        escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"')  inString = false;
                continue;
            }
            if (c == '"') { inString = true; out.push_back(c); continue; }
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
            out.push_back(c);
        }
        return out;
    }

    // Substring probe over the squeezed dump. Whitespace inside strings is
    // preserved, so needles like "survives every build" still work.
    bool Contains(const std::string& haystack, const std::string& needle)
    {
        return Squeeze(haystack).find(needle) != std::string::npos;
    }
}

// =============================================================================
// Build-independent: the round-trip itself
// =============================================================================

TEST_CASE("A 3D-authored scene round-trips and is idempotent")
{
    const std::string pass1 = RoundTrip(kAuthoredScene);
    const std::string pass2 = RoundTrip(pass1);

    // Dump vs dump: both key-sorted, so this comparison is legitimate and is the
    // strongest byte-level claim available. A component that failed to round-trip
    // would drift between the two passes.
    CHECK(pass1 == pass2);

    const std::string pass3 = RoundTrip(pass2);
    CHECK(pass2 == pass3);

    // The entities themselves survived, by UUID.
    CHECK(Contains(pass1, "00000000000000a1"));
    CHECK(Contains(pass1, "00000000000000a2"));
    CHECK(Contains(pass1, "00000000000000a3"));

    // And the shared components loaded as real components in both builds.
    CHECK(Contains(pass1, "MeshHolder"));
    CHECK(Contains(pass1, "\"Sun\""));
    CHECK(Contains(pass1, "Ground"));
}

TEST_CASE("A component name no build knows survives verbatim")
{
    // This is OpaqueComponentsComponent working identically in BOTH engines —
    // the 2D build's treatment of MeshRenderer is the same machinery.
    const std::string pass1 = RoundTrip(kAuthoredScene);

    CHECK(Contains(pass1, "TotallyUnknownFutureComponent"));
    CHECK(Contains(pass1, "survives every build"));
    CHECK(Contains(pass1, "\"Deep\":[1,2,3]"));
    CHECK(Contains(pass1, "\"Flag\":true"));

    // Nested structure, not a flattened or stringified blob.
    CHECK(Contains(pass1, "\"Nested\":{\"Deep\":[1,2,3]}"));

    const std::string pass2 = RoundTrip(pass1);
    CHECK(Contains(pass2, "\"Nested\":{\"Deep\":[1,2,3]}"));
    CHECK(Contains(pass2, "survives every build"));
}

TEST_CASE("Unknown blocks survive alongside known ones on the same entity")
{
    // Entity a3 carries BOTH a Terrain block and the unknown block. On the 2D
    // engine both are opaque; on the 3D engine one is real and one is opaque.
    // Either way neither may displace the other.
    const std::string pass1 = RoundTrip(kAuthoredScene);

    CHECK(Contains(pass1, "Terrain"));
    CHECK(Contains(pass1, "TotallyUnknownFutureComponent"));
    CHECK(Contains(pass1, "Ground"));
}

TEST_CASE("An empty scene and a scene of only unknown blocks both round-trip")
{
    // Degenerate inputs on the same path — an empty entity list must not fault,
    // and an entity that is nothing but opaque blocks must still be emitted.
    const char* emptyScene = R"JSON({"cosmic_scene":1,"entities":[]})JSON";
    const std::string emptyPass1 = RoundTrip(emptyScene);
    CHECK(emptyPass1 == RoundTrip(emptyPass1));

    const char* opaqueOnly = R"JSON({
      "cosmic_scene": 1,
      "entities": [
        { "id": "00000000000000b1",
          "components": {
            "FirstUnknown":  { "A": 1 },
            "SecondUnknown": { "B": [true, false] }
          } }
      ]
    })JSON";

    const std::string pass1 = RoundTrip(opaqueOnly);
    CHECK(Contains(pass1, "FirstUnknown"));
    CHECK(Contains(pass1, "SecondUnknown"));
    CHECK(Contains(pass1, "\"B\":[true,false]"));
    CHECK(pass1 == RoundTrip(pass1));
}

// =============================================================================
// 2D build: every 3D block must come back byte-for-byte
// =============================================================================

#ifdef COSMIC_2D_ONLY

TEST_CASE("2D engine: 3D component blocks are preserved verbatim")
{
    const std::string pass1 = RoundTrip(kAuthoredScene);

    // None of these types exist in this build, so each one went through
    // OpaqueComponentsComponent. Every authored key and value must reappear,
    // and nothing may be silently dropped or coerced.
    CHECK(Contains(pass1, "MeshRenderer"));
    CHECK(Contains(pass1, "assets/models/crate.obj"));
    CHECK(Contains(pass1, "assets/materials/crate.cmat"));
    CHECK(Contains(pass1, "\"CastShadows\":false"));

    CHECK(Contains(pass1, "DirectionalLight"));
    CHECK(Contains(pass1, "\"Intensity\":3.5"));

    CHECK(Contains(pass1, "Terrain"));
    CHECK(Contains(pass1, "\"WorldSize\":512.0"));
    CHECK(Contains(pass1, "\"Resolution\":257.0"));
    CHECK(Contains(pass1, "\"HeightScale\":80.0"));
    CHECK(Contains(pass1, "\"Seed\":1337.0"));

    // An opaque block is stored as the dump of what was parsed, so re-emitting
    // it is exact — including the nested arrays the 2D engine cannot interpret.
    CHECK(Contains(pass1, "\"Direction\":[-0.4,-1.0,-0.2]"));
    CHECK(Contains(pass1, "\"Color\":[0.25,0.5,0.75,1.0]"));
}

TEST_CASE("2D engine: 3D components are NOT resurrected as real components")
{
    // The other half of the guarantee — the 2D engine must not half-load a 3D
    // component. Preservation happens at the text level only.
    Scene scene;
    REQUIRE(SceneSerializer::LoadFromString(scene, kAuthoredScene));

    auto& reg = scene.GetRegistry();
    size_t opaqueCount = 0;
    for (auto handle : reg.view<OpaqueComponentsComponent>())
        opaqueCount += reg.get<OpaqueComponentsComponent>(handle).Blocks.size();

    // MeshRenderer, DirectionalLight, Terrain, TotallyUnknownFutureComponent.
    CHECK(opaqueCount == 4);
}

#else

// =============================================================================
// 3D build: every 3D block must load into its real component
// =============================================================================

TEST_CASE("3D engine: 3D component blocks load into real components")
{
    Scene scene;
    REQUIRE(SceneSerializer::LoadFromString(scene, kAuthoredScene));

    auto& reg = scene.GetRegistry();

    // Exactly one carrier of each, counted by hand so the assertion does not
    // depend on which entt view API version is in play.
    auto countOf = [&reg](auto typeTag) -> size_t
    {
        using T = decltype(typeTag);
        size_t n = 0;
        for (auto handle : reg.view<T>()) { (void)handle; ++n; }
        return n;
    };

    // MeshRenderer — asset paths and the non-default flag survived.
    {
        REQUIRE(countOf(MeshRendererComponent{}) == 1);
        auto view = reg.view<MeshRendererComponent>();
        const auto& mr = reg.get<MeshRendererComponent>(*view.begin());
        CHECK(mr.MeshPath     == "assets/models/crate.obj");
        CHECK(mr.MaterialPath == "assets/materials/crate.cmat");
        CHECK(mr.CastShadows  == false);
    }

    // DirectionalLight — a scalar the 2D build can only carry as text.
    {
        REQUIRE(countOf(DirectionalLightComponent{}) == 1);
        auto view = reg.view<DirectionalLightComponent>();
        const auto& dl = reg.get<DirectionalLightComponent>(*view.begin());
        CHECK(dl.Intensity == doctest::Approx(3.5f));
    }

    // Terrain — the recipe fields.
    {
        REQUIRE(countOf(TerrainComponent{}) == 1);
        auto view = reg.view<TerrainComponent>();
        const auto& t = reg.get<TerrainComponent>(*view.begin());
        CHECK(t.WorldSize   == doctest::Approx(512.0f));
        CHECK(t.HeightScale == doctest::Approx(80.0f));
    }

    // Only the genuinely unknown block went opaque here.
    size_t opaqueCount = 0;
    for (auto handle : reg.view<OpaqueComponentsComponent>())
        opaqueCount += reg.get<OpaqueComponentsComponent>(handle).Blocks.size();
    CHECK(opaqueCount == 1);
}

TEST_CASE("3D engine: re-saving preserves the authored 3D values")
{
    const std::string pass1 = RoundTrip(kAuthoredScene);

    // Values, not formatting — a real component re-serializes its whole field
    // set, so the block is a superset of what was authored.
    CHECK(Contains(pass1, "assets/models/crate.obj"));
    CHECK(Contains(pass1, "assets/materials/crate.cmat"));
    CHECK(Contains(pass1, "\"CastShadows\":false"));
    CHECK(Contains(pass1, "\"Intensity\":3.5"));
    CHECK(Contains(pass1, "\"WorldSize\":512.0"));
    CHECK(Contains(pass1, "\"HeightScale\":80.0"));

    // And the unknown block rode through untouched next to them.
    CHECK(Contains(pass1, "survives every build"));
}

#endif
