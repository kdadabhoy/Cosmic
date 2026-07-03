#pragma once
// Last Modified: 5/24/2026

#include "core/Core.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "scene/ComponentRegistry.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

namespace Cosmic
{
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

        MeshRendererComponent() = default;
        MeshRendererComponent(const MeshRendererComponent&) = default;
        MeshRendererComponent(const Ref<Mesh>& mesh) : MeshAsset(mesh) {}
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
CS_REGISTER_COMPONENT(Cosmic::TagComponent)
CS_REGISTER_COMPONENT(Cosmic::TransformComponent)
CS_REGISTER_COMPONENT(Cosmic::SpriteRendererComponent)
CS_REGISTER_COMPONENT(Cosmic::MeshRendererComponent)
CS_REGISTER_COMPONENT(Cosmic::DirectionalLightComponent)
CS_REGISTER_COMPONENT(Cosmic::PointLightComponent)
CS_REGISTER_COMPONENT(Cosmic::TerrainComponent)
CS_REGISTER_COMPONENT(Cosmic::WaterComponent)
CS_REGISTER_COMPONENT(Cosmic::ParticleEmitterComponent)



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