// test_components3d_registry.cpp — the component-registration baseline
// (Phase 29 W2 / §9.3). Written NOW, against the single un-split
// scene/Components.h + reflect/TypeRegistry.cpp, so W4 can prove nothing changed.
//
// W4 splits Components.h into Components.h + Components3D.h and
// TypeRegistry.cpp into TypeRegistry.cpp + TypeRegistry3D.cpp. The
// highest-severity failure modes of that split are all SILENT:
//
//   * a component stops being registered      -> it vanishes from saved scenes
//   * a component is registered twice         -> duplicated fields
//   * it lands under a different NAME         -> old scenes stop loading it
//   * its CS_REGISTER_COMPONENT line is lost  -> entt::type_hash falls back to the
//                                                compiler-generated hash, and every
//                                                already-saved scene mis-keys
//   * a field is dropped in the move          -> that field stops round-tripping
//
// Nothing crashes in any of those cases, so this suite is the tripwire. Both
// halves are pinned: the entt type hash (a stable FNV-1a of the fully-qualified
// type name, thanks to the CS_REGISTER_COMPONENT specialisation — so the literal
// baseline is the type-name STRING, which is readable and toolchain-independent)
// and the reflected field list.

#include <doctest.h>

#include "reflect/TypeRegistry.h"
#include "scene/Components.h"
#include "scene/ComponentRegistry.h"
#include "scene/ui/UiComponents.h"
#include "graphics/MaterialAsset.h"

#include <entt/entt.hpp>
#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace Cosmic;
using namespace Cosmic::Reflect;

namespace
{
    // The committed baseline. `Name` is the registered/serialized name, `Category`
    // its Inspector grouping, and `TypeName` the exact string
    // CS_REGISTER_COMPONENT hashes into entt::type_hash — the identity a saved
    // scene is keyed by. `Moves3D` marks the rows W4 relocates to
    // Components3D.h / TypeRegistry3D.cpp.
    struct Row
    {
        const char* Name;
        const char* Category;
        const char* TypeName;
        bool        Moves3D;
    };

    const Row kBaseline[] = {
        // --- shared: stays in Components.h / TypeRegistry.cpp on both engines --
        { "Tag",                 "Core",      "Cosmic::TagComponent",                 false },
        { "Transform",           "Core",      "Cosmic::TransformComponent",           false },
        { "SpriteRenderer",      "Rendering", "Cosmic::SpriteRendererComponent",      false },
        { "Tilemap",             "Rendering", "Cosmic::TilemapComponent",             false },
        { "Light2D",             "Rendering", "Cosmic::Light2DComponent",             false },
        { "SpriteAnimation",     "Rendering", "Cosmic::SpriteAnimationComponent",     false },
        { "Camera",              "Rendering", "Cosmic::CameraComponent",              false },
        { "Environment",         "Rendering", "Cosmic::EnvironmentComponent",         false },
        { "NativeScript",        "Scripts",   "Cosmic::NativeScriptComponent",        false },
        { "SystemScript",        "Systems",   "Cosmic::SystemScriptComponent",        false },
        { "Prefab",              "Core",      "Cosmic::PrefabComponent",              false },
        { "Canvas",              "UI",        "Cosmic::CanvasComponent",              false },
        { "RectTransform",       "UI",        "Cosmic::RectTransformComponent",       false },
        { "UiImage",             "UI",        "Cosmic::UiImageComponent",             false },
        { "UiText",              "UI",        "Cosmic::UiTextComponent",              false },
        { "UiButton",            "UI",        "Cosmic::UiButtonComponent",            false },
        { "UiWorldAnchor",       "UI",        "Cosmic::UiWorldAnchorComponent",       false },
        { "Material",            "Material",  "Cosmic::MaterialAsset",                false },
        // --- physics: shared, because Jolt ships on both (decision 3) ---------
        { "RigidBody",           "Physics",   "Cosmic::RigidBodyComponent",           false },
        { "BoxCollider",         "Physics",   "Cosmic::BoxColliderComponent",         false },
        { "SphereCollider",      "Physics",   "Cosmic::SphereColliderComponent",      false },
        { "CapsuleCollider",     "Physics",   "Cosmic::CapsuleColliderComponent",     false },
        { "MeshCollider",        "Physics",   "Cosmic::MeshColliderComponent",        false },
        { "CharacterController", "Physics",   "Cosmic::CharacterControllerComponent", false },
        // --- 3D-only: the W4 movers -------------------------------------------
        { "MeshRenderer",        "Rendering", "Cosmic::MeshRendererComponent",        true  },
        { "Animator",            "Rendering", "Cosmic::AnimatorComponent",            true  },
        { "Socket",              "Rendering", "Cosmic::SocketComponent",              true  },
        { "PrimitiveMesh",       "Rendering", "Cosmic::PrimitiveMeshComponent",       true  },
        { "LODGroup",            "Rendering", "Cosmic::LODGroupComponent",            true  },
        { "DirectionalLight",    "Lighting",  "Cosmic::DirectionalLightComponent",    true  },
        { "PointLight",          "Lighting",  "Cosmic::PointLightComponent",          true  },
        { "Terrain",             "World",     "Cosmic::TerrainComponent",             true  },
        { "Water",               "World",     "Cosmic::WaterComponent",               true  },
        { "ParticleEmitter",     "World",     "Cosmic::ParticleEmitterComponent",     true  },
        { "VoxelVolume",         "World",     "Cosmic::VoxelVolumeComponent",         true  },
        { "TerrainCollider",     "Physics",   "Cosmic::TerrainColliderComponent",     true  },
        { "NavMesh",             "Navigation","Cosmic::NavMeshComponent",             true  },
        { "NavAgent",            "Navigation","Cosmic::NavAgentComponent",            true  },
    };

    constexpr size_t kBaselineCount = sizeof(kBaseline) / sizeof(kBaseline[0]);

    // A private registry populated by exactly one RegisterEngineTypes call.
    //
    // Using this instead of the process-wide GetRegistry() for the field-list
    // assertions is load-bearing: TypeRegistry::GetOrCreate does NOT clear
    // Fields, so ClassIn APPENDS on re-registration. Any suite in this binary
    // that re-registers a type would otherwise change what this one sees. Leaked
    // on purpose, exactly like the engine's singleton, so no teardown ordering
    // can touch the std::function members.
    const TypeRegistry& Baseline()
    {
        static TypeRegistry* r = [] {
            auto* p = new TypeRegistry();
            RegisterEngineTypes(*p);
            return p;
        }();
        return *r;
    }

    // Assert that `name` resolves to a descriptor bound to the C++ type T.
    template<typename T>
    void CheckBinding(const char* name)
    {
        const TypeRegistry& r = Baseline();
        const TypeDescriptor* byName = r.FindByName(name);
        REQUIRE_MESSAGE(byName != nullptr, "not registered by name: ", name);
        const TypeDescriptor* byType = r.Find<T>();
        REQUIRE_MESSAGE(byType != nullptr, "not registered by type: ", name);

        // The name and the C++ type must be two routes to the SAME descriptor.
        CHECK(byName == byType);
        CHECK(byName->TypeId == entt::type_hash<T>::value());
        CHECK(byName->Name == name);

        // The entt glue must be wired — a descriptor whose Add/Has/Get went
        // missing in the split deserializes as a silent no-op.
        CHECK(byName->Add    != nullptr);
        CHECK(byName->Has    != nullptr);
        CHECK(byName->Get    != nullptr);
        CHECK(byName->Remove != nullptr);
        CHECK(byName->Copy   != nullptr);
    }

    std::vector<std::string> FieldNames(const char* type)
    {
        std::vector<std::string> out;
        const TypeDescriptor* d = Baseline().FindByName(type);
        if (!d) return out;
        for (const auto& f : d->Fields)
            out.push_back(f.Name);
        return out;
    }

    void CheckFields(const char* type, const std::vector<std::string>& expected)
    {
        REQUIRE_MESSAGE(Baseline().FindByName(type) != nullptr, "not registered: ", type);
        const std::vector<std::string> actual = FieldNames(type);
        REQUIRE_MESSAGE(actual.size() == expected.size(), type, ": field COUNT drifted");
        for (size_t i = 0; i < expected.size(); ++i)
            CHECK_MESSAGE(actual[i] == expected[i], type, " field ", i, ": expected ",
                          expected[i], ", got ", actual[i]);
    }
}

TEST_SUITE("W4 baseline: engine component registration")
{
    TEST_CASE("every baselined component is registered exactly once, under its own name")
    {
        const TypeRegistry& r = Baseline();

        std::set<std::string>   names;
        std::set<entt::id_type> ids;
        for (const Row& row : kBaseline)
        {
            const TypeDescriptor* d = r.FindByName(row.Name);
            REQUIRE_MESSAGE(d != nullptr, "not registered: ", row.Name);
            CHECK(d->Category == row.Category);
            CHECK(d->Name == row.Name);

            CHECK(names.insert(row.Name).second);   // no duplicate baseline rows
            CHECK(ids.insert(d->TypeId).second);    // no two names share a type
        }
        CHECK(names.size() == kBaselineCount);
        CHECK(ids.size()   == kBaselineCount);
        CHECK(kBaselineCount == 38u);
    }

    TEST_CASE("the type hash is the pinned FNV-1a of the qualified type name")
    {
        // CS_REGISTER_COMPONENT specialises entt::type_hash<T> to
        // hashed_string::value("Cosmic::XComponent"), which is what makes the id
        // stable across DLL boundaries AND across compilers. Losing that line
        // during the move to Components3D.h silently re-keys every saved scene,
        // and nothing else in the suite would notice.
        const TypeRegistry& r = Baseline();
        for (const Row& row : kBaseline)
        {
            const TypeDescriptor* d = r.FindByName(row.Name);
            REQUIRE_MESSAGE(d != nullptr, "not registered: ", row.Name);
            CHECK_MESSAGE(d->TypeId == entt::hashed_string::value(row.TypeName),
                          row.Name, ": type hash is not the pinned hash of ", row.TypeName);
        }
    }

    TEST_CASE("the registry holds no ENGINE type beyond the baseline")
    {
        // The other half of the guard: W4 must not quietly introduce a new
        // registration or leave a stale one behind.
        std::set<std::string> baseline;
        for (const Row& row : kBaseline)
            baseline.insert(row.Name);

        std::vector<std::string> unexpected;
        for (const auto& [id, desc] : Baseline().Types())
            if (!baseline.count(desc.Name))
                unexpected.push_back(desc.Name);

        std::sort(unexpected.begin(), unexpected.end());
        for (const std::string& n : unexpected)
            MESSAGE("unexpected registered engine type: ", n);
        CHECK(unexpected.empty());
        CHECK(Baseline().Types().size() == kBaselineCount);
    }

    TEST_CASE("the PROCESS-WIDE registry agrees with the baseline")
    {
        // GetRegistry() is what the SceneSerializer and the Inspector actually
        // read. It is shared with every other suite in this binary, so only the
        // engine rows are checked — a test-only type registered elsewhere is
        // filed under the "Test" category and ignored.
        const TypeRegistry& live = GetRegistry();
        for (const Row& row : kBaseline)
        {
            const TypeDescriptor* d = live.FindByName(row.Name);
            REQUIRE_MESSAGE(d != nullptr, "not registered process-wide: ", row.Name);
            CHECK(d->Category == row.Category);
            CHECK(d->TypeId == entt::hashed_string::value(row.TypeName));
        }

        std::set<std::string> baseline;
        for (const Row& row : kBaseline)
            baseline.insert(row.Name);
        for (const auto& [id, desc] : live.Types())
            if (desc.Category != "Test")
                CHECK_MESSAGE(baseline.count(desc.Name) == 1u,
                              "unexpected engine type in the live registry: ", desc.Name);
    }

    TEST_CASE("every name binds to the C++ type it claims")
    {
        CheckBinding<TagComponent>                ("Tag");
        CheckBinding<TransformComponent>          ("Transform");
        CheckBinding<SpriteRendererComponent>     ("SpriteRenderer");
        CheckBinding<TilemapComponent>            ("Tilemap");
        CheckBinding<Light2DComponent>            ("Light2D");
        CheckBinding<SpriteAnimationComponent>    ("SpriteAnimation");
        CheckBinding<CameraComponent>             ("Camera");
        CheckBinding<EnvironmentComponent>        ("Environment");
        CheckBinding<NativeScriptComponent>       ("NativeScript");
        CheckBinding<SystemScriptComponent>       ("SystemScript");
        CheckBinding<PrefabComponent>             ("Prefab");
        CheckBinding<CanvasComponent>             ("Canvas");
        CheckBinding<RectTransformComponent>      ("RectTransform");
        CheckBinding<UiImageComponent>            ("UiImage");
        CheckBinding<UiTextComponent>             ("UiText");
        CheckBinding<UiButtonComponent>           ("UiButton");
        CheckBinding<UiWorldAnchorComponent>      ("UiWorldAnchor");
        CheckBinding<MaterialAsset>               ("Material");

        CheckBinding<RigidBodyComponent>          ("RigidBody");
        CheckBinding<BoxColliderComponent>        ("BoxCollider");
        CheckBinding<SphereColliderComponent>     ("SphereCollider");
        CheckBinding<CapsuleColliderComponent>    ("CapsuleCollider");
        CheckBinding<MeshColliderComponent>       ("MeshCollider");
        CheckBinding<CharacterControllerComponent>("CharacterController");

        // The W4 movers.
        CheckBinding<MeshRendererComponent>       ("MeshRenderer");
        CheckBinding<AnimatorComponent>           ("Animator");
        CheckBinding<SocketComponent>             ("Socket");
        CheckBinding<PrimitiveMeshComponent>      ("PrimitiveMesh");
        CheckBinding<LODGroupComponent>           ("LODGroup");
        CheckBinding<DirectionalLightComponent>   ("DirectionalLight");
        CheckBinding<PointLightComponent>         ("PointLight");
        CheckBinding<TerrainComponent>            ("Terrain");
        CheckBinding<WaterComponent>              ("Water");
        CheckBinding<ParticleEmitterComponent>    ("ParticleEmitter");
        CheckBinding<VoxelVolumeComponent>        ("VoxelVolume");
        CheckBinding<TerrainColliderComponent>    ("TerrainCollider");
        CheckBinding<NavMeshComponent>            ("NavMesh");
        CheckBinding<NavAgentComponent>           ("NavAgent");
    }

    TEST_CASE("re-registering introduces no new TYPE — but DOES append fields")
    {
        // W4 splits the registration into two functions. This pins what a repeat
        // call actually does today, on a throwaway registry so nothing global is
        // disturbed.
        //
        // The type set is stable: no duplicate, no drop, same ids and names. The
        // FIELD LISTS are not — TypeRegistry::GetOrCreate returns the existing
        // descriptor without clearing Fields, so ClassIn appends a second copy of
        // every field. Nothing calls RegisterEngineTypes twice today, but W4 is
        // about to turn one call into two, so the sharp edge is recorded here
        // rather than discovered by a duplicated Inspector row.
        TypeRegistry local;
        RegisterEngineTypes(local);

        const size_t typeCount = local.Types().size();
        std::vector<std::pair<std::string, size_t>> fieldCounts;
        for (const Row& row : kBaseline)
        {
            const TypeDescriptor* d = local.FindByName(row.Name);
            REQUIRE_MESSAGE(d != nullptr, "not registered: ", row.Name);
            fieldCounts.emplace_back(row.Name, d->Fields.size());
        }

        RegisterEngineTypes(local);

        CHECK(local.Types().size() == typeCount);     // no new / lost types
        for (const Row& row : kBaseline)
        {
            const TypeDescriptor* d = local.FindByName(row.Name);
            REQUIRE_MESSAGE(d != nullptr, "lost on re-register: ", row.Name);
            CHECK(d->Category == row.Category);
            CHECK(d->TypeId == entt::hashed_string::value(row.TypeName));
        }
        for (const auto& [name, count] : fieldCounts)
            CHECK(local.FindByName(name)->Fields.size() == count * 2u);   // fields append
    }

    TEST_CASE("a live registry round-trip works through every moving descriptor")
    {
        // Add / Has / Get / Copy / Remove exercised on a real registry, so a
        // descriptor whose entt glue is wired to the WRONG type (a copy-paste in
        // the split) fails here rather than at scene-load time on a user machine.
        entt::registry reg;
        const entt::entity e = reg.create();

        for (const Row& row : kBaseline)
        {
            if (!row.Moves3D) continue;
            const TypeDescriptor* d = Baseline().FindByName(row.Name);
            REQUIRE_MESSAGE(d != nullptr, "not registered: ", row.Name);

            CHECK_FALSE(d->Has(reg, e));
            void* comp = d->Add(reg, e);
            CHECK_MESSAGE(d->Has(reg, e), row.Name, ": Add did not create the component");
            CHECK(d->Get(reg, e) == comp);
            d->Remove(reg, e);
            CHECK_MESSAGE(!d->Has(reg, e), row.Name, ": Remove did not erase the component");
        }
    }
}

TEST_SUITE("W4 baseline: the moving components' field lists")
{
    // Only the components that MOVE are field-baselined here — the ones that stay
    // are covered by test_reflect / test_components / test_scene_components. A
    // field lost or renamed during the move stops round-tripping through the
    // SceneSerializer, which is exactly what these lists pin.

    TEST_CASE("MeshRenderer / Animator / Socket / PrimitiveMesh / LODGroup")
    {
        CheckFields("MeshRenderer",
            { "Color", "CastShadows", "MeshPath", "MaterialPath", "Enabled" });
        CheckFields("Animator",
            { "ClipPath", "Speed", "Loop", "Playing", "NormalizedTime" });
        CheckFields("Socket",
            { "Joint", "Position", "Rotation", "Scale" });
        CheckFields("PrimitiveMesh",
            { "ShapeType", "Size", "Radius", "Height", "TubeRadius", "Segments", "Rings" });
        CheckFields("LODGroup",
            { "Color", "CastShadows" });
    }

    TEST_CASE("DirectionalLight / PointLight")
    {
        CheckFields("DirectionalLight", { "Direction", "Color", "Intensity", "Enabled" });
        CheckFields("PointLight",       { "Color", "Intensity", "Radius", "Enabled" });
    }

    TEST_CASE("Terrain")
    {
        CheckFields("Terrain", {
            "UseRecipe", "WorldSize", "Resolution", "HeightScale", "BaseHeight", "Seed",
            "Octaves", "Frequency", "Lacunarity", "Gain", "EdgeFalloff", "HeightmapPath",
            "GrassColor", "RockColor", "SnowColor", "SandColor",
            "GrassTex", "RockTex", "SnowTex", "SandTex",
            "SnowHeight", "SnowBlend" });
    }

    TEST_CASE("Water")
    {
        CheckFields("Water", {
            "UseRecipe", "Preset", "Center", "Extent", "SurfaceHeight", "GridResolution",
            "Amplitude", "Choppiness", "ShallowColor", "DeepColor",
            "CausticStrength", "WhitecapStrength", "SparkleStrength", "Enabled" });
    }

    TEST_CASE("ParticleEmitter")
    {
        CheckFields("ParticleEmitter", {
            "UseRecipe", "MaxParticles", "SpawnRate", "Shape", "ShapeRadius", "ConeAngleDeg",
            "BoxExtents", "SpeedMin", "SpeedMax", "LifeMin", "LifeMax", "Gravity", "Drag",
            "Wind", "SizeStart", "SizeEnd", "ColorStart", "ColorEnd", "Blend", "Space",
            "TexturePath", "FlipbookTilesX", "FlipbookTilesY", "FlipbookFps", "FlipbookBlend",
            "SoftFadeDistance", "StretchByVelocity",
            "NoiseEnabled", "NoiseStrength", "NoiseFrequency", "NoiseOctaves",
            "BoundsExtents", "BoundsWrap", "Enabled" });
    }

    TEST_CASE("VoxelVolume")
    {
        CheckFields("VoxelVolume", {
            "PalettePath", "VolumePath", "VoxelSize", "ViewRadius", "Greedy", "GenEnabled",
            "Seed", "SurfaceLevel", "Amplitude", "Frequency", "Octaves", "Lacunarity",
            "Gain", "Ridged", "CaveThreshold", "CaveFrequency", "DirtDepth", "SandLevel",
            "GrassBlock", "DirtBlock", "StoneBlock", "SandBlock" });
    }

    TEST_CASE("TerrainCollider / NavMesh / NavAgent")
    {
        CheckFields("TerrainCollider", {});   // a tag component: no fields at all

        CheckFields("NavMesh", {
            "SidecarPath", "SourceMode", "AutoGenerate", "AlwaysRenderHelper",
            "CellSize", "CellHeight", "AgentRadius", "AgentHeight", "AgentMaxClimb",
            "AgentMaxSlope", "RegionMinSize", "RegionMergeSize", "EdgeMaxLen",
            "EdgeMaxError", "DetailSampleDist", "DetailSampleMaxError",
            "VertsPerPoly", "TileSize" });

        CheckFields("NavAgent", {
            "Radius", "Height", "MaxSpeed", "MaxAccel", "StoppingDistance", "AutoRepath" });
    }
}
