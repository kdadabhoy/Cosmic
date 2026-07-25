#pragma once
// Last Modified: 7/25/2026

// ============================================================================
// Cosmic 3D components (Phase 29 W4) — the 3D half of the ECS component set.
// ============================================================================
//
// scene/Components.h keeps the 19 components EVERY engine configuration has: the
// dimension-neutral core (ID/Tag/Transform/Camera/Environment/scripts/prefab), the
// 2D renderables, and the dimension-agnostic physics tier. This header holds the 15
// that only mean anything in a 3D world — meshes and LODs, skeletal animation and
// sockets, the 3D lights, terrain/water/particles/voxels, the mesh + terrain
// colliders, and navigation.
//
// The pure-2D configuration (COSMIC_2D_ONLY) drops this file from the build outright
// (Cosmic/CMakeLists.txt step 3), and Cosmic.h includes it behind the same fence, so
// a 2D engine never compiles a line of it. A translation unit that names ANY
// component below must include this header — Components.h alone no longer declares
// them.
//
// Nothing here changed in the split beyond its address: the struct bodies, field
// order, defaults and registered names are the pre-split text verbatim, so the type
// ids and every serialized scene are unaffected (test_components3d_registry pins the
// names and the entt::type_hash values).

#include "scene/Components.h"

// Moved with the components that need them — these were Components.h includes 8-10
// before the split, and nothing that stayed behind uses them.
#include "graphics/Skeleton.h"         // A2 — AnimatorComponent runtime skeleton ref
#include "graphics/AnimationClip.h"    // A2 — AnimatorComponent runtime clip ref
#include "particles/ParticleSystem.h"  // EmitterShape/ParticleBlend/ParticleSpace (E18 emitter recipe)

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Cosmic
{
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
        bool          Enabled = true;        // T12 — false hides the mesh (editor + Play + shadow submit)

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

        // M5 — per-submesh material SLOTS for a multi-material mesh (indexed by the
        // mesh's Submesh::MaterialIndex; several submeshes may share a slot). EMPTY
        // ⇒ the legacy single MaterialAsset/MaterialPath path draws the whole mesh,
        // BYTE-IDENTICAL — this is the compat gate. Not reflected (a vector<string>
        // is not a reflectable FieldKind): the SceneSerializer special-cases the
        // "MaterialPaths" array and the Inspector draws a bespoke "Materials" list.
        // A slot whose path is empty / unresolved falls back to MaterialAsset (else
        // the Lambert Color) for that range.
        std::vector<std::string>   MaterialPaths;
        std::vector<Ref<Material>> MaterialAssets;          // runtime-resolved, parallel to MaterialPaths
        bool                       MaterialPathsResolved = false;   // runtime-only

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
     * @brief Skeletal-animation driver (Phase 20 / A2). Attach next to (or on
     * an ancestor of) a MeshRenderer whose mesh is SKINNED (Mesh::IsSkinned) —
     * Scene::UpdateAnimators samples the clip each frame into a joint palette,
     * and the render submit routes the mesh through the PBRSkinned twin.
     *
     * ClipPath addresses one clip inside a model file: "project://models/
     * Fox.glb#Run" (name) or "...glb#0" (index); a bare file path plays its
     * first clip. v1 plays ONE clip per Animator (blend trees / state machines
     * are parked — FEATURE-MATRIX). NormalizedTime [0,1] is the play head: it
     * tracks playback while Playing, and scrubbing it while paused re-poses
     * the mesh (the Inspector's scrub bar writes it).
     */
    struct COSMIC_API AnimatorComponent
    {
        std::string ClipPath;               // "file#clip" (AssetPath("animation"))
        float       Speed          = 1.0f;  // playback rate multiplier (may be negative)
        bool        Loop           = true;
        bool        Playing        = true;
        float       NormalizedTime = 0.0f;  // [0,1] play head (reflected — scrubbed)

        // --- Runtime (not reflected / serialized) ---
        Ref<AnimationClip>     ClipRef;          // resolved from ClipPath (guarded)
        std::string            ResolvedClipPath; // the path ClipRef was resolved from
        Ref<Skeleton>          SkelRef;          // from the driven skinned mesh
        std::vector<glm::mat4> Palette;          // this frame's skinning matrices
        std::vector<glm::mat4> ScratchLocals;    // sampling scratch (avoids realloc)
        float                  TimeSeconds = 0.0f;

        // M4 — per-joint BAKED-space model transforms (ImportCorrection · global),
        // published every frame by Scene::UpdateAnimators from the current pose
        // (or the bind pose when no clip is resolved). SocketComponent reads these
        // to follow a joint, and the editor's bone overlay draws from them. Empty
        // when the animator has no skeleton yet. NOT the skinning palette — these
        // are the joint frames themselves (no inverse-bind), so a child placed at
        // JointModelMatrices[j] sits ON the joint.
        std::vector<glm::mat4> ScratchGlobals;      // ComputeGlobals scratch (avoids realloc)
        std::vector<glm::mat4> JointModelMatrices;  // baked-space joint frames (sockets/overlay)

        // --- Crossfade tier (M6) — script-driven timed blend to a NEXT clip. The
        // full controller graph stays parked; this is the minimal tier a playable
        // character needs (idle↔walk↔run). NextClipPath set ⇒ Scene::UpdateAnimators
        // resolves NextClipRef, advances both heads, pose-blends the two sampled
        // LOCALS (AnimationClip::BlendLocals) by FadeElapsed/FadeDuration, and
        // PROMOTES the next clip to current when the fade completes. All runtime.
        std::string            NextClipPath;         // target "file#clip" ("" = no crossfade)
        std::string            ResolvedNextClipPath; // guard for NextClipRef resolution
        Ref<AnimationClip>     NextClipRef;          // resolved from NextClipPath
        float                  NextTimeSeconds = 0.0f;
        float                  FadeDuration    = 0.0f;   // total fade seconds (0 = no fade)
        float                  FadeElapsed     = 0.0f;   // seconds into the current fade
        std::vector<glm::mat4> ScratchLocalsB;           // second-clip sampling scratch

        /**
         * @brief Start a timed crossfade to `clipPath` ("file#clip") over
         * `seconds`. `seconds` <= 0 (or the same clip) switches immediately.
         * Re-targets an in-flight fade. Only sets intent — Scene::UpdateAnimators
         * resolves the clip (it owns the AssetLibrary) and runs the blend.
         */
        void CrossfadeTo(const std::string& clipPath, float seconds)
        {
            if (clipPath.empty() || clipPath == ClipPath)
            {
                // Already playing it (or cleared) — cancel any pending fade.
                NextClipPath.clear();
                FadeDuration = FadeElapsed = 0.0f;
                return;
            }
            if (seconds <= 0.0f)
            {
                ClipPath = clipPath;          // hard switch (UpdateAnimators re-resolves ClipRef)
                NormalizedTime = 0.0f;
                NextClipPath.clear();
                FadeDuration = FadeElapsed = 0.0f;
                return;
            }
            NextClipPath = clipPath;          // UpdateAnimators resolves + blends
            FadeDuration = seconds;
            FadeElapsed  = 0.0f;
        }

        AnimatorComponent() = default;
        AnimatorComponent(const AnimatorComponent&) = default;
    };


    /**
     * @brief Attach an entity to a joint of an animated ancestor (Phase 24 / M4,
     * gap §8.3). When an entity carries a SocketComponent and an ancestor in its
     * parent chain has an AnimatorComponent whose skeleton contains a joint named
     * `Joint`, Scene::GetWorldTransform composes:
     *
     *     socketWorld = ancestorWorld · jointFrame · (T(Position)·R(Rotation)·S(Scale))
     *
     * where `jointFrame` is the animator's published baked-space joint transform
     * (AnimatorComponent::JointModelMatrices). The entity then follows the joint
     * every frame — render, physics attach points, and scripts all read the
     * composed transform. The entity's own TransformComponent local is ignored
     * while the socket resolves (the offset lives here). If no ancestor animates,
     * or the joint name is unknown, the entity falls back to its normal
     * parent-relative transform (compat: entities WITHOUT the component are
     * untouched, and a socket whose rig hasn't posed yet behaves as an ordinary
     * child until the animator publishes joints).
     */
    struct COSMIC_API SocketComponent
    {
        std::string Joint;                          // target joint NAME (e.g. "hand.r")
        glm::vec3   Position{ 0.0f, 0.0f, 0.0f };   // offset from the joint (metres)
        glm::quat   Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };  // offset rotation (w, x, y, z)
        glm::vec3   Scale{ 1.0f, 1.0f, 1.0f };      // offset scale

        SocketComponent() = default;
        SocketComponent(const SocketComponent&) = default;
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
        bool      Enabled = true;            // T12 — false skips this light's collect

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
        // Default raised 1 -> 8 (H3): with the windowed inverse-square falloff the
        // engine uses, intensity 1 at the default 10 m radius is nearly imperceptible
        // a few metres out — so "drop a point light into a scene" now visibly lights
        // nearby default-material meshes. Tune down for a subtle fill.
        float     Intensity = 8.0f;
        float     Radius    = 10.0f;
        bool      Enabled = true;            // T12 — false skips this light's collect

        PointLightComponent() = default;
        PointLightComponent(const PointLightComponent&) = default;
    };


    class Terrain;
    class Water;
    class ParticleEmitter;
    class VoxelVolume;         // voxel/VoxelVolume.h (V1) — chunk store
    class BlockPalette;        // voxel/BlockPalette.h (V1) — block type table
    struct VoxelRenderData;    // voxel/VoxelRender.h (V3) — runtime GPU chunk meshes
    class NavWorld;            // nav/NavWorld.h (N1) — baked navmesh runtime (Recast-free pimpl)

    /** Ocean/lake wave-stack preset the WaterComponent recipe seeds from (E18).
     *  The scalar recipe overrides (amplitude/choppiness/optics) apply on top. */
    enum class WaterPreset { Lake = 0, Ocean = 1, Storm = 2 };

    /**
     * @brief Heightmap terrain (S8.1) with an authoring recipe (E18).
     * Scene::OnRender3D renders TerrainAsset with quadtree LOD around the pass
     * camera; the asset is placed by its own spec (world origin/size) — terrain
     * is world geometry, so the entity's TransformComponent is not applied.
     *
     * AUTHORING (E18): when UseRecipe is set, Scene::SyncWorldSystems (re)builds
     * TerrainAsset from the reflected recipe below via TerrainSpecification. The
     * terrain build is expensive, so the auto-build only runs once (asset null +
     * never built); the Starforge WorldSystems panel drives explicit rebuilds off
     * the JobSystem. An entity whose TerrainAsset was set in CODE keeps UseRecipe
     * false and is never touched (the Frontier compat gate).
     */
    struct COSMIC_API TerrainComponent
    {
        Ref<Terrain> TerrainAsset;           // runtime; entity skipped when null

        // --- Reflected recipe (maps onto TerrainSpecification) ----------------
        bool        UseRecipe   = false;     // gates SyncWorldSystems regen (compat)
        float       WorldSize   = 512.0f;    // meters along X and Z
        int32_t     Resolution  = 513;       // clamped to 32*2^k + 1 at build
        float       HeightScale = 60.0f;     // world height of a 1.0 sample
        float       BaseHeight  = 0.0f;      // world Y of a 0.0 sample
        uint32_t    Seed        = 1337;
        int32_t     Octaves     = 6;
        float       Frequency   = 3.0f;      // fBm periods across the terrain
        float       Lacunarity  = 2.0f;
        float       Gain        = 0.5f;
        float       EdgeFalloff = 0.0f;      // 0 = none; else island edge fade (0..1)
        std::string HeightmapPath;           // AssetPath; empty -> procedural fBm
        // Auto-splat layer tints (0=grass,1=rock,2=snow,3=sand). *Color -> picker.
        glm::vec3   GrassColor{ 0.24f, 0.38f, 0.15f };
        glm::vec3   RockColor { 0.36f, 0.33f, 0.31f };
        glm::vec3   SnowColor { 0.92f, 0.94f, 0.98f };
        glm::vec3   SandColor { 0.55f, 0.48f, 0.36f };
        // Optional splat albedo textures (resolved on the main thread at build).
        std::string GrassTex, RockTex, SnowTex, SandTex;   // AssetPath
        float       SnowHeight  = 30.0f;     // world Y where the snow layer fades in
        float       SnowBlend   = 6.0f;      // smoothstep half-width for the snow band

        // Runtime-only (NOT reflected): hash of the recipe the current asset was
        // built from. 0 == never built (SyncWorldSystems' one-shot auto-build gate).
        std::size_t BuiltSignature = 0;

        TerrainComponent() = default;
        TerrainComponent(const TerrainComponent&) = default;
    };

    /**
     * @brief Water surface (S9.1) with an authoring recipe (E18). Water is a
     * MULTI-PASS effect; the SceneRenderer sequences planar reflection, and the
     * simple Scene::OnRenderWorldFX path (editor/PlayerLayer) draws it with the
     * cheap IBL-fallback reflection. When UseRecipe is set, Scene::SyncWorldSystems
     * (re)builds WaterAsset from the recipe (cheap — Water::Create is GL-free);
     * a code-set WaterAsset keeps UseRecipe false and is never touched.
     */
    struct COSMIC_API WaterComponent
    {
        Ref<Water> WaterAsset;               // runtime

        // --- Reflected recipe (maps onto WaterSpecification) ------------------
        bool        UseRecipe = false;       // gates SyncWorldSystems regen (compat)
        WaterPreset Preset = WaterPreset::Lake;   // seeds the wave stack + base optics
        glm::vec2   Center{ 0.0f };          // world XZ center of the plane
        glm::vec2   Extent{ 200.0f, 200.0f };// world size along X and Z
        float       SurfaceHeight = 0.0f;    // world Y of the calm surface
        int32_t     GridResolution = 129;    // vertices per side of the displaced grid
        float       Amplitude  = 1.0f;       // multiplies the preset wave amplitudes
        float       Choppiness = 1.0f;       // multiplies the preset wave steepness
        glm::vec3   ShallowColor{ 0.10f, 0.42f, 0.45f };
        glm::vec3   DeepColor{ 0.02f, 0.12f, 0.20f };
        float       CausticStrength  = 0.0f;
        float       WhitecapStrength = 0.0f;
        float       SparkleStrength  = 0.0f;
        bool        Enabled = true;          // T12 — false skips SyncWorldSystems + draw

        std::size_t BuiltSignature = 0;      // runtime; not reflected

        WaterComponent() = default;
        WaterComponent(const WaterComponent&) = default;
    };

    /**
     * @brief GPU particle emitter (S10.1) with an authoring recipe (E18).
     * Scene::OnRenderWorldFX updates + draws the emitter (placed at the entity's
     * world transform). When UseRecipe is set, Scene::SyncWorldSystems (re)builds
     * Emitter from the recipe (ParticleEmitter::Create is cheap, GL-lazy). The
     * recipe's reflected fields ARE the `.cemitter` preset (saved/loaded through
     * the generic reflected-struct serializer). Default values describe a warm
     * additive campfire ember cone. A code-set Emitter keeps UseRecipe false.
     */
    struct COSMIC_API ParticleEmitterComponent
    {
        Ref<ParticleEmitter> Emitter;        // runtime

        // --- Reflected recipe (maps onto ParticleEmitterSpec) -----------------
        bool          UseRecipe = false;     // gates SyncWorldSystems regen (compat)
        bool          Enabled = true;        // T12 — false skips SyncWorldSystems + update/draw
        uint32_t      MaxParticles = 2048;
        float         SpawnRate    = 60.0f;  // particles / second
        EmitterShape  Shape        = EmitterShape::Cone;
        float         ShapeRadius  = 0.5f;   // Sphere / Cone base
        float         ConeAngleDeg = 20.0f;
        glm::vec3     BoxExtents{ 1.0f };
        float         SpeedMin = 1.0f, SpeedMax = 3.0f;
        float         LifeMin  = 1.0f, LifeMax  = 2.5f;
        glm::vec3     Gravity{ 0.0f, 1.5f, 0.0f };   // hot air lifts embers
        float         Drag = 0.6f;
        glm::vec3     Wind{ 0.4f, 0.0f, 0.0f };
        float         SizeStart = 0.10f, SizeEnd = 0.02f;
        glm::vec4     ColorStart{ 1.0f, 0.75f, 0.30f, 1.0f };   // Color
        glm::vec4     ColorEnd{ 1.0f, 0.25f, 0.05f, 0.0f };     // Color
        ParticleBlend Blend = ParticleBlend::Additive;
        ParticleSpace Space = ParticleSpace::World;
        std::string   TexturePath;           // AssetPath; empty -> procedural puff
        int32_t       FlipbookTilesX = 1, FlipbookTilesY = 1;
        float         FlipbookFps = 0.0f;
        bool          FlipbookBlend = false;
        float         SoftFadeDistance   = 0.2f;
        float         StretchByVelocity  = 0.0f;

        // --- Curl-noise turbulence (X3 / gap §11.1). Off = byte-identical. ---
        bool          NoiseEnabled   = false;
        float         NoiseStrength  = 3.0f;
        float         NoiseFrequency = 0.4f;
        int32_t       NoiseOctaves   = 2;    // clamped 1..4 at build

        // --- Local-space bounds (X4 / gap §11.3). All-zero = off (byte-identical). ---
        glm::vec3     BoundsExtents{ 0.0f }; // half-extents; 0 axis = unbounded
        bool          BoundsWrap = false;    // false = kill past bounds, true = wrap

        std::size_t BuiltSignature = 0;      // runtime; not reflected

        ParticleEmitterComponent() = default;
        ParticleEmitterComponent(const ParticleEmitterComponent&) = default;
    };

    /**
     * @brief Editable chunked voxel volume (Phase 18 / V1–V6). The scene stores a
     * tiny authoring recipe (palette + `.cvox` sidecar path + placement + a
     * flattened procedural-generation recipe) — the voxel DATA lives in the
     * `.cvox`, not the scene JSON (the E15 "params, not meshes" rule at volume
     * scale). The runtime members (Volume/Palette/Render) are rebuilt by
     * Scene::SyncVoxelVolumes from those params on load and are NOT reflected.
     *
     * Placement: the volume sits at the entity's WORLD transform translation with
     * VoxelSize metres per voxel. Streaming: when GenEnabled, chunks within
     * ViewRadius (chunks) of the camera are generated on demand; edits are kept.
     *
     * Compat gate: shipped apps attach no VoxelVolumeComponent, so
     * SyncVoxelVolumes is a no-op for them.
     */
    struct COSMIC_API VoxelVolumeComponent
    {
        // --- runtime (NOT reflected) -----------------------------------------
        Ref<VoxelVolume>        Volume;    // chunk store (loaded from VolumePath or generated)
        Ref<BlockPalette>       Palette;   // block table (loaded from PalettePath or default)
        Ref<VoxelRenderData>    Render;     // GPU chunk meshes + atlas + streaming state

        // --- reflected authoring recipe --------------------------------------
        std::string PalettePath;           // AssetPath(.cpal); empty -> default palette
        std::string VolumePath;            // AssetPath(.cvox); empty -> empty/generated volume
        float       VoxelSize  = 1.0f;     // metres per voxel
        int32_t     ViewRadius = 8;        // chunk radius around the camera for streaming
        bool        Greedy     = true;     // greedy (merged) vs culled render mesh

        // Flattened VoxelGeneratorRecipe (V6) — see voxel/VoxelGenerator.h.
        bool        GenEnabled    = false;
        uint32_t    Seed          = 1337;
        float       SurfaceLevel  = 32.0f; // voxels (world Y) of average ground
        float       Amplitude     = 24.0f; // +/- voxels of height variation
        float       Frequency     = 0.010f;
        int32_t     Octaves       = 5;
        float       Lacunarity    = 2.0f;
        float       Gain          = 0.5f;
        bool        Ridged        = false;
        float       CaveThreshold = 0.0f;  // 0 = no caves
        float       CaveFrequency = 0.05f;
        int32_t     DirtDepth     = 4;
        float       SandLevel     = -1.0e9f;
        uint32_t    GrassBlock = 1, DirtBlock = 2, StoneBlock = 3, SandBlock = 4;

        // Runtime (NOT reflected): recipe signature the resident generated chunks
        // were built from (0 = never generated).
        std::size_t BuiltGenSignature = 0;

        VoxelVolumeComponent() = default;
        VoxelVolumeComponent(const VoxelVolumeComponent&) = default;
    };

    // ========================================================================
    // Physics, the 3D-only tier (Phase 15 / J3). The rigid body and the box /
    // sphere / capsule colliders are dimension-agnostic and stay in Components.h
    // with the rest of the shared physics tier; these two derive their shape from
    // 3D world geometry — a mesh and a terrain heightfield — so they live here.
    // The same rules apply: a collider WITHOUT a RigidBody is an implicit static
    // body, and bodies exist only while a physics session runs.
    // ========================================================================

    /** @brief Mesh collider (J3) — uses the sibling MeshRenderer/Primitive mesh.
     *  Convex=true builds a ConvexHullShape (dynamic-capable); Convex=false builds a
     *  static/kinematic-only MeshShape (a Console warning fires if used on a dynamic
     *  body). */
    struct COSMIC_API MeshColliderComponent
    {
        bool Convex    = false;
        bool IsTrigger = false;
        bool Enabled   = true;               // T12 — false skips the physics bake

        MeshColliderComponent() = default;
        MeshColliderComponent(const MeshColliderComponent&) = default;
    };

    /** @brief Terrain collider (J7) — uses the sibling TerrainComponent's CPU
     *  heightfield to build a JPH::HeightFieldShape. Terrain is always static. */
    struct COSMIC_API TerrainColliderComponent
    {
        TerrainColliderComponent() = default;
        TerrainColliderComponent(const TerrainColliderComponent&) = default;
    };

    // ========================================================================
    // Navigation (Phase 26 / N2) — a Recast/Detour navmesh authored on an entity.
    // The scene stores only the reflected BAKE RECIPE (below); the built navmesh is
    // big binary that rides a `.cnav` sidecar (the `.cvox` rule), rebuilt/loaded by
    // SceneNav from the recipe. The runtime NavWorld is not reflected. Bake geometry
    // is the collision view of the scene (colliders / terrain heightfield / voxel
    // chunks), filtered to the entity's children when SourceMode == FromChildren.
    //
    // Compat gate: shipped apps attach no NavMeshComponent, so Scene::SyncNavMeshes
    // is a strict no-op for them.
    // ========================================================================

    /** How a navmesh bake gathers its source geometry (mirrors 2215's "Mode").
     *  FromChildren = only this entity's descendants (the level parented under the
     *  navmesh object); WholeScene = every collidable entity in the scene. */
    enum class NavSourceMode : int32_t { FromChildren = 0, WholeScene = 1 };

    struct COSMIC_API NavMeshComponent
    {
        // --- runtime (NOT reflected) -----------------------------------------
        Ref<NavWorld> Nav;                 // baked navmesh (loaded from .cnav or baked); null = none
        std::size_t   BuiltSignature = 0;  // recipe+geometry signature the current Nav was built from (0 = none)
        bool          Baking = false;      // an async bake is in flight (editor owns the job)

        // --- reflected bake recipe (mirrors NavBuildDesc) --------------------
        std::string SidecarPath;           // AssetPath(.cnav); empty -> derived beside the scene

        float CellSize   = 0.30f;          // xz rasterization voxel size (m)
        float CellHeight = 0.20f;          // y rasterization voxel size (m)
        float AgentRadius   = 0.6f;        // walkable area eroded by this (m)
        float AgentHeight   = 2.0f;        // vertical clearance (m)
        float AgentMaxClimb = 0.9f;        // max auto-step height (m)
        float AgentMaxSlope = 45.0f;       // max walkable slope (deg)
        float RegionMinSize   = 8.0f;      // min region (voxels; area = size^2)
        float RegionMergeSize = 20.0f;     // merge regions smaller than this (voxels)
        float EdgeMaxLen   = 12.0f;        // max contour edge (m)
        float EdgeMaxError = 1.3f;         // contour simplification (voxels)
        float DetailSampleDist     = 6.0f; // detail sample spacing (x CellSize)
        float DetailSampleMaxError = 1.0f; // detail max error (x CellHeight)
        int32_t VertsPerPoly = 6;          // max verts per navmesh poly (3..6)
        float   TileSize     = 0.0f;       // tiled build hint (voxels; 0 = solo, v1)

        // --- authoring -------------------------------------------------------
        NavSourceMode SourceMode = NavSourceMode::FromChildren;
        bool AutoGenerate      = false;    // rebake when the recipe/geometry signature changes
        bool AlwaysRenderHelper = false;   // draw the nav overlay even when not selected

        NavMeshComponent() = default;
        NavMeshComponent(const NavMeshComponent&) = default;
    };

    /**
     * @brief A navigation agent (Phase 26 / N4). Steered by DetourCrowd over the
     * scene's baked navmesh while a play session runs — the crowd exists only during
     * Play (the physics-body lifetime rule), and the agent's transform is written
     * back each fixed step like a physics body. Scripts drive it through Nav():
     * SetTarget / Stop, and receive `nav.arrived` on the scene EventBus when it
     * reaches within StoppingDistance. The runtime agent id lives in the Scene's nav
     * runtime, not here (this component stays pure authored data).
     *
     * Compat gate: shipped apps attach no NavAgentComponent, so the nav runtime is
     * a no-op for them.
     */
    struct COSMIC_API NavAgentComponent
    {
        float Radius   = 0.4f;    // agent footprint radius (m)
        float Height   = 1.8f;    // agent height (m)
        float MaxSpeed = 3.5f;    // m/s
        float MaxAccel = 8.0f;    // m/s^2
        float StoppingDistance = 0.4f;   // arrival tolerance (m) -> emits nav.arrived
        bool  AutoRepath = true;  // re-plan when the path is invalidated (crowd default)

        NavAgentComponent() = default;
        NavAgentComponent(const NavAgentComponent&) = default;
    };
}

// ODR CONTRACT — the same one Components.h states for its half of the set. These
// expansions sit at GLOBAL scope in a header, so they land in every translation unit
// that includes this file; each produces a full specialisation of entt::type_hash<T>
// whose only member is a consteval function returning a compile-time constant, which
// makes the definitions token-for-token identical everywhere. Splitting the block in
// two does not disturb that: a TU either sees a 3D component's registration (because
// it included this header, which it must in order to name the type at all) or never
// names the type. Any 3D component registration added here MUST preserve the
// property — no static data members, no non-consteval member functions, no
// TU-dependent initialisation.
CS_REGISTER_COMPONENT(Cosmic::MeshRendererComponent)
CS_REGISTER_COMPONENT(Cosmic::AnimatorComponent)
CS_REGISTER_COMPONENT(Cosmic::SocketComponent)
CS_REGISTER_COMPONENT(Cosmic::PrimitiveMeshComponent)
CS_REGISTER_COMPONENT(Cosmic::LODGroupComponent)
CS_REGISTER_COMPONENT(Cosmic::DirectionalLightComponent)
CS_REGISTER_COMPONENT(Cosmic::PointLightComponent)
CS_REGISTER_COMPONENT(Cosmic::TerrainComponent)
CS_REGISTER_COMPONENT(Cosmic::WaterComponent)
CS_REGISTER_COMPONENT(Cosmic::ParticleEmitterComponent)
CS_REGISTER_COMPONENT(Cosmic::VoxelVolumeComponent)
CS_REGISTER_COMPONENT(Cosmic::MeshColliderComponent)
CS_REGISTER_COMPONENT(Cosmic::TerrainColliderComponent)
CS_REGISTER_COMPONENT(Cosmic::NavMeshComponent)
CS_REGISTER_COMPONENT(Cosmic::NavAgentComponent)
