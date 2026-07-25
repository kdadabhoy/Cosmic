#pragma once
// Last Modified: 7/25/2026

// The 19 components EVERY engine configuration has (Phase 29 W4): the
// dimension-neutral core, the 2D renderables, and the dimension-agnostic physics
// tier (rigid body + box/sphere/capsule colliders + character controller, kept
// shared because Jolt ships on both engines). The 15 that only mean anything in a
// 3D world — meshes and LODs, skeletal animation and sockets, the 3D lights,
// terrain/water/particles/voxels, the mesh + terrain colliders, and navigation —
// live in scene/Components3D.h, which includes this header and which the pure-2D
// configuration does not compile. Name a 3D component and you must include that.

#include "core/Core.h"
#include "core/UUID.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "physics/PhysicsTypes.h"      // MotionType (J3 — reflected on RigidBodyComponent)
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

        // T13 — per-entity active flag. Effective-active = own ∧ every ancestor's
        // (Scene::IsActiveInHierarchy). An inactive entity's whole subtree is not
        // rendered, ticked, or baked. Default true → existing scenes/apps
        // unchanged (omitted from serialization while true).
        bool Active = true;

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

        // --- 2D authoring (U3, appended to keep the struct's ABI stable) -------
        // SourceRect is the sampled sub-rectangle of the sprite texture in
        // NORMALIZED UV {u0, v0, u1, v1} (default = the whole image). A
        // SpriteAnimationComponent overwrites it per frame. ZOrder sorts within
        // the 2D pass (ties broken by Position.z); PixelsPerUnit converts a
        // sprite's pixel size to world units for the ortho 2D camera.
        glm::vec4 SourceRect{ 0.0f, 0.0f, 1.0f, 1.0f };
        float     PixelsPerUnit = 100.0f;
        int32_t   ZOrder = 0;

        // --- 2D authoring, second slice (U3 full mode; ABI-appended) -----------
        // TexturePath: authored sprite image (AssetPath("texture")); empty keeps
        // the legacy behavior (ActiveMaterial if set, else a flat Color quad).
        // Lazily resolved by Scene::OnRenderSprites (main-thread/GL) into
        // Resolved. YSort: within one ZOrder, order back-to-front by -Y instead
        // of Z (per-sprite Y-sort — a lower-on-screen sprite draws in front, the
        // top-down-game convention). Sized by the texture: worldSize =
        // (SourceRect texels / PixelsPerUnit) * Transform.Scale.xy.
        std::string TexturePath;
        bool        YSort = false;

        // T12 enable gate: when false the 2D pass skips this sprite (editor + Play).
        // Default true → existing scenes/apps unchanged (omitted from serialization
        // while true, so unchanged scenes stay byte-identical).
        bool        Enabled = true;

        // Runtime-only (not reflected): lazily resolved texture + its source path.
        Ref<Texture2D> Resolved;
        std::string    ResolvedPath;

        SpriteRendererComponent() = default;
        SpriteRendererComponent(const SpriteRendererComponent&) = default;
        SpriteRendererComponent(const Ref<Material>& material) : ActiveMaterial(material) {}
        SpriteRendererComponent(const glm::vec4& color) : Color(color) {}

        /** @brief The U3 sizing rule, shared by the render pass and editor
         *  picking/outlines (pure). Textured (texW/texH > 0): world size =
         *  SourceRect texels / PixelsPerUnit x scale; untextured: the scale is
         *  the size (legacy quad behavior). Unsigned — flips are applied by the
         *  draw, not here. */
        static glm::vec2 WorldSize(const SpriteRendererComponent& s,
                                   const glm::vec2& scale, int texW, int texH)
        {
            if (texW > 0 && texH > 0)
            {
                const float ppu = (s.PixelsPerUnit > 0.0f) ? s.PixelsPerUnit : 1.0f;
                return { (s.SourceRect.z - s.SourceRect.x) * (float)texW / ppu * scale.x,
                         (s.SourceRect.w - s.SourceRect.y) * (float)texH / ppu * scale.y };
            }
            return scale;
        }
    };


    /**
     * @brief Flipbook sprite animation (U4). Plays frames from a sprite SHEET by
     * driving the sibling SpriteRendererComponent::SourceRect. Mirrors the
     * particle flipbook: a grid of FrameW x FrameH cells; `Frames` cells are
     * played along `Row` at `FPS`, framerate-independent. Attach with a
     * SpriteRendererComponent whose ActiveMaterial samples the sheet. Engine-
     * generic, default-off for shipped apps (no sprite-anim component => untouched).
     */
    struct COSMIC_API SpriteAnimationComponent
    {
        std::string SheetPath;             // AssetPath("texture") — the sprite sheet
        int32_t     FrameW  = 16;          // cell width in texels
        int32_t     FrameH  = 16;          // cell height in texels
        int32_t     Frames  = 1;           // number of cells to play (along Row)
        int32_t     Row     = 0;           // 0-based row of the sheet
        float       FPS     = 8.0f;
        bool        Playing = true;
        bool        Loop    = true;

        float       Elapsed = 0.0f;        // runtime-only (not reflected)

        SpriteAnimationComponent() = default;
        SpriteAnimationComponent(const SpriteAnimationComponent&) = default;

        /** @brief Frame index at `elapsed` seconds (pure, headless-tested). A
         *  looping clip wraps; a one-shot clamps to the last frame. */
        static int SelectFrame(float elapsed, float fps, int frames, bool loop)
        {
            if (frames <= 1 || fps <= 0.0f) return 0;
            const int f = (int)(elapsed * fps);
            if (loop) return ((f % frames) + frames) % frames;
            return f < 0 ? 0 : (f >= frames ? frames - 1 : f);
        }

        /** @brief Normalized UV {u0,v0,u1,v1} of `frame` on a texW x texH sheet
         *  with FrameW x FrameH cells on `row` (pure). V is top-left origin:
         *  row 0 is the TOP of the sheet. */
        static glm::vec4 FrameUV(int texW, int texH, int frameW, int frameH,
                                 int row, int frame)
        {
            if (texW <= 0 || texH <= 0 || frameW <= 0 || frameH <= 0)
                return { 0.0f, 0.0f, 1.0f, 1.0f };
            const float u0 = (float)(frame * frameW) / (float)texW;
            const float u1 = (float)((frame + 1) * frameW) / (float)texW;
            const float v0 = (float)(row * frameH) / (float)texH;
            const float v1 = (float)((row + 1) * frameH) / (float)texH;
            return { u0, v0, u1, v1 };
        }
    };


    /**
     * @brief Tile-grid renderer (U4). A GridW x GridH map of cells drawn from a
     * tile ATLAS (TilesetPath, TileW x TileH texel tiles, `Columns` per atlas
     * row — 0 derives it from the texture width). Cell values: 0 = empty, v > 0
     * = atlas tile index v-1 (row-major, row 0 at the TOP of the atlas image).
     *
     * WORLD MAPPING: one cell = one world unit (the 2D pixel-grid convention);
     * the entity's Position is the map's BOTTOM-LEFT corner, cells grow +X/+Y.
     * Entity rotation/scale are ignored in v1 (axis-aligned draw). Drawn inside
     * Scene::OnRenderSprites' painter order via ZOrder (key = Position.z), so
     * maps layer with sprites. Cells are serialized by the SceneSerializer as a
     * plain int array (diff-friendly; compresses fine in git) — a custom block,
     * not a reflected field. Grid dimensions clamp to 1..1024 (v1 cap); the
     * visible-cell walk is culled by the camera's world rect each draw.
     */
    struct COSMIC_API TilemapComponent
    {
        static constexpr int32_t kMaxGrid = 1024;

        std::string TilesetPath;          // AssetPath("texture") — the tile atlas
        int32_t     TileW   = 16;         // texels per tile in the atlas
        int32_t     TileH   = 16;
        int32_t     Columns = 0;          // atlas columns; 0 => texture width / TileW
        int32_t     GridW   = 32;         // map size in cells (1..kMaxGrid)
        int32_t     GridH   = 32;
        int32_t     ZOrder  = 0;          // sort order within the 2D pass
        std::vector<uint16_t> Cells;      // row-major [y*GridW + x]; y 0 = bottom row

        // Runtime-only (not reflected): lazily resolved atlas + its source path.
        Ref<Texture2D> Resolved;
        std::string    ResolvedPath;

        TilemapComponent() = default;
        TilemapComponent(const TilemapComponent&) = default;

        /** @brief Clamp the grid to 1..kMaxGrid and size Cells to match
         *  (preserving existing values; new cells are empty). */
        void EnsureCells()
        {
            GridW = GridW < 1 ? 1 : (GridW > kMaxGrid ? kMaxGrid : GridW);
            GridH = GridH < 1 ? 1 : (GridH > kMaxGrid ? kMaxGrid : GridH);
            Cells.resize((size_t)GridW * (size_t)GridH, 0);
        }

        bool InBounds(int x, int y) const
        {
            return x >= 0 && y >= 0 && x < GridW && y < GridH;
        }

        uint16_t At(int x, int y) const
        {
            const size_t i = (size_t)y * (size_t)GridW + (size_t)x;
            return (InBounds(x, y) && i < Cells.size()) ? Cells[i] : (uint16_t)0;
        }

        /** @brief 4-connected flood fill over a cell buffer (pure; shared by the
         *  editor's fill tool and tests). Fills the connected region of the
         *  value at (x,y) with `value`; returns the changed indices (empty when
         *  out of bounds or the region already holds `value`). */
        static std::vector<uint32_t> FloodFill(std::vector<uint16_t>& cells,
                                               int gridW, int gridH,
                                               int x, int y, uint16_t value)
        {
            std::vector<uint32_t> changed;
            if (x < 0 || y < 0 || x >= gridW || y >= gridH)
                return changed;
            const size_t need = (size_t)gridW * (size_t)gridH;
            if (cells.size() < need)
                cells.resize(need, 0);

            const uint16_t from = cells[(size_t)y * gridW + x];
            if (from == value)
                return changed;

            std::vector<uint32_t> stack{ (uint32_t)(y * gridW + x) };
            while (!stack.empty())
            {
                const uint32_t i = stack.back();
                stack.pop_back();
                if (cells[i] != from)
                    continue;
                cells[i] = value;
                changed.push_back(i);

                const int cx = (int)(i % gridW), cy = (int)(i / gridW);
                if (cx > 0)         stack.push_back(i - 1);
                if (cx < gridW - 1) stack.push_back(i + 1);
                if (cy > 0)         stack.push_back(i - (uint32_t)gridW);
                if (cy < gridH - 1) stack.push_back(i + (uint32_t)gridW);
            }
            return changed;
        }
    };

    /**
     * @brief 2D point light (X5 / gap §12.1) — an additive radial light for the 2D
     * sprite path. Scene::OnRender2DLights accumulates every active Light2DComponent
     * into a half-res HDR buffer (cleared to Ambient2D) and MULTIPLIES it over the
     * 2D output, so the scene darkens between lights. The light sits at its entity's
     * TransformComponent XY. With NO lights and a WHITE Ambient2D the multiply is
     * identity ⇒ the 2D output is byte-identical (compat). Normal-mapped 2D lights
     * are explicitly out of scope v1.
     */
    struct COSMIC_API Light2DComponent
    {
        glm::vec3 Color{ 1.0f, 0.85f, 0.6f };   // warm default (campfire)
        float     Radius    = 4.0f;             // world-unit reach
        float     Intensity = 1.5f;             // HDR brightness at the center
        float     Falloff   = 2.0f;             // radial falloff exponent (higher = tighter)
        bool      Enabled   = true;             // T12-style gate: false = skip

        Light2DComponent() = default;
        Light2DComponent(const Light2DComponent&) = default;
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
        enum class SkyMode { Procedural = 0, Detailed = 1, HDRI = 2, Physical = 3 };

        // Sun — drives the first DirectionalLight / the owned EnvironmentMap.
        glm::vec3 SunDirection{ -0.4f, -1.0f, -0.3f };  // direction the light TRAVELS
        glm::vec3 SunColor{ 1.0f };
        float     SunIntensity = 1.0f;

        // Sky + IBL.
        SkyMode     Sky = SkyMode::Procedural;
        std::string HdriPath;                 // used when Sky == HDRI

        // Physical atmosphere (X1, gap §7.2) — analytic Rayleigh+Mie single
        // scattering, active ONLY when Sky == Physical. It bakes into the same
        // environment cube the skybox + IBL read, so lighting always matches the
        // visual sky. Values below are ignored by every other mode, so their
        // presence keeps Procedural/Detailed/HDRI output byte-identical.
        float     Turbidity     = 2.5f;   // haze: scales Mie density (1 = pristine, 10 = smoggy)
        float     RayleighScale = 1.0f;   // scales Rayleigh (blue) scattering
        float     MieScale      = 1.0f;   // scales Mie (white haze / sun halo) scattering
        float     MieG          = 0.80f;  // Mie phase asymmetry (sun-halo tightness), 0..0.99
        float       TimeOfDay = 12.0f;        // hours 0..24 (sun scrub)
        bool        Skybox = true;            // == SceneRendererSettings::Skybox
        bool        IBL = true;               // == SceneRendererSettings::IBL
        float       IBLIntensity = 1.0f;
        float       Exposure = 1.0f;          // == SceneRenderDesc::Exposure

        // Environment polish (X2, gap §7.3/§7.4) — defaults keep frames identical.
        float       AmbientIntensity = 1.0f;  // scales the IBL/flat ambient term (1 = today)
        float       Gamma            = 2.2f;  // tonemap output gamma (2.2 = the shipped curve)
        float       SunAngularSize   = 0.53f; // sun-disc DIAMETER in degrees (Detailed/Physical)

        // 2D lighting ambient (X5, gap §12.1) — the base level the Light2D buffer is
        // cleared to and MULTIPLIED over the 2D output. WHITE (default) = no darkening
        // ⇒ 2D scenes without lights are byte-identical; darken it for night scenes.
        glm::vec3   Ambient2D{ 1.0f };

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

        // Vignette (Q5, gap §9.4a) — post-tonemap edge darkening. Default OFF ⇒
        // byte-identical output (compat). Amount is the blend strength; Radius/
        // Feather shape the falloff; Color is the edge colour (black by default).
        bool      Vignette = false;
        float     VignetteAmount  = 0.35f;
        float     VignetteRadius  = 0.9f;
        float     VignetteFeather = 0.4f;
        glm::vec3 VignetteColor{ 0.0f, 0.0f, 0.0f };

        EnvironmentComponent() = default;
        EnvironmentComponent(const EnvironmentComponent&) = default;
    };


    // ========================================================================
    // Physics (Phase 15 / J3) — a generic Jolt-backed rigid-body tier. A body is a
    // RigidBodyComponent + >= 1 collider on the same entity (multiple colliders =>
    // a compound shape). A collider WITHOUT a RigidBody is an implicit static body
    // (the ground/world). Bodies exist only while a physics session runs (editor
    // Play / PlayerLayer); edit mode holds no Jolt objects — these components are
    // the authored truth, the bodies are derived (Scene::OnPhysicsStart). No runtime
    // body id lives on the components; the Scene's physics runtime owns that map.
    // ========================================================================

    /**
     * @brief Rigid-body settings (J3). Pair with one or more collider components.
     * Motion drives both the behaviour and the coarse broadphase layer; the 16-bit
     * CollisionCategory / CollidesWith pair is the fine gameplay filter (two bodies
     * collide iff each one's category is in the other's mask).
     */
    struct COSMIC_API RigidBodyComponent
    {
        MotionType Motion = MotionType::Static;

        float Mass           = 1.0f;      // kg (dynamic only)
        float Friction       = 0.5f;      // 0..1
        float Restitution    = 0.1f;      // 0 = no bounce, 1 = perfectly elastic
        float LinearDamping  = 0.05f;
        float AngularDamping  = 0.05f;
        float GravityFactor  = 1.0f;      // scales gravity for this body (0 = float)
        bool  CCD            = false;     // continuous collision for fast small bodies
        bool  StartAsleep    = false;

        // Fine collision filter (bit category + mask). Defaults collide with all.
        uint32_t CollisionCategory = 0x0001;   // stored 16-bit; uint32 for reflection
        uint32_t CollidesWith      = 0xFFFF;

        RigidBodyComponent() = default;
        RigidBodyComponent(const RigidBodyComponent&) = default;
        RigidBodyComponent(MotionType m) : Motion(m) {}
    };

    /** @brief Box collider (J3). HalfExtents are pre-scale; the entity's world scale
     *  is baked into the shape at build. IsTrigger => a sensor (overlap events, no
     *  contact response). */
    struct COSMIC_API BoxColliderComponent
    {
        glm::vec3 HalfExtents{ 0.5f, 0.5f, 0.5f };
        glm::vec3 Offset{ 0.0f };
        bool      IsTrigger = false;
        bool      Enabled = true;            // T12 — false skips the physics bake

        BoxColliderComponent() = default;
        BoxColliderComponent(const BoxColliderComponent&) = default;
    };

    /** @brief Sphere collider (J3). Non-uniform scale warns once and uses X scale. */
    struct COSMIC_API SphereColliderComponent
    {
        float     Radius = 0.5f;
        glm::vec3 Offset{ 0.0f };
        bool      IsTrigger = false;
        bool      Enabled = true;            // T12 — false skips the physics bake

        SphereColliderComponent() = default;
        SphereColliderComponent(const SphereColliderComponent&) = default;
    };

    /** @brief Capsule collider (J3). HalfHeight = half the cylinder part (excludes
     *  the two hemispherical caps). Y-axis aligned. */
    struct COSMIC_API CapsuleColliderComponent
    {
        float     Radius     = 0.5f;
        float     HalfHeight = 0.5f;
        glm::vec3 Offset{ 0.0f };
        bool      IsTrigger = false;
        bool      Enabled = true;            // T12 — false skips the physics bake

        CapsuleColliderComponent() = default;
        CapsuleColliderComponent(const CapsuleColliderComponent&) = default;
    };

    /** @brief Character controller (J6) — a kinematic capsule with slope/step
     *  handling (Jolt CharacterVirtual). Drive it from a script via Character().
     *  Does not need a RigidBody/collider; it owns its own capsule. */
    struct COSMIC_API CharacterControllerComponent
    {
        float Height      = 1.8f;    // total capsule height incl. caps
        float Radius      = 0.3f;
        float MaxSlopeDeg = 45.0f;
        float StepHeight  = 0.35f;
        float Mass        = 80.0f;

        CharacterControllerComponent() = default;
        CharacterControllerComponent(const CharacterControllerComponent&) = default;
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
     * @brief SystemScript link (H9) — logic bound to a *class* of entities rather
     * than one entity. Held on any single entity (scene-level); at Play the ScriptHost
     * resolves ClassName -> a SystemScript subclass registered with CS_SYSTEM, builds
     * ONE instance, and each tick calls its OnUpdateAll with the matching entity set
     * (declared by the system's Requires<>/WithTag query). Mirrors NativeScriptComponent:
     * ClassName is the only plain reflected field; Fields holds the reflected overrides
     * ((de)serialized out-of-band via the SystemDescriptor), and Instance is runtime-only.
     */
    struct COSMIC_API SystemScriptComponent
    {
        std::string ClassName;                  // registered system class ("" = none)
        void*       Instance = nullptr;         // runtime-only SystemScript*; owned by ScriptHost

        // name -> boxed override value (same as NativeScriptComponent::Fields).
        std::unordered_map<std::string, Reflect::FieldValue> Fields;

        SystemScriptComponent() = default;
        SystemScriptComponent(const SystemScriptComponent&) = default;
        SystemScriptComponent(const std::string& className) : ClassName(className) {}
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

// ODR CONTRACT: These CS_REGISTER_COMPONENT expansions appear at global scope in
// a header and are therefore included in every translation unit that includes
// Components.h. (W4 moved the 3D half of the block to the bottom of
// scene/Components3D.h, under the same contract — a TU that names a 3D component
// includes that header, so it sees that half too.) This is safe under the One
// Definition Rule because each
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
CS_REGISTER_COMPONENT(Cosmic::SpriteAnimationComponent)
CS_REGISTER_COMPONENT(Cosmic::TilemapComponent)
CS_REGISTER_COMPONENT(Cosmic::Light2DComponent)
CS_REGISTER_COMPONENT(Cosmic::CameraComponent)
CS_REGISTER_COMPONENT(Cosmic::EnvironmentComponent)
CS_REGISTER_COMPONENT(Cosmic::RigidBodyComponent)
CS_REGISTER_COMPONENT(Cosmic::BoxColliderComponent)
CS_REGISTER_COMPONENT(Cosmic::SphereColliderComponent)
CS_REGISTER_COMPONENT(Cosmic::CapsuleColliderComponent)
CS_REGISTER_COMPONENT(Cosmic::CharacterControllerComponent)
CS_REGISTER_COMPONENT(Cosmic::NativeScriptComponent)
CS_REGISTER_COMPONENT(Cosmic::SystemScriptComponent)
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