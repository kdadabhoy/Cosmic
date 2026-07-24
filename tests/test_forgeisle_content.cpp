// test_forgeisle_content.cpp — Phase 28 / Z1 gated content smoke (doc 27).
//
// Validates the ForgeIsle project's AUTHORED content headlessly, so a broken
// scene/flow/terrain edit fails CI before anyone boots the game:
//   * every scene parses and carries its contract entities/components,
//   * Main.cflow validates, references real scene files, declares the Z-series
//     variables,
//   * the greybox route is walkable over the EXACT terrain the recipe builds
//     (Cosmic::BuildTerrainSpec -> Terrain::Create -> SampleHeight — the J7
//     CPU-sampling contract), and every blockout site sits sanely on it.
//
// GATED: skips (with a message) when the ForgeIsle project folder is absent —
// the COSMIC_FOX_GLB precedent. The project lives in-repo at Projects/ForgeIsle,
// so it always runs for this tree; external checkouts of the engine alone skip.

#include "doctest.h"

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/ui/UiComponents.h"
#include "scene/SceneSerializer.h"
#include "scene/FlowMachine.h"
#include "scene/WorldSystemRecipes.h"
#include "terrain/Terrain.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Cosmic;

namespace
{
    // Walk up from the CWD until Projects/ForgeIsle/project.cproj appears
    // (works from the repo root AND from build/Runtime/<CONFIG>).
    std::optional<fs::path> FindForgeIsleRoot()
    {
        fs::path p = fs::current_path();
        for (int i = 0; i < 8; ++i)
        {
            const fs::path candidate = p / "Projects" / "ForgeIsle";
            if (fs::exists(candidate / "project.cproj"))
                return candidate;
            if (!p.has_parent_path() || p.parent_path() == p)
                break;
            p = p.parent_path();
        }
        return std::nullopt;
    }

    bool LoadScene(Scene& s, const fs::path& file)
    {
        return SceneSerializer::Load(s, file.generic_string());
    }

    Entity FindByTag(Scene& s, const std::string& tag)
    {
        auto& reg = s.GetRegistry();
        for (auto e : reg.view<TagComponent>())
            if (reg.get<TagComponent>(e).Tag == tag)
                return Entity(e, &s);
        return {};
    }

    int CountWith(Scene& s, const char* tagPrefix)
    {
        int n = 0;
        auto& reg = s.GetRegistry();
        for (auto e : reg.view<TagComponent>())
            if (reg.get<TagComponent>(e).Tag.rfind(tagPrefix, 0) == 0)
                ++n;
        return n;
    }
}

TEST_CASE("ForgeIsle Z1: scenes parse and carry their contract entities")
{
    auto root = FindForgeIsleRoot();
    if (!root) { MESSAGE("ForgeIsle project not present - skipping"); return; }

    // ---- Title ----------------------------------------------------------
    {
        Scene s;
        REQUIRE(LoadScene(s, *root / "scenes" / "Title.cscene"));

        Entity cam = FindByTag(s, "Vista Camera");
        REQUIRE(cam);
        CHECK(cam.GetComponent<CameraComponent>().Primary);

        Entity start = FindByTag(s, "Start Button");
        REQUIRE(start);
        CHECK(start.GetComponent<UiButtonComponent>().Signal == "start");
        CHECK(start.HasComponent<UiTextComponent>());
        CHECK(start.HasComponent<RectTransformComponent>());

        Entity quit = FindByTag(s, "Quit Button");
        REQUIRE(quit);
        CHECK(quit.GetComponent<UiButtonComponent>().Signal == "quit");

        Entity ocean = FindByTag(s, "Ocean");
        REQUIRE(ocean);
        CHECK(ocean.GetComponent<WaterComponent>().UseRecipe);
        CHECK(ocean.GetComponent<WaterComponent>().Preset == WaterPreset::Ocean);

        Entity env = FindByTag(s, "Environment");
        REQUIRE(env);
        CHECK((int)env.GetComponent<EnvironmentComponent>().Sky == 3);   // Physical (X1)
    }

    // ---- Pause ----------------------------------------------------------
    {
        Scene s;
        REQUIRE(LoadScene(s, *root / "scenes" / "Pause.cscene"));
        REQUIRE(FindByTag(s, "Resume Button"));
        CHECK(FindByTag(s, "Resume Button").GetComponent<UiButtonComponent>().Signal == "resume");
        CHECK(FindByTag(s, "Quit To Title Button").GetComponent<UiButtonComponent>().Signal == "quit_title");
        CHECK(FindByTag(s, "Pause Camera").GetComponent<CameraComponent>().Primary);
    }

    // ---- Island ---------------------------------------------------------
    {
        Scene s;
        REQUIRE(LoadScene(s, *root / "scenes" / "Island.cscene"));

        Entity terrain = FindByTag(s, "Island Terrain");
        REQUIRE(terrain);
        const auto& tc = terrain.GetComponent<TerrainComponent>();
        CHECK(tc.UseRecipe);
        CHECK(tc.EdgeFalloff > 0.0f);                       // an island, not a tile
        CHECK(terrain.HasComponent<TerrainColliderComponent>());   // J7 — walkable

        Entity ocean = FindByTag(s, "Ocean");
        REQUIRE(ocean);
        CHECK(ocean.GetComponent<WaterComponent>().Preset == WaterPreset::Ocean);

        Entity player = FindByTag(s, "Player");
        REQUIRE(player);
        CHECK(player.GetComponent<CharacterControllerComponent>().StepHeight
              == doctest::Approx(0.5f));
        CHECK(player.GetComponent<NativeScriptComponent>().ClassName == "PlayerController");

        Entity cam = FindByTag(s, "PlayerCamera");
        REQUIRE(cam);
        CHECK(cam.GetComponent<CameraComponent>().Primary);
        // ...and it must be the player's CHILD (the PlayerController contract).
        {
            bool isChild = false;
            auto& reg = s.GetRegistry();
            if (auto* rel = reg.try_get<RelationshipComponent>(player))
                for (const UUID& c : rel->Children)
                    if (Entity e = s.FindByUUID(c); e && e == cam)
                        isChild = true;
            CHECK(isChild);
        }

        // The greybox sites + interactables.
        CHECK(CountWith(s, "Beacon 1") >= 2);
        CHECK(CountWith(s, "Beacon 2") >= 2);
        CHECK(CountWith(s, "Beacon 3") >= 2);
        CHECK(CountWith(s, "Ruin ") >= 4);
        CHECK(CountWith(s, "Canyon ") >= 2);
        CHECK(CountWith(s, "Quarry ") >= 4);
        CHECK(CountWith(s, "Great Forge") >= 2);

        // J-series teaser: the ruin crates are DYNAMIC bodies.
        Entity crate = FindByTag(s, "Ruin Crate A");
        REQUIRE(crate);
        CHECK(crate.GetComponent<RigidBodyComponent>().Motion == MotionType::Dynamic);
        CHECK(crate.HasComponent<BoxColliderComponent>());

        // HUD present.
        REQUIRE(FindByTag(s, "HUD"));
        CHECK(FindByTag(s, "HUD Hint").HasComponent<UiTextComponent>());
    }
}

TEST_CASE("ForgeIsle Z1: Main.cflow validates and references real content")
{
    auto root = FindForgeIsleRoot();
    if (!root) { MESSAGE("ForgeIsle project not present - skipping"); return; }

    // Read the flow file directly (no VFS mount needed for a content check).
    FlowAsset flow;
    std::string err;
    {
        std::ifstream is(*root / "flows" / "Main.cflow", std::ios::binary);
        REQUIRE(is.good());
        std::stringstream ss; ss << is.rdbuf();
        REQUIRE_MESSAGE(FlowAsset::LoadFromString(flow, ss.str(), &err), err);
    }

    CHECK(flow.Validate().empty());
    CHECK(flow.Start == "Title");
    REQUIRE(flow.States.size() == 3);

    // Every state's scene path resolves to a real file under the project root.
    for (const FlowState& st : flow.States)
    {
        REQUIRE_FALSE(st.Scene.empty());
        const std::string prefix = "project://";
        REQUIRE(st.Scene.rfind(prefix, 0) == 0);
        const fs::path rel = st.Scene.substr(prefix.size());
        CHECK_MESSAGE(fs::exists(*root / rel), st.Scene, " missing on disk");
    }

    // The pause overlay is a PUSH with its own scene (design doc §7.1).
    const FlowState* island = flow.Find("Island");
    REQUIRE(island);
    bool pausePush = false;
    for (const auto& t : island->Transitions)
        if (t.On == "key:Escape" && t.To == "Pause" && t.Push)
            pausePush = true;
    CHECK(pausePush);

    // Z-series quest variables are declared (Q2).
    auto hasVar = [&](const char* n) {
        for (const auto& v : flow.Variables) if (v.Name == n) return true;
        return false;
    };
    CHECK(hasVar("MetHermit"));
    CHECK(hasVar("BeaconsLit"));
    CHECK(hasVar("TentWins"));
}

TEST_CASE("ForgeIsle Z1: the greybox route is walkable on the real terrain")
{
    auto root = FindForgeIsleRoot();
    if (!root) { MESSAGE("ForgeIsle project not present - skipping"); return; }

    Scene s;
    REQUIRE(LoadScene(s, *root / "scenes" / "Island.cscene"));
    Entity terrainE = FindByTag(s, "Island Terrain");
    REQUIRE(terrainE);

    // Build the EXACT terrain the recipe builds (the SyncWorldSystems mapping).
    const TerrainSpecification spec = BuildTerrainSpec(terrainE.GetComponent<TerrainComponent>());
    Ref<Terrain> terrain = Terrain::Create(spec);
    REQUIRE(terrain);

    struct Anchor { const char* Name; float X, Z, MinH, MaxH; };
    // Height windows around the measured seed-20260714 values — the blockout Ys
    // in Island.cscene are seated on these. A recipe edit that shifts any site
    // >0.8 m fails here: re-seat BOTH the scene and this table together.
    const Anchor anchors[] = {
        { "Spawn beach",         0.0f,  205.0f, 0.3f, 2.5f },   // measured 0.58
        { "Camp",               55.0f,  150.0f, 5.0f, 6.6f },   // measured 5.96
        { "Ruin ramp foot",   -126.0f,   62.0f, 3.4f, 5.0f },   // measured 4.20
        { "Ruin plaza",       -150.0f,   55.0f, 5.6f, 7.4f },   // measured 6.59
        { "Canyon south",        0.0f,  -55.0f, 5.8f, 7.6f },   // measured 6.78
        { "Canyon north",      -28.0f, -170.0f, 4.6f, 6.5f },   // measured 5.66
        { "Beacon 2 ramp foot",-30.0f, -176.0f, 2.0f, 5.5f },
        { "Beacon 2 site",     -30.0f, -192.0f, 0.5f, 2.5f },   // measured 1.51
        { "Quarry",            150.0f,  -40.0f, 3.0f, 4.7f },   // measured 3.84
        { "Summit ramp foot",    0.0f,    3.8f, 8.0f, 9.6f },   // measured 8.93
        { "Summit",              0.0f,  -20.0f, 7.0f, 8.9f },   // measured 7.94
    };

    // Always print the table — it is the re-seating tool for blockout Ys.
    for (const Anchor& a : anchors)
    {
        const float h = terrain->SampleHeight(a.X, a.Z);
        MESSAGE("terrain(", std::string(a.Name), " @ ", a.X, ",", a.Z, ") = ", h);
        CHECK_MESSAGE(h >= a.MinH, std::string(a.Name), ": terrain ", h, " below window min ", a.MinH);
        CHECK_MESSAGE(h <= a.MaxH, std::string(a.Name), ": terrain ", h, " above window max ", a.MaxH);
    }

    // Route legs stay on land and under a walkable grade (MaxSlopeDeg 55 with
    // ramps for anything steeper; the greybox bar is a 4 m rise <= 3.2 m).
    struct Leg { const char* Name; float X0, Z0, X1, Z1; };
    const Leg legs[] = {
        { "Beach->Camp",        0.0f,  205.0f,   55.0f,  150.0f },
        { "Camp->Ruin",        55.0f,  150.0f, -126.0f,   62.0f },
        { "Ruin->CanyonS",   -126.0f,   62.0f,    0.0f,  -55.0f },
        { "Canyon corridor",    0.0f,  -55.0f,  -28.0f, -170.0f },
        { "CanyonN->Quarry",  -28.0f, -170.0f,  150.0f,  -40.0f },
        { "Quarry->Summit",   150.0f,  -40.0f,    0.0f,  -20.0f },
    };
    for (const Leg& l : legs)
    {
        const float dx = l.X1 - l.X0, dz = l.Z1 - l.Z0;
        const float len = std::sqrt(dx * dx + dz * dz);
        const int   steps = (int)(len / 4.0f);
        float prev = terrain->SampleHeight(l.X0, l.Z0);
        float minH = prev, maxRise = 0.0f;
        for (int i = 1; i <= steps; ++i)
        {
            const float t = (float)i / (float)steps;
            const float h = terrain->SampleHeight(l.X0 + dx * t, l.Z0 + dz * t);
            maxRise = std::max(maxRise, std::abs(h - prev));
            minH    = std::min(minH, h);
            prev    = h;
        }
        MESSAGE("leg ", std::string(l.Name), ": minH=", minH, " maxRise/4m=", maxRise);
        CHECK_MESSAGE(minH > -0.6f, std::string(l.Name), ": route dips underwater (", minH, ")");
        CHECK_MESSAGE(maxRise < 3.2f, std::string(l.Name), ": grade too steep (", maxRise, " per 4 m)");
    }
}
