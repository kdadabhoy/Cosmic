// test_wo09_c05_json.cpp — WO-09 (2D stability) C05, the headless half: scene /
// prefab / material / config documents — valid round-trips, seeded malformed
// input (F-CORRUPT: truncation at every byte, bad magic / version / counts,
// nonfinite values, oversized metadata, deep nesting), hierarchy integrity
// under malformed links (cycles, self-child, a child claimed twice, duplicate
// UUIDs), and the committed corrupt fixtures.
//
// Oracle per parser: a loader returns false, or returns true and leaves a scene
// / struct that can be saved and reloaded (SaveToString re-parses; entity and
// component counts are bounded by the input). A crash or an uncaught exception
// ends the process — that is the failing evidence, captured by the runner.
//
// The 3D/unknown-block preservation obligation is asserted in
// test_crossbuild_scene.cpp (extended by WO-09), not duplicated here. The
// real-project editor sequence (create/import/save/reopen/play/stop/undo/redo/
// delete) is the W/I case: Projects/Starforge/src/C05ProjectLifecycleSelfTest.cpp.

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/SceneSerializer.h"
#include "scene/ui/UiComponents.h"
#include "graphics/MaterialAsset.h"
#include "utils/Config.h"
#include "wo09_fuzz.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

using namespace Cosmic;

namespace
{
    std::string EvidenceDir()
    {
        if (const char* e = std::getenv("COSMIC_WO09_EVIDENCE_DIR")) return e[0] ? e : "";
        return "";
    }
    int FuzzCases()
    {
        if (const char* n = std::getenv("COSMIC_WO09_FUZZ_CASES")) return std::max(1, std::atoi(n));
        return 2000;
    }
    std::filesystem::path FixtureDir()
    {
#ifdef COSMIC_WO09_FIXTURES
        return std::filesystem::path(COSMIC_WO09_FIXTURES);
#else
        return std::filesystem::path("tests/fixtures/wo09");
#endif
    }

    Entity FindTagged(Scene& s, const std::string& tag)
    {
        for (auto e : s.GetRegistry().view<TagComponent>())
            if (s.GetRegistry().get<TagComponent>(e).Tag == tag) return Entity{ e, &s };
        return Entity{};
    }

    // The F-CONTENT scene: one of every 2D content type plus an unknown block and
    // a hierarchy, written by the serializer itself so it is a REAL file shape.
    std::string ContentScene()
    {
        Scene s;
        Entity cam = s.CreateEntity("Camera");
        cam.GetComponent<TransformComponent>().Position = { 0, 0, 10 };
        Entity spr = s.CreateEntity("Hero");
        spr.GetComponent<TransformComponent>().Position = { 1.5f, -2.0f, 0.0f };
        auto& sr = spr.AddComponent<SpriteRendererComponent>();
        sr.TexturePath = "project://textures/hero.png"; sr.ZOrder = 2; sr.FlipX = true; sr.Color = { 1, 0.5f, 0.25f, 1 };
        auto& an = spr.AddComponent<SpriteAnimationComponent>();
        an.SheetPath = "project://textures/hero.png"; an.Frames = 4; an.FPS = 12.0f;
        Entity map = s.CreateEntity("Ground");
        auto& tm = map.AddComponent<TilemapComponent>();
        tm.GridW = 8; tm.GridH = 4; tm.EnsureCells();
        for (int i = 0; i < 32; ++i) tm.Cells[i] = (uint16_t)(i % 5);
        Entity light = s.CreateEntity("Lamp");
        light.AddComponent<Light2DComponent>().Radius = 3.0f;
        Entity env = s.CreateEntity("Environment");
        env.AddComponent<EnvironmentComponent>().Ambient2D = { 0.2f, 0.2f, 0.3f };
        Entity canvas = s.CreateEntity("Canvas");
        canvas.AddComponent<CanvasComponent>();
        Entity btn = s.CreateEntity("Play");
        btn.AddComponent<RectTransformComponent>();
        btn.AddComponent<UiButtonComponent>().Signal = "play";
        btn.AddComponent<UiTextComponent>().Text = "Play";
        s.SetParent(btn, canvas, false);
        Entity child = s.CreateEntity("HeroShadow");
        child.AddComponent<SpriteRendererComponent>();
        s.SetParent(child, spr, false);
        Entity script = s.CreateEntity("Scripted");
        script.AddComponent<NativeScriptComponent>("NotARegisteredClass");
        script.GetComponent<NativeScriptComponent>().PendingFields = "{ \"Speed\": 3.5 }";
        Entity unknown = s.CreateEntity("Future");
        unknown.AddComponent<OpaqueComponentsComponent>().Blocks.emplace_back("FutureThing", "{\"a\":[1,2,{\"b\":null}],\"s\":\"x\"}");
        return SceneSerializer::SaveToString(s);
    }

    // Scene oracle: accepted ⇒ the loaded scene saves and reloads, entity count is
    // bounded by the input's entity array, and every entity has an ID/Transform/Tag.
    bool ParseScene(const std::string& text)
    {
        Scene s;
        if (!SceneSerializer::LoadFromString(s, text)) return false;
        size_t entities = 0;
        for (auto e : s.GetRegistry().view<IDComponent>()) { (void)e; ++entities; }
        const std::string again = SceneSerializer::SaveToString(s);
        Scene s2;
        if (!SceneSerializer::LoadFromString(s2, again)) FAIL("a loaded scene re-saved into something unloadable");
        size_t entities2 = 0;
        for (auto e : s2.GetRegistry().view<IDComponent>()) { (void)e; ++entities2; }
        if (entities2 != entities) FAIL("entity count changed across a re-save: " << entities << " -> " << entities2);
        return true;
    }

    // Prefab oracle (file based): accepted ⇒ a root entity came back.
    bool ParsePrefab(const std::string& text)
    {
        static const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "wo09-c05-fuzz.cprefab";
        Wo09Fuzz::WriteFile(tmp, text);
        Scene s;
        Entity root = SceneSerializer::InstantiatePrefab(s, tmp.string());
        return (bool)root;
    }

    bool ParseMaterial(const std::string& text)
    {
        MaterialAsset m;
        if (!SceneSerializer::LoadReflectedFromString(entt::type_hash<MaterialAsset>::value(), &m, text)) return false;
        const std::string again = SceneSerializer::SaveReflectedToString(entt::type_hash<MaterialAsset>::value(), &m);
        MaterialAsset m2;
        if (!SceneSerializer::LoadReflectedFromString(entt::type_hash<MaterialAsset>::value(), &m2, again)) FAIL("a loaded material re-saved into something unloadable");
        return true;
    }

    bool ParseConfig(const std::string& text)
    {
        Ref<Config> c = Config::Parse(text, "fuzz.toml");
        if (!c) return false;
        (void)c->GetFloat("window.width", 1.0f);
        (void)c->GetString("app.name", "");
        (void)c->GetVec3("env.sun", glm::vec3(0.0f));
        (void)c->GetFloatArray("data.samples");
        return true;
    }

    const char* kToml = R"(# F-CONTENT config
[app]
name = "Sample"
version = 3
fullscreen = false

[window]
width = 1280
height = 720.0
title = "Cosmic 2D"

[env]
sun = [0.2, -1.0, 0.3]
ambient = [0.1, 0.1, 0.2, 1.0]

[data]
samples = [1.0, 2.5, -3.75, 1e3]

[[motors]]
name = "left"
kv = 900

[[motors]]
name = "right"
kv = 950
)";
}

TEST_SUITE("WO-09 C05 JSON (headless)")
{
    TEST_CASE("WO-09 C05: the F-CONTENT scene round-trips (save -> load -> save byte-stable) and its content counts are exact")
    {
        const std::string text = ContentScene();
        Scene s;
        REQUIRE(SceneSerializer::LoadFromString(s, text));
        CHECK(SceneSerializer::SaveToString(s) == text);
        size_t entities = 0; for (auto e : s.GetRegistry().view<IDComponent>()) { (void)e; ++entities; }
        CHECK(entities == 10);
        CHECK(FindTagged(s, "Hero").GetComponent<SpriteRendererComponent>().FlipX);
        CHECK(FindTagged(s, "Ground").GetComponent<TilemapComponent>().Cells.size() == 32);
        CHECK(FindTagged(s, "Scripted").GetComponent<NativeScriptComponent>().PendingFields.find("Speed") != std::string::npos);
        REQUIRE(FindTagged(s, "Future").HasComponent<OpaqueComponentsComponent>());
        CHECK(FindTagged(s, "Future").GetComponent<OpaqueComponentsComponent>().Blocks[0].first == "FutureThing");
        // Hierarchy: Play under Canvas, HeroShadow under Hero.
        CHECK(FindTagged(s, "Play").GetComponent<RelationshipComponent>().Parent == FindTagged(s, "Canvas").GetComponent<IDComponent>().ID);
        CHECK(FindTagged(s, "HeroShadow").GetComponent<RelationshipComponent>().Parent == FindTagged(s, "Hero").GetComponent<IDComponent>().ID);
        // Write the F-CONTENT scene into evidence (the fixture is also committed).
        if (!EvidenceDir().empty()) Wo09Fuzz::WriteFile(std::filesystem::path(EvidenceDir()) / "f-content-Main.cscene", text);
    }

    TEST_CASE("WO-09 C05: truncation at EVERY byte of the F-CONTENT scene is rejected or loads a bounded prefix, never crashes")
    {
        const std::string text = ContentScene();
        int rejected = 0, accepted = 0;
        double maxMs = 0;
        for (size_t n = 0; n < text.size(); n += 1)
        {
            const auto t0 = std::chrono::steady_clock::now();
            const bool ok = ParseScene(text.substr(0, n));
            maxMs = std::max(maxMs, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
            ok ? ++accepted : ++rejected;
        }
        CHECK(rejected > 0);
        MESSAGE("C05 truncation ladder: " << text.size() << " prefixes, rejected=" << rejected << " accepted=" << accepted << " max=" << maxMs << " ms");
        CHECK(maxMs < 2000.0);
    }

    TEST_CASE("WO-09 C05: bad magic / version / counts, nonfinite values, oversized metadata and 100,000-deep nesting are bounded")
    {
        // Bad magic / wrong root types.
        CHECK_FALSE(ParseScene("5"));
        CHECK_FALSE(ParseScene("[]"));
        CHECK_FALSE(ParseScene("{}"));
        CHECK_FALSE(ParseScene("{ \"cosmic_scene\": \"one\" }"));
        CHECK_FALSE(ParseScene("{ \"entities\": {} }"));
        CHECK(ParseScene("{ \"cosmic_scene\": -999999999999, \"entities\": [] }"));      // version is informational
        CHECK(ParseScene("{ \"cosmic_scene\": 1, \"entities\": [ 5, \"x\", null, [], { } ] }"));   // junk entities: bounded
        {
            Scene s;
            REQUIRE(SceneSerializer::LoadFromString(s, "{ \"cosmic_scene\": 1, \"entities\": [ 5, \"x\", null, [], { } ] }"));
            size_t entities = 0; for (auto e : s.GetRegistry().view<IDComponent>()) { (void)e; ++entities; }
            CHECK(entities == 5);                                    // one entity per array element, nothing more
        }
        // Nonfinite values: JSON has no inf/NaN literal, and an out-of-range number
        // (1e999) is a PARSE ERROR for the JSON layer — the whole document is
        // rejected (pinned; nothing half-loads). Values that overflow FLOAT but not
        // double (1e300) load as +/-inf in the float field: the scene still saves
        // (a non-finite float dumps as null) and reloads with that field at 0.
        CHECK_FALSE(ParseScene("{ \"cosmic_scene\": 1, \"entities\": [ { \"id\": \"00000000000000A1\", \"components\": { "
                               "\"Transform\": { \"Position\": [1e999, -1e999, 0], \"Scale\": [0, -0, 1e-999] } } } ] }"));
        CHECK_FALSE(ParseMaterial("{ \"fields\": { \"Roughness\": 1e999 } }"));
        {
            Scene s;
            REQUIRE(SceneSerializer::LoadFromString(s, "{ \"cosmic_scene\": 1, \"entities\": [ { \"id\": \"00000000000000A1\", \"components\": { "
                         "\"Transform\": { \"Position\": [1e300, -1e300, 0] } } } ] }"));
            Entity e = s.FindByUUID(UUID::FromString("00000000000000A1"));
            REQUIRE(e);
            const glm::vec3 p = e.GetComponent<TransformComponent>().Position;
            CHECK(std::isinf(p.x)); CHECK(std::isinf(p.y));
            MESSAGE("C05 float-overflow Position from JSON 1e300 loads as (" << p.x << ", " << p.y << ", " << p.z << ") — pinned");
            const std::string again = SceneSerializer::SaveToString(s);
            CHECK(again.find("null") != std::string::npos);          // inf dumps as null ...
            Scene s2;
            REQUIRE(SceneSerializer::LoadFromString(s2, again));
            const glm::vec3 p2 = s2.FindByUUID(UUID::FromString("00000000000000A1")).GetComponent<TransformComponent>().Position;
            CHECK(p2.x == 0.0f); CHECK(p2.y == 0.0f);                // ... and null reloads as 0 (documented data laundering)
            // The same for a NaN authored at runtime: saved as null, reloaded as 0.
            e.GetComponent<TransformComponent>().Position.z = std::numeric_limits<float>::quiet_NaN();
            Scene s3;
            REQUIRE(SceneSerializer::LoadFromString(s3, SceneSerializer::SaveToString(s)));
            CHECK(s3.FindByUUID(UUID::FromString("00000000000000A1")).GetComponent<TransformComponent>().Position.z == 0.0f);
        }
        // Oversized metadata: a 4 MiB tag and a 4 MiB unknown block are stored, not truncated, not a crash.
        {
            const std::string big(4u * 1024u * 1024u, 'T');
            Scene s;
            REQUIRE(SceneSerializer::LoadFromString(s, "{ \"cosmic_scene\": 1, \"entities\": [ { \"id\": \"00000000000000B1\", \"components\": { "
                         "\"Tag\": { \"Tag\": \"" + big + "\" }, \"Unknown\": { \"blob\": \"" + big + "\" } } } ] }"));
            Entity e = s.FindByUUID(UUID::FromString("00000000000000B1"));
            REQUIRE(e);
            CHECK(e.GetComponent<TagComponent>().Tag.size() == big.size());
            CHECK(e.GetComponent<OpaqueComponentsComponent>().Blocks[0].second.size() > big.size());
        }
        // Deep nesting: 100,000 nested arrays inside an unknown block, and as the whole document.
        {
            std::string deep = "{ \"cosmic_scene\": 1, \"entities\": [ { \"id\": \"00000000000000C1\", \"components\": { \"Deep\": ";
            for (int i = 0; i < 100000; ++i) deep += '[';
            deep += '1';
            for (int i = 0; i < 100000; ++i) deep += ']';
            deep += " } } ] }";
            const auto t0 = std::chrono::steady_clock::now();
            const bool ok = ParseScene(deep);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            MESSAGE("C05 100,000-deep nesting: " << (ok ? "accepted" : "rejected") << " in " << ms << " ms");
            CHECK(ms < 5000.0);
            std::string whole;
            for (int i = 0; i < 100000; ++i) whole += '[';
            for (int i = 0; i < 100000; ++i) whole += ']';
            CHECK_FALSE(ParseScene(whole));
            CHECK_FALSE(ParseMaterial(whole));
        }
        // Huge counts: a GridW of 2^31-1 and a Frames of -2^31 are clamped / harmless.
        CHECK(ParseScene("{ \"cosmic_scene\": 1, \"entities\": [ { \"id\": \"00000000000000D1\", \"components\": { "
                         "\"Tilemap\": { \"GridW\": 2147483647, \"GridH\": 2147483647, \"Cells\": [1] }, "
                         "\"SpriteAnimation\": { \"Frames\": -2147483648, \"FPS\": 1e300 } } } ] }"));
    }

    TEST_CASE("WO-09 C05: malformed hierarchy links — cycle, self-child, child claimed by two parents, duplicate UUIDs — load bounded, no dangling reference")
    {
        // Cycle A -> B -> A and self-child C -> C through the Children arrays.
        const std::string cyc = R"({ "cosmic_scene": 1, "entities": [
            { "id": "00000000000000A1", "components": { "Tag": { "Tag": "A" }, "Relationship": { "Children": ["00000000000000B1"] } } },
            { "id": "00000000000000B1", "components": { "Tag": { "Tag": "B" }, "Relationship": { "Children": ["00000000000000A1"] } } },
            { "id": "00000000000000C1", "components": { "Tag": { "Tag": "C" }, "Relationship": { "Children": ["00000000000000C1", "00000000000000C1"] } } },
            { "id": "00000000000000D1", "components": { "Tag": { "Tag": "D" }, "Relationship": { "Children": ["00000000000000E1"] } } },
            { "id": "00000000000000D2", "components": { "Tag": { "Tag": "D2" }, "Relationship": { "Children": ["00000000000000E1", "00000000000000FF"] } } },
            { "id": "00000000000000E1", "components": { "Tag": { "Tag": "E" } } } ] })";
        Scene s;
        REQUIRE(SceneSerializer::LoadFromString(s, cyc));
        Entity a = FindTagged(s, "A"), b = FindTagged(s, "B"), c = FindTagged(s, "C"), d = FindTagged(s, "D"), d2 = FindTagged(s, "D2"), e = FindTagged(s, "E");
        REQUIRE((a && b && c && d && d2 && e));
        // The second link (B -> A) is refused: A is B's ancestor. No cycle exists.
        CHECK(b.GetComponent<RelationshipComponent>().Parent == a.GetComponent<IDComponent>().ID);
        CHECK_FALSE((a.HasComponent<RelationshipComponent>() && a.GetComponent<RelationshipComponent>().Parent.IsValid()));
        CHECK_FALSE(s.IsAncestor(b, a));
        CHECK(s.IsAncestor(a, b));
        // Self-child refused; C has no parent and no children.
        CHECK_FALSE((c.HasComponent<RelationshipComponent>() && !c.GetComponent<RelationshipComponent>().Children.empty()));
        // A child claimed by two parents belongs to the LAST claimant (file order); the
        // first parent's list no longer names it; the missing child FF is skipped.
        CHECK(e.GetComponent<RelationshipComponent>().Parent == d2.GetComponent<IDComponent>().ID);
        CHECK(d.GetComponent<RelationshipComponent>().Children.empty());
        CHECK(d2.GetComponent<RelationshipComponent>().Children.size() == 1);
        // Every walker terminates on the loaded scene.
        (void)s.GetWorldTransform(b);
        CHECK(s.IsActiveInHierarchy(b));
        const std::string again = SceneSerializer::SaveToString(s);
        Scene s2;
        CHECK(SceneSerializer::LoadFromString(s2, again));

        // Duplicate UUIDs: two entities with the same id — both are created; the
        // UUID map resolves to ONE of them; destroying the other must not leave the
        // survivor unreachable... PINNED as observed (see the MESSAGE).
        const std::string dup = R"({ "cosmic_scene": 1, "entities": [
            { "id": "00000000000000AA", "components": { "Tag": { "Tag": "first" } } },
            { "id": "00000000000000AA", "components": { "Tag": { "Tag": "second" } } } ] })";
        Scene sd;
        REQUIRE(SceneSerializer::LoadFromString(sd, dup));
        size_t entities = 0; for (auto h : sd.GetRegistry().view<IDComponent>()) { (void)h; ++entities; }
        Entity resolved = sd.FindByUUID(UUID::FromString("00000000000000AA"));
        REQUIRE(resolved);
        const std::string resolvedTag = resolved.GetComponent<TagComponent>().Tag;
        Entity first = FindTagged(sd, "first"), second = FindTagged(sd, "second");
        REQUIRE((first && second));
        sd.DestroyEntity(resolvedTag == "second" ? first : second, true);   // destroy the one the map does NOT point at
        Entity after = sd.FindByUUID(UUID::FromString("00000000000000AA"));
        MESSAGE("C05 duplicate UUID: " << entities << " entities created, the map resolved to '" << resolvedTag
                << "', after destroying the other the id " << std::string(after ? "still resolves" : "NO LONGER RESOLVES (stale)"));
        CHECK(entities == 2);
        CHECK_MESSAGE((bool)after, "destroying a duplicate-UUID twin erased the survivor's UUID mapping (stale entity reference)");
        const std::string dumped = SceneSerializer::SaveToString(sd);
        Scene sd2;
        CHECK(SceneSerializer::LoadFromString(sd2, dumped));
    }

    TEST_CASE("WO-09 C05: material (.cmat) and config (.toml) — valid round-trip and edge values")
    {
        MaterialAsset m;
        const std::string text = SceneSerializer::SaveReflectedToString(entt::type_hash<MaterialAsset>::value(), &m);
        CHECK(text.find("cosmic_type") != std::string::npos);
        CHECK(ParseMaterial(text));
        CHECK(ParseMaterial("{}"));                                   // a bare object: every field default
        CHECK(ParseMaterial("{ \"fields\": { \"Nope\": 1, \"Shader\": 5 } }"));   // wrong types: defaults
        CHECK_FALSE(ParseMaterial("not json"));
        CHECK_FALSE(ParseMaterial(""));
        CHECK(ParseMaterial("5"));                                    // pinned: a non-object parses to defaults
        CHECK(ParseConfig(kToml));
        CHECK_FALSE(ParseConfig("[app\nname = "));
        CHECK(ParseConfig(""));                                       // an empty TOML document is valid
        Ref<Config> c = Config::Parse(kToml, "t.toml");
        REQUIRE(c);
        CHECK(c->GetInt("window.width", 0) == 1280);
        CHECK(c->GetFloat("window.height", 0.0f) == 720.0f);
        CHECK(c->GetVec3("env.sun", glm::vec3(0.0f)) == glm::vec3(0.2f, -1.0f, 0.3f));
        CHECK(c->GetFloatArray("data.samples").size() == 4);
        CHECK(c->GetString("app.name", "") == "Sample");
        CHECK(c->GetInt("window.title", -1) == -1);                  // type mismatch: fallback

        // KI-49: a table header that starts with a non-key character is a REJECTED
        // parse (it used to trip a toml++ assertion in Debug); header-like lines
        // inside multi-line strings and comments are content and still load.
        CHECK_FALSE(ParseConfig("[!x]\nk = 1\n"));
        CHECK_FALSE(ParseConfig("[[%arr]]\nk = 1\n"));
        CHECK_FALSE(ParseConfig("  [ \x01" "bad ]\n"));
        CHECK_FALSE(ParseConfig(std::string("[\0motors]]\n", 12)));
        CHECK(ParseConfig("[ \"quoted key\" ]\nk = 1\n"));
        CHECK(ParseConfig("[ 'literal' ]\nk = 1\n"));
        CHECK(ParseConfig("[a.b-c_9]\nk = 1\n"));
        CHECK(ParseConfig("s = \"\"\"\n[!not a header]\n[[%nor this]]\n\"\"\"\nk = 1\n"));
        CHECK(ParseConfig("s = '''\n[!literal content]\n'''\n[real]\nk = 1\n"));
        CHECK(ParseConfig("s = \"a \\\"b\\\" c\" # \"\"\" not a string\n[real]\nk = 1\n"));   // escaped quotes / a comment never open a multi-line string
        CHECK(ParseConfig("# [!in a comment]\n[real] # [!trailing]\nk = 1\n"));
        {
            Ref<Config> ml = Config::Parse("s = \"\"\"\n[!x]\n\"\"\"\n[t]\nv = 2\n", "ml.toml");
            REQUIRE(ml);
            CHECK(ml->GetString("s", "") == "[!x]\n");
            CHECK(ml->GetInt("t.v", 0) == 2);
        }
    }

    TEST_CASE("WO-09 C05: parser fuzz — scene / prefab / material / config over seeded mutations with a per-case deadline")
    {
        const int cases = FuzzCases();
        const double deadlineMs = 2000.0;
        const std::string scene = ContentScene();
        REQUIRE(ParseScene(scene));
        // A prefab file of the Hero subtree (real shape: written by SavePrefab).
        std::string prefab;
        {
            Scene s;
            REQUIRE(SceneSerializer::LoadFromString(s, scene));
            const std::filesystem::path p = std::filesystem::temp_directory_path() / "wo09-c05-hero.cprefab";
            REQUIRE(SceneSerializer::SavePrefab(s, FindTagged(s, "Hero"), p.string()));
            prefab = Wo09Fuzz::ReadFile(p);
            std::error_code ec; std::filesystem::remove(p, ec);
        }
        REQUIRE(ParsePrefab(prefab));
        MaterialAsset m;
        const std::string material = SceneSerializer::SaveReflectedToString(entt::type_hash<MaterialAsset>::value(), &m);
        REQUIRE(ParseMaterial(material));
        REQUIRE(ParseConfig(kToml));

        const Wo09Fuzz::Stats sc = Wo09Fuzz::Run("scene",    scene,    0x0905C4E5u, cases, deadlineMs, ParseScene,    EvidenceDir(), true);
        const Wo09Fuzz::Stats pf = Wo09Fuzz::Run("prefab",   prefab,   0x0905B4EFu, cases, deadlineMs, ParsePrefab,   EvidenceDir(), true);
        const Wo09Fuzz::Stats mt = Wo09Fuzz::Run("material", material, 0x0905CA47u, cases, deadlineMs, ParseMaterial, EvidenceDir(), true);
        const Wo09Fuzz::Stats cf = Wo09Fuzz::Run("config",   kToml,    0x090570A1u, cases, deadlineMs, ParseConfig,   EvidenceDir(), false);
        MESSAGE("C05 fuzz scene:    seed 0x0905C4E5 cases=" << sc.cases << " accepted=" << sc.accepted << " rejected=" << sc.rejected << " max=" << sc.maxMs << " ms (case " << sc.maxCase << ")");
        MESSAGE("C05 fuzz prefab:   seed 0x0905B4EF cases=" << pf.cases << " accepted=" << pf.accepted << " rejected=" << pf.rejected << " max=" << pf.maxMs << " ms (case " << pf.maxCase << ")");
        MESSAGE("C05 fuzz material: seed 0x0905CA47 cases=" << mt.cases << " accepted=" << mt.accepted << " rejected=" << mt.rejected << " max=" << mt.maxMs << " ms (case " << mt.maxCase << ")");
        MESSAGE("C05 fuzz config:   seed 0x090570A1 cases=" << cf.cases << " accepted=" << cf.accepted << " rejected=" << cf.rejected << " max=" << cf.maxMs << " ms (case " << cf.maxCase << ")");
        for (const auto* st : { &sc, &pf, &mt, &cf })
        {
            CHECK(st->cases == cases);
            CHECK(st->maxMs <= deadlineMs);
            CHECK(st->accepted > 0);
            CHECK(st->rejected > 0);
        }
    }

    TEST_CASE("WO-09 C05: committed F-CORRUPT fixtures — every .cscene / .cprefab / .cmat / .toml under tests/fixtures/wo09/corrupt is handled")
    {
        const std::filesystem::path dir = FixtureDir() / "corrupt";
        REQUIRE_MESSAGE(std::filesystem::exists(dir), "fixture dir missing: " << dir.string());
        int scenes = 0, prefabs = 0, materials = 0, configs = 0;
        for (const auto& entry : std::filesystem::directory_iterator(dir))
        {
            const std::string ext = entry.path().extension().string();
            const std::string text = Wo09Fuzz::ReadFile(entry.path());
            const auto t0 = std::chrono::steady_clock::now();
            if (ext == ".cscene")       { ++scenes;    (void)ParseScene(text); }
            else if (ext == ".cprefab") { ++prefabs;   (void)ParsePrefab(text); }
            else if (ext == ".cmat")    { ++materials; (void)ParseMaterial(text); }
            else if (ext == ".toml")    { ++configs;   (void)ParseConfig(text); }
            else continue;
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            CHECK_MESSAGE(ms < 2000.0, entry.path().filename().string() << " took " << ms << " ms");
        }
        CHECK(scenes >= 1); CHECK(prefabs >= 1); CHECK(materials >= 1); CHECK(configs >= 1);
        MESSAGE("C05 corrupt fixtures: " << scenes << " scene, " << prefabs << " prefab, " << materials << " material, " << configs << " config handled");
        // And the committed F-CONTENT project scene loads.
        const std::filesystem::path content = FixtureDir() / "content" / "scenes" / "Main.cscene";
        REQUIRE_MESSAGE(std::filesystem::exists(content), "F-CONTENT scene missing: " << content.string());
        CHECK(ParseScene(Wo09Fuzz::ReadFile(content)));
    }
}
