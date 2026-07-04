#pragma once
// Last Modified: 5/24/2026

#include "core/Core.h"
#include "core/UUID.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "scene/ComponentRegistry.h"
#include "reflect/TypeDescriptor.h"   // Reflect::FieldValue (NativeScriptComponent::Fields)
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Cosmic
{
    class ScriptableEntity;   // scripting/ScriptableEntity.h (E11) — NativeScriptComponent link

    /**
     * @brief Stable 64-bit identity (E2). Scene::CreateEntity emplaces one with
     * a fresh UUID; the SceneSerializer writes it as the per-entity "id" (not a
     * component block) and restores it on load so parent/EntityRef/prefab
     * references survive save/load. Not reflected — identity isn't user-editable.
     */
    struct COSMIC_API IDComponent
    {
        UUID ID;

        IDComponent() = default;
        IDComponent(const IDComponent&) = default;
        IDComponent(UUID id) : ID(id) {}
    };

    /**
     * @brief Verbatim store for component blocks whose type was NOT registered
     * when a scene loaded (E2). Preserved as (name -> serialized JSON text) and
     * re-emitted unchanged on save, so opening a scene in an editor build that
     * lacks some game module never silently drops that module's data. Internal:
     * not reflected, handled directly by the serializer.
     */
    struct COSMIC_API OpaqueComponentsComponent
    {
        std::vector<std::pair<std::string, std::string>> Blocks;

        OpaqueComponentsComponent() = default;
        OpaqueComponentsComponent(const OpaqueComponentsComponent&) = default;
    };

    /**
     * @brief Parent/child links for scene hierarchy (E3). UUID-based so they
     * survive serialization (no entt-handle staleness). Present ONLY on entities
     * that participate in a hierarchy — entities without it behave exactly as
     * before (the 2D path and every shipped flat scene are untouched). Children
     * order is authoritative and preserved by the serializer; Parent is the
     * back-link. Mutate through Scene::SetParent, never by hand (it keeps the two
     * sides consistent and refuses cycles). Structural, not reflected.
     */
    struct COSMIC_API RelationshipComponent
    {
        UUID              Parent{ 0 }; // 0 == no parent (a root). MUST default to
                                       // 0 — a bare UUID default-constructs RANDOM.
        std::vector<UUID> Children;    // ordered

        RelationshipComponent() = default;
        RelationshipComponent(const RelationshipComponent&) = default;
    };

    /**
     * @brief Provides every entity with an internal debug name identity tag.
     */
    struct COSMIC_API TagComponent
    {
        std::string Tag;

        TagComponent() = default;
        TagComponent(const TagComponent&) = default;
        TagComponent(const std::string& tag) : Tag(tag) {}
    };

    /**
     * @brief Spatial placement properties of an entity within world-space.
     */
    struct COSMIC_API TransformComponent
    {
        glm::vec3 Position{ 0.0f, 0.0f, 0.0f };
        glm::vec3 Rotation{ 0.0f, 0.0f, 0.0f };       // Euler DEGREES (X, Y, Z); Z is 2D roll
        glm::vec3 Scale{ 1.0f, 1.0f, 1.0f };          // per-axis scale (was vec2 pre-S4.3)

        // Optional quaternion rotation for full 3D use (S4.3). When UseQuatRotation
        // is true, GetTransform() uses RotationQuat instead of the Euler product;
        // Euler degrees remain the default and drive the entire 2D path.
        //
        // CONVERSION POLICY: the two representations are INDEPENDENT — writing one
        // does NOT sync the other. Pick one per entity. Euler<->quat helpers are
        // deferred until an editor needs them (S14).
        glm::quat RotationQuat{ 1.0f, 0.0f, 0.0f, 0.0f }; // identity (w, x, y, z)
        bool      UseQuatRotation = false;

        TransformComponent() = default;
        TransformComponent(const TransformComponent&) = default;
        TransformComponent(const glm::vec3& position) : Position(position) {}

        /**
         * @brief Standard matrix transformation construction convenience helper.
         *
         * NOTE: In Euler mode GetTransform() applies all three Euler angles (X, Y, Z).
         * However, Scene::OnRender only passes Rotation.z to DrawRotatedQuad —
         * Rotation.x and Rotation.y are reserved for 3D use and are intentionally
         * ignored by the 2D render path. Entities with non-zero X or Y rotation
         * therefore differ between GetTransform() and what OnRender draws.
         */
        glm::mat4 GetTransform() const
        {
            const glm::mat4 rotation = UseQuatRotation
                ? glm::mat4_cast(RotationQuat)
                : glm::rotate(glm::mat4(1.0f), glm::radians(Rotation.x), { 1, 0, 0 })
                    * glm::rotate(glm::mat4(1.0f), glm::radians(Rotation.y), { 0, 1, 0 })
                    * glm::rotate(glm::mat4(1.0f), glm::radians(Rotation.z), { 0, 0, 1 });

            return glm::translate(glm::mat4(1.0f), Position)
                * rotation
                * glm::scale(glm::mat4(1.0f), Scale);
        }
    };


    /**
     * @brief Combines spatial layout data with your graphic engine's Material layers.
     */
    struct COSMIC_API SpriteRendererComponent
    {
        Ref<Material> ActiveMaterial;
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f }; // Solid default fallback color tint

        bool FlipX = false;
        bool FlipY = false;

        SpriteRendererComponent() = default;
        SpriteRendererComponent(const SpriteRendererComponent&) = default;
        SpriteRendererComponent(const Ref<Material>& material) : ActiveMaterial(material) {}
        SpriteRendererComponent(const glm::vec4& color) : Color(color) {}
    };


    /**
     * @brief 3D mesh renderer (S4.3). Attach with a TransformComponent to have
     * Scene::OnRender3D draw the mesh each frame.
     *
     * MaterialAsset null  → the Lambert color path (Renderer3D::DrawMesh + Color tint).
     * MaterialAsset set   → the custom-material path (Renderer3D::DrawMesh + Material).
     * MeshAsset null      → the entity is skipped.
     */
    struct COSMIC_API MeshRendererComponent
    {
        Ref<Mesh>     MeshAsset;             // entity skipped when null
        Ref<Material> MaterialAsset;         // null → Lambert color path
        glm::vec4     Color{ 1.0f };         // Lambert tint when MaterialAsset is null
        bool          CastShadows = true;    // consumed from S6.4; stored now so the ABI breaks once

        // Imported / loaded mesh reference (E16). When non-empty and MeshAsset is
        // null (e.g. a freshly loaded scene), Scene::SyncPrimitiveMeshes resolves it
        // through AssetLibrary::GetMesh (which routes non-glTF single-mesh formats
        // through MeshImport with their .cmeta). Reflected as an AssetPath("mesh")
        // slot so the Content Browser can drop onto it. Empty for primitives (their
        // mesh is built from params) and for meshes assigned directly in code.
        std::string   MeshPath;
        bool          MeshPathResolved = false;   // runtime-only; not reflected/serialized

        // Material asset reference (E17). When non-empty and MaterialAsset is null,
        // Scene::SyncPrimitiveMeshes resolves the `.cmat` through
        // AssetLibrary::GetMaterial. Reflected as an AssetPath("material") slot.
        // Empty -> the Lambert Color path (or whatever MaterialAsset was set in code).
        std::string   MaterialPath;
        bool          MaterialPathResolved = false;   // runtime-only; not reflected/serialized

        MeshRendererComponent() = default;
        MeshRendererComponent(const MeshRendererComponent&) = default;
        MeshRendererComponent(const Ref<Mesh>& mesh) : MeshAsset(mesh) {}
    };


    /**
     * @brief Parametric primitive (E15). A live-editable box/sphere/plane/cylinder/
     * cone/torus: the scene stores only the SHAPE + parameters (tiny, diffable
     * text), and Scene::SyncPrimitiveMeshes rebuilds the sibling
     * MeshRendererComponent's MeshAsset whenever the parameters change (or after a
     * load, when the mesh is null). Attach one alongside a MeshRendererComponent —
     * the create menus add both; the sync auto-adds a MeshRenderer if one is
     * missing. Editing a param in the Inspector is an ordinary reflected-field edit
     * (so it is undoable via E7); the rebuild is automatic.
     */
    struct COSMIC_API PrimitiveMeshComponent
    {
        enum class Shape { Box = 0, Sphere = 1, Plane = 2, Cylinder = 3, Cone = 4, Torus = 5 };

        Shape     ShapeType  = Shape::Box;
        glm::vec3 Size{ 1.0f, 1.0f, 1.0f };   // Box: full extents. Plane: X=width, Z=depth.
        float     Radius     = 0.5f;          // Sphere/Cylinder/Cone radius; Torus ring radius.
        float     Height     = 1.0f;          // Cylinder/Cone height.
        float     TubeRadius = 0.2f;          // Torus tube radius.
        int32_t   Segments   = 24;            // Radial / longitude subdivisions.
        int32_t   Rings      = 16;            // Sphere latitude bands / Torus tube sides.

        // Runtime-only (NOT reflected -> neither serialized nor shown in the
        // Inspector): a hash of the parameters the current MeshAsset was built
        // from. SyncPrimitiveMeshes rebuilds when this disagrees with the live
        // parameters, so any change (Inspector edit, undo, script, hand-edited
        // scene) regenerates the mesh with no explicit dirty-flag bookkeeping.
        std::size_t BuiltSignature = 0;

        PrimitiveMeshComponent() = default;
        PrimitiveMeshComponent(const PrimitiveMeshComponent&) = default;
        PrimitiveMeshComponent(Shape shape) : ShapeType(shape) {}
    };


    /**
     * @brief Distance-switched level-of-detail mesh set (S12.4). Attach with a
     * TransformComponent; Scene::OnRender3D draws ONE level per frame — the
     * first whose MaxDistance covers the camera-to-entity distance. Beyond the
     * last level the entity is not drawn at all (built-in distance cull).
     *
     * Levels are ordered nearest -> farthest (ascending MaxDistance); a level
     * with a null MeshAsset is skipped for that frame. All levels share the one
     * MaterialAsset/Color exactly like MeshRendererComponent. In the
     * SceneRenderer's shadow/coverage depth passes the SAME level is selected
     * (by the real camera distance), so an entity's caster always matches its
     * lit mesh. Cross-fade between levels is a documented follow-up (needs a
     * dither/alpha post path); switches are hard cuts.
     */
    struct COSMIC_API LODGroupComponent
    {
        struct Level
        {
            Ref<Mesh> MeshAsset;          // level skipped when null
            float     MaxDistance = 25.0f; // draw while cameraDistance <= this (meters)
        };

        std::vector<Level> Levels;         // nearest -> farthest
        Ref<Material>      MaterialAsset;  // null -> Lambert color path
        glm::vec4          Color{ 1.0f };  // Lambert tint when MaterialAsset is null
        bool               CastShadows = true;

        LODGroupComponent() = default;
        LODGroupComponent(const LODGroupComponent&) = default;

        /**
         * @brief Pure level selection (headless unit-tested): index of the first
         * level whose MaxDistance >= distance, or -1 when the distance is beyond
         * every level (distance-culled) or there are no levels.
         */
        static int SelectLevel(const std::vector<Level>& levels, float distance)
        {
            for (size_t i = 0; i < levels.size(); ++i)
                if (distance <= levels[i].MaxDistance)
                    return static_cast<int>(i);
            return -1;
        }
    };


    /**
     * @brief Directional (sun) light for lighting v1 (S4.5). Scene::OnRender3D
     * uses the FIRST directional light it finds as the sun.
     */
    struct COSMIC_API DirectionalLightComponent
    {
        glm::vec3 Direction{ -0.4f, -1.0f, -0.3f };  // direction the light TRAVELS
        glm::vec3 Color{ 1.0f };
        float     Intensity = 1.0f;

        DirectionalLightComponent() = default;
        DirectionalLightComponent(const DirectionalLightComponent&) = default;
    };

    /**
     * @brief Point light for lighting v1 (S4.5). World position comes from the
     * entity's TransformComponent; up to 16 are uploaded per frame.
     */
    struct COSMIC_API PointLightComponent
    {
        glm::vec3 Color{ 1.0f };
        float     Intensity = 1.0f;
        float     Radius    = 10.0f;

        PointLightComponent() = default;
        PointLightComponent(const PointLightComponent&) = default;
    };


    /**
     * @brief Scene camera (E4). Play mode renders from the first Primary camera
     * (falls back to the editor camera + a Console warning when none is Primary).
     * Position/orientation come from the entity's TransformComponent; this holds
     * the projection. GetProjection matches glm::perspective / glm::ortho.
     */
    struct COSMIC_API CameraComponent
    {
        enum class Projection { Perspective = 0, Orthographic = 1 };

        bool       Primary   = true;
        Projection ProjectionType = Projection::Perspective;
        float      FovDeg    = 60.0f;    // vertical FOV (perspective)
        float      Near      = 0.1f;
        float      Far       = 1000.0f;
        float      OrthoSize = 10.0f;    // half-height in world units (orthographic)

        CameraComponent() = default;
        CameraComponent(const CameraComponent&) = default;

        glm::mat4 GetProjection(float aspect) const
        {
            if (ProjectionType == Projection::Perspective)
                return glm::perspective(glm::radians(FovDeg), aspect, Near, Far);

            const float h = OrthoSize;
            const float w = OrthoSize * aspect;
            return glm::ortho(-w, w, -h, h, Near, Far);
        }
    };

    /**
     * @brief Scene-level rendering environment (E4). The editor keeps exactly one
     * entity (named "Environment") carrying this; SceneRenderer::ApplyEnvironment
     * maps it into a SceneRenderDesc each frame. EVERY field defaults to the
     * current SceneRenderer default (see SceneRendererSettings), so a scene with
     * NO EnvironmentComponent — every shipped app — renders exactly as before.
     * Frontier's programmatic setters are unaffected (it never calls
     * ApplyEnvironment). The Frontier DayNightCycle recipe is the reference for
     * TimeOfDay, but the fields here stay engine-generic.
     */
    struct COSMIC_API EnvironmentComponent
    {
        enum class SkyMode { Procedural = 0, Detailed = 1, HDRI = 2 };

        // Sun — drives the first DirectionalLight / the owned EnvironmentMap.
        glm::vec3 SunDirection{ -0.4f, -1.0f, -0.3f };  // direction the light TRAVELS
        glm::vec3 SunColor{ 1.0f };
        float     SunIntensity = 1.0f;

        // Sky + IBL.
        SkyMode     Sky = SkyMode::Procedural;
        std::string HdriPath;                 // used when Sky == HDRI
        float       TimeOfDay = 12.0f;        // hours 0..24 (sun scrub)
        bool        Skybox = true;            // == SceneRendererSettings::Skybox
        bool        IBL = true;               // == SceneRendererSettings::IBL
        float       IBLIntensity = 1.0f;
        float       Exposure = 1.0f;          // == SceneRenderDesc::Exposure

        // Height fog (== SceneRendererSettings fog defaults).
        bool      Fog = false;
        glm::vec3 FogColor{ 0.70f, 0.80f, 0.92f };
        float     FogDensity = 0.02f;
        float     FogHeightFalloff = 0.12f;
        float     FogBaseHeight = 0.0f;

        // Post (== SceneRendererSettings post defaults).
        bool  Bloom = false;
        float BloomThreshold = 1.0f;
        float BloomIntensity = 0.6f;
        bool  SSAO = false;
        float SsaoRadius = 0.5f;
        bool  FXAA = true;
        bool  LensFlare = false;
        float LensFlareIntensity = 0.35f;

        EnvironmentComponent() = default;
        EnvironmentComponent(const EnvironmentComponent&) = default;
    };


    class Terrain;
    class Water;
    class ParticleEmitter;

    /**
     * @brief Heightmap terrain (S8.1). Scene::OnRender3D renders it with
     * quadtree LOD around the pass camera. The Terrain asset itself is placed
     * by its own specification (world origin/size) — terrain is world geometry,
     * so the entity's TransformComponent is intentionally not applied.
     */
    struct COSMIC_API TerrainComponent
    {
        Ref<Terrain> TerrainAsset;           // entity skipped when null

        TerrainComponent() = default;
        TerrainComponent(const TerrainComponent&) = default;
    };

    /**
     * @brief Water surface (S9.1). A data holder: water is a MULTI-PASS effect
     * (planar reflection re-render + scene-color refraction grab), so the app
     * (or the future SceneRenderer, S12) sequences Water's passes explicitly —
     * Scene::OnRender3D cannot re-render the world mid-pass and skips this.
     */
    struct COSMIC_API WaterComponent
    {
        Ref<Water> WaterAsset;

        WaterComponent() = default;
        WaterComponent(const WaterComponent&) = default;
    };

    /**
     * @brief GPU particle emitter (S10.1). A data holder: emitters need a
     * per-frame Update(dt) and render with camera + scene-depth arguments the
     * app owns, so apps drive Update/Render directly (see ParticleEmitter.h).
     */
    struct COSMIC_API ParticleEmitterComponent
    {
        Ref<ParticleEmitter> Emitter;

        ParticleEmitterComponent() = default;
        ParticleEmitterComponent(const ParticleEmitterComponent&) = default;
    };

    /**
     * @brief Native C++ script link (E11). The scene stores the script's class
     * NAME (resolved to a factory through ModuleRegistry at Play) plus the
     * reflected field-override values edited in the Inspector and saved with the
     * scene. Instance is runtime-only — null in edit mode, owned by the ScriptHost
     * during Play; it is never copied meaningfully because scripts only ever run in
     * the throwaway runtime scene. Only ClassName is a plain reflected field; the
     * Fields map is (de)serialized specially by the SceneSerializer, which consults
     * the script's descriptor for each field's kind.
     */
    struct COSMIC_API NativeScriptComponent
    {
        std::string       ClassName;             // registered script class ("" = none)
        ScriptableEntity* Instance = nullptr;    // runtime-only; owned by ScriptHost

        // name -> boxed override value; authoritative in edit mode, pushed into the
        // fresh instance at Play. Empty entries fall back to the script's C++ member
        // defaults. FieldValue is the same boxed type the reflection registry uses.
        std::unordered_map<std::string, Reflect::FieldValue> Fields;

        NativeScriptComponent() = default;
        NativeScriptComponent(const NativeScriptComponent&) = default;
        NativeScriptComponent(const std::string& className) : ClassName(className) {}
    };

    /**
     * @brief Marks an entity subtree as an instance of a prefab asset (E14). Stores
     * the source `.cprefab` VFS path so "Revert to Prefab" can re-instantiate it and
     * a missing-source load can warn. No per-field override tracking in v1 — an
     * instance is a plain detached copy that happens to remember where it came from.
     */
    struct COSMIC_API PrefabComponent
    {
        std::string SourcePath;                  // "project://prefabs/Foo.cprefab"

        PrefabComponent() = default;
        PrefabComponent(const PrefabComponent&) = default;
        PrefabComponent(const std::string& path) : SourcePath(path) {}
    };
}

// ============================================================================
// CRITICAL ARCHITECTURAL NOTE ON CLIENT COMPONENT REGISTRATION:
// Built-in engine components below use manual specialization layouts. 
// If you are writing a custom simulation or game component inside an external 
// client module / DLL project, you MUST use the 'CS_REGISTER_COMPONENT(MyType)' 
// macro inside your component header file. 
// Failure to do so will cause the host binary and dynamic plugin binary to map 
// the same type name to divergent integer indices, breaking component storage.
// ============================================================================

// ODR CONTRACT: These three CS_REGISTER_COMPONENT expansions appear at global
// scope in a header and are therefore included in every translation unit that
// includes Components.h. This is safe under the One Definition Rule because each
// expansion produces a full template specialisation of entt::type_hash<T> whose
// only member is a `consteval` function returning a compile-time constant — the
// definitions are therefore token-for-token identical in every TU. Any future
// component registration added here MUST preserve this property: no static data
// members, no non-consteval member functions, and no initialisation that could
// differ between TUs. If those guarantees cannot be met, move the registration
// to a dedicated .cpp file and add a corresponding extern declaration here.
CS_REGISTER_COMPONENT(Cosmic::IDComponent)
CS_REGISTER_COMPONENT(Cosmic::OpaqueComponentsComponent)
CS_REGISTER_COMPONENT(Cosmic::RelationshipComponent)
CS_REGISTER_COMPONENT(Cosmic::TagComponent)
CS_REGISTER_COMPONENT(Cosmic::TransformComponent)
CS_REGISTER_COMPONENT(Cosmic::SpriteRendererComponent)
CS_REGISTER_COMPONENT(Cosmic::MeshRendererComponent)
CS_REGISTER_COMPONENT(Cosmic::PrimitiveMeshComponent)
CS_REGISTER_COMPONENT(Cosmic::LODGroupComponent)
CS_REGISTER_COMPONENT(Cosmic::DirectionalLightComponent)
CS_REGISTER_COMPONENT(Cosmic::PointLightComponent)
CS_REGISTER_COMPONENT(Cosmic::CameraComponent)
CS_REGISTER_COMPONENT(Cosmic::EnvironmentComponent)
CS_REGISTER_COMPONENT(Cosmic::TerrainComponent)
CS_REGISTER_COMPONENT(Cosmic::WaterComponent)
CS_REGISTER_COMPONENT(Cosmic::ParticleEmitterComponent)
CS_REGISTER_COMPONENT(Cosmic::NativeScriptComponent)
CS_REGISTER_COMPONENT(Cosmic::PrefabComponent)



// The block below just manually does this (shows what ComponentRegistry is doing)
/*
#include <entt/entt.hpp>
namespace entt
{
    template<>
    struct type_hash<Cosmic::TagComponent> final
    {
        [[nodiscard]] static consteval id_type value() noexcept
        {
            return hashed_string::value("TagComponent");
        }
    };

    template<>
    struct type_hash<Cosmic::TransformComponent> final
    {
        [[nodiscard]] static consteval id_type value() noexcept
        {
            return hashed_string::value("TransformComponent");
        }
    };

    template<>
    struct type_hash<Cosmic::SpriteRendererComponent> final
    {
        [[nodiscard]] static consteval id_type value() noexcept
        {
            return hashed_string::value("SpriteRendererComponent");
        }
    };
}
*/