#pragma once
// Last Modified: 5/24/2026

#include "core/Core.h"
#include "core/UUID.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Skeleton.h"       // A2 — AnimatorComponent runtime skeleton ref
#include "graphics/AnimationClip.h"  // A2 — AnimatorComponent runtime clip ref
#include "particles/ParticleSystem.h"  // EmitterShape/ParticleBlend/ParticleSpace (E18 emitter recipe)
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
CS_REGISTER_COMPONENT(Cosmic::SpriteAnimationComponent)
CS_REGISTER_COMPONENT(Cosmic::TilemapComponent)
CS_REGISTER_COMPONENT(Cosmic::Light2DComponent)
CS_REGISTER_COMPONENT(Cosmic::MeshRendererComponent)
CS_REGISTER_COMPONENT(Cosmic::AnimatorComponent)
CS_REGISTER_COMPONENT(Cosmic::SocketComponent)
CS_REGISTER_COMPONENT(Cosmic::PrimitiveMeshComponent)
CS_REGISTER_COMPONENT(Cosmic::LODGroupComponent)
CS_REGISTER_COMPONENT(Cosmic::DirectionalLightComponent)
CS_REGISTER_COMPONENT(Cosmic::PointLightComponent)
CS_REGISTER_COMPONENT(Cosmic::CameraComponent)
CS_REGISTER_COMPONENT(Cosmic::EnvironmentComponent)
CS_REGISTER_COMPONENT(Cosmic::TerrainComponent)
CS_REGISTER_COMPONENT(Cosmic::WaterComponent)
CS_REGISTER_COMPONENT(Cosmic::ParticleEmitterComponent)
CS_REGISTER_COMPONENT(Cosmic::VoxelVolumeComponent)
CS_REGISTER_COMPONENT(Cosmic::RigidBodyComponent)
CS_REGISTER_COMPONENT(Cosmic::BoxColliderComponent)
CS_REGISTER_COMPONENT(Cosmic::SphereColliderComponent)
CS_REGISTER_COMPONENT(Cosmic::CapsuleColliderComponent)
CS_REGISTER_COMPONENT(Cosmic::MeshColliderComponent)
CS_REGISTER_COMPONENT(Cosmic::TerrainColliderComponent)
CS_REGISTER_COMPONENT(Cosmic::CharacterControllerComponent)
CS_REGISTER_COMPONENT(Cosmic::NavMeshComponent)
CS_REGISTER_COMPONENT(Cosmic::NavAgentComponent)
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