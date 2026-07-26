# Voxels — Guide

**What this covers:** authoring editable block worlds — the **block palette**, the sparse **chunk
store** and its dirty tracking, the `.cvox`/`.cpal` files, the **mesher** (culled vs greedy),
`VoxelVolumeComponent` and what `Scene::SyncVoxelVolumes` does every frame, **editing** voxels from
a script and with the editor brush, **procedural generation** and streaming, and **collision** —
one static body per resident chunk, rebuilt when the chunk goes dirty.
**Source of truth:** `Cosmic/src/voxel/VoxelVolume.{h,cpp}`, `voxel/BlockPalette.{h,cpp}`,
`voxel/VoxelMesher.{h,cpp}`, `voxel/VoxelGenerator.{h,cpp}`, `voxel/VoxelRender.{h,cpp}`,
`scene/Components3D.h`, `scene/Scene3D.cpp`, `scene/SceneNav.cpp`, `physics/ScenePhysics.cpp`,
`scripting/ScriptableEntity.h`, `reflect/TypeRegistry3D.cpp`,
`Projects/Starforge/src/panels/VoxelPanel.cpp`, `Projects/Starforge/src/ViewportController.cpp`,
`Projects/Starforge/src/commands/EditorCommands.cpp`, `Projects/Starforge/src/StarforgeApp.cpp`
(`BuildForgeBlocks`), `tests/test_voxel.cpp`, `tests/test_voxel_collision.cpp`
**API Reference:** *none — the whole `voxel/` tier has **no row** in the
[reference manifest](../reference/README.md), so this chapter is the client-facing source.* ·
**How it works:** *none — there is no `docs/systems/` explainer for voxels either.*
**Configuration:** **3D only.** `Cosmic/src/voxel/` is filtered out of the 2D engine build,
`VoxelVolumeComponent` lives in `scene/Components3D.h`, and the `Voxels()` script proxy sits inside
`#ifndef COSMIC_2D_ONLY` in `scripting/ScriptableEntity.h`. Naming any of it in a `COSMIC_2D_ONLY`
tree is a compile error, by design — see
[`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md).

Cosmic's voxel worlds are built out of four small, separable pieces, and it pays to know which one
you are touching:

| Piece | What it is | GL? | Physics? |
| --- | --- | --- | --- |
| `BlockPalette` | id → block type (name, solidity, per-face atlas tile, colour). Id 0 is always Air. | no | no |
| `VoxelVolume` | the **authored truth**: a sparse map of 32³ chunks of `uint16` block ids, plus a dirty set | no | no |
| `VoxelMesher` | pure chunk → `MeshData` (render *or* collision geometry) | no | no |
| `VoxelRenderData` | the **runtime** side: uploaded chunk meshes, the procedural atlas, streaming bookkeeping | yes | — |

Only the last one needs a GL context, which is why the first three are fully headless-testable and
why `tests/test_voxel.cpp` can round-trip a million voxels without a window.

**The sample.** `ForgeBlocks` is the voxel showcase, and — like ForgePong and FlowDemo — it is **not
a folder in `Projects/`**. It is generated in code by `StarforgeApp::BuildForgeBlocks`
(`StarforgeApp.cpp:2690`), reachable from the Starforge homescreen's **Voxel Sample** button. Every
`ForgeBlocks` snippet below is quoted from there.

---

## Quick start

### In the editor

**Entity ▸ World ▸ Voxel Volume.** That creates an entity with a `VoxelVolumeComponent` whose
`GenEnabled` is already **on**, and opens the **Voxels** panel. Chunks of hilly terrain stream in
around the camera within a frame or two. Tick *Edit in viewport*, pick a block from the palette
list, then **LMB places** and **RMB breaks**.

### In code

```cpp
#include <Cosmic.h>
#include "voxel/VoxelVolume.h"      // not pulled in by <Cosmic.h> — see "Which headers you need"
#include "voxel/BlockPalette.h"

// A voxel world at the origin, 1 m per voxel, streaming terrain around the camera.
Cosmic::Entity world = scene->CreateEntity("Voxel World");
{
    auto& v = world.AddComponent<Cosmic::VoxelVolumeComponent>();
    v.VoxelSize    = 1.0f;
    v.ViewRadius   = 8;          // chunks around the camera
    v.Greedy       = true;
    v.GenEnabled   = true;       // stream-generate ungenerated chunks
    v.Seed         = 20260708u;
    v.SurfaceLevel = 24.0f;      // voxels of world Y
    v.Amplitude    = 9.0f;
    v.Frequency    = 0.02f;
}
```

That is the whole setup. You never call `VoxelVolume::Create` or `VoxelMesher::BuildChunk` yourself
in a scene — `Scene::SyncVoxelVolumes` runs at the top of `Scene::OnRender3D` **and**
`Scene::BuildRenderDesc`, and it creates the volume, loads or generates the blocks, builds the
atlas, meshes dirty chunks and hands the results to the renderer.

To dig and place from a script, add a `NativeScriptComponent` and use the
[`Voxels()` proxy](#editing-voxels-from-code):

```cpp
void OnUpdate(float) override
{
    const glm::vec3 eye = glm::vec3(GetScene().GetWorldTransform(GetEntity())[3]);
    const glm::vec3 fwd = -glm::vec3(GetScene().GetWorldTransform(GetEntity())[2]);
    if (Cosmic::Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_LEFT))
        Voxels().Break(eye, fwd, 6.0f);
}
```

### Which headers you need

`Cosmic.h` does **not** include any `voxel/` header directly. What you get for free depends on what
you name:

| You name | Comes from | Extra include needed |
| --- | --- | --- |
| `VoxelVolumeComponent` | `scene/Components3D.h` (via `Cosmic.h`) | none |
| `VoxelVolume`, `BlockPalette`, `BlockType`, `VoxelRayHit` | `scripting/ScriptableEntity.h` (via `Cosmic.h`) | none in practice; include `voxel/VoxelVolume.h` + `voxel/BlockPalette.h` explicitly if you don't want to rely on that |
| `VoxelRenderData` (chunk-mesh counts, `CollisionDirty`) | — | `#include "voxel/VoxelRender.h"` |
| `VoxelMesher`, `VoxelMeshMode` | — | `#include "voxel/VoxelMesher.h"` |
| `VoxelGenerator`, `VoxelGeneratorRecipe` | — | `#include "voxel/VoxelGenerator.h"` |

`Components3D.h` only **forward-declares** `VoxelVolume`, `BlockPalette` and `VoxelRenderData`
(`:327-329`), which is enough to hold them in a `Ref<>` but not to call anything on them.

---

## Blocks: the palette

A `BlockPalette` maps a compact `uint16` id to a `BlockType`. The volume stores only ids; the
palette is what turns an id into everything the mesher and renderer need.

```cpp
struct BlockType
{
    std::string Name        = "Block";
    bool        Solid       = true;     // occludes neighbour faces + has collision
    bool        Transparent = false;    // a transparent solid does NOT cull its neighbour's face
    bool        Emissive    = false;    // reserved — the voxel-light unlock is parked
    uint16_t    TileTop     = 0;        // per-face atlas tile indices
    uint16_t    TileSide    = 0;
    uint16_t    TileBottom  = 0;
    glm::vec3   Color{ 0.8f, 0.8f, 0.8f };   // procedural-atlas fill / tint
};
```

**Id 0 is always Air.** `Reset()` (called by the constructor) reserves it as a non-solid block, and
`FromJson` force-clears `Solid` on element 0 even if the file said otherwise. Ids are dense — 1, 2,
3, … — so a block's id doubles as its default atlas tile index.

Three queries decide everything downstream, and they are not the same question:

| Call | True when | Used by |
| --- | --- | --- |
| `IsAir(id)` | `id == 0` **or** out of range | convenience |
| `IsSolid(id)` | `id != 0`, in range, and `Solid` | the mesher (does this cell own a face?), the ray cast, collision |
| `Occludes(id)` | in range, `Solid` **and not** `Transparent` | the mesher (does the *neighbour* hide my face?) |

Out-of-range ids read as Air/not-solid rather than crashing, and `Get(id)` clamps to element 0.

### The default palette

`BlockPalette::CreateDefault()` is the ForgeBlocks starter set, and it is what a volume falls back
to when `PalettePath` is empty or fails to load:

| Id | Name | Tiles (top / side / bottom) | Colour |
| --- | --- | --- | --- |
| 0 | Air | — | — |
| 1 | Grass | 1 / **7** / 2 | `{0.34, 0.62, 0.24}` |
| 2 | Dirt | 2 / 2 / 2 | `{0.42, 0.30, 0.18}` |
| 3 | Stone | 3 / 3 / 3 | `{0.48, 0.48, 0.50}` |
| 4 | Sand | 4 / 4 / 4 | `{0.80, 0.74, 0.52}` |
| 5 | Wood | 5 / 5 / 5 | `{0.52, 0.37, 0.22}` |
| 6 | Leaves | 6 / 6 / 6 | `{0.22, 0.44, 0.18}` |

Grass is the only three-face block: its top samples tile 1, its bottom borrows dirt's tile 2, and
its sides sample **tile 7 — a tile with no block of its own**. That is the point of separating tile
indices from block ids. `Count()` is therefore 7 while the atlas grid is 3×3.

### Adding blocks

```cpp
Cosmic::Ref<Cosmic::BlockPalette> pal = Cosmic::BlockPalette::CreateDefault();

// One tile per block — the common case. The new block's three faces all sample
// its own id, and the atlas grid grows to fit.
const uint16_t ice = pal->AddBlock("Ice", { 0.62f, 0.80f, 0.92f });

// Fully specified — per-face tiles you set yourself.
Cosmic::BlockType glass("Glass", { 0.75f, 0.85f, 0.90f });
glass.Transparent = true;                  // does not cull the face behind it
glass.TileTop = glass.TileSide = glass.TileBottom = 8;
const uint16_t glassId = pal->AddBlock(glass);
```

`RefitAtlas()` runs on every `AddBlock` and grows the grid to the smallest square that holds
`maxTile + 1` cells. `SetAtlasTiles(x, y)` overrides it (clamped to ≥ 1 on each axis) — that is how
`CreateDefault` forces 3×3 to make room for tile 7.

`TileUV(tile)` returns the normalized `{u0, v0, u1, v1}` rect of a tile, **row-major from the
top-left** — the same convention as `SpriteAnimation::FrameUV`.

### The `.cpal` file

`ToJson()`/`FromJson()` are pure; `Save(path)`/`Load(path)` add disk I/O and **do** resolve VFS
paths through `FileSystem::Resolve`. The schema is hand-written (the reflection registry has no
array-of-struct field kind) and pretty-printed with `dump(2)`:

```json
{
  "cosmic_asset": "block_palette",
  "version": 1,
  "atlas_tiles_x": 3,
  "atlas_tiles_y": 3,
  "blocks": [
    { "name": "Air",   "solid": false, "transparent": false, "emissive": false,
      "tile_top": 0, "tile_side": 0, "tile_bottom": 0, "color": [0.0, 0.0, 0.0] },
    { "name": "Grass", "solid": true,  "transparent": false, "emissive": false,
      "tile_top": 1, "tile_side": 7, "tile_bottom": 2, "color": [0.34, 0.62, 0.24] }
  ]
}
```

Failure behaviour: `FromJson` returns `false` and **leaves the palette untouched** on malformed
input or a missing/non-array `blocks`; `Load` logs `CS_CORE_WARN` on a missing file and
`CS_CORE_ERROR` on a malformed one, returning `nullptr` either way. Every field is read with
`.value(…, default)`, so a partial file parses.

> **There is no palette editor.** The Content Browser's **Create ▸ Palette** writes a `.cpal`
> containing exactly `CreateDefault()`, and the `.cpal` row is registered with `AssetOpen::None` —
> double-clicking it opens nothing. The Voxels panel *picks* a block; it cannot add, rename or
> recolour one. To author a custom palette, either hand-edit the JSON or build it in code and
> `Save` it.

---

## Storing voxels: the volume and its chunks

A `VoxelVolume` is a sparse `unordered_map` from chunk coordinate to a dense
`std::vector<uint16_t>` of `32*32*32 = 32768` block ids. An all-air region costs nothing: the chunk
is simply absent.

Coordinates are **world voxel coordinates** — signed and unbounded. The mapping to chunk-local
space is arithmetic shift and mask, which is why negatives are correct without a branch:

```cpp
VoxelVolume::ChunkCoord(x, y, z);   // { x >> 5, y >> 5, z >> 5 }   floor-division by 32
VoxelVolume::LocalCoord(x, y, z);   // { x & 31, y & 31, z & 31 }   non-negative remainder
VoxelVolume::LocalIndex(lx, ly, lz);// lx + ly*32 + lz*1024
VoxelVolume::ChunkMinVoxel(chunk);  // chunk * 32
```

`ChunkCoord(-1,0,0)` is `(-1,0,0)` and `LocalCoord(-1,0,0)` is `(31,0,0)` — pinned by
`tests/test_voxel.cpp:31`.

### Get and Set

```cpp
uint16_t Get(int x, int y, int z) const;          // 0 (Air) if the chunk is not resident
void     Set(int x, int y, int z, uint16_t block);
// glm::ivec3 overloads of both.
```

`Set` has three behaviours worth knowing:

- **Setting Air in a non-resident region is a no-op.** It does not allocate a chunk of zeros.
- **Setting the value a cell already holds is a no-op.** No dirty flag, no work.
- **Otherwise it allocates the chunk if needed, writes, and marks it dirty** — *plus any neighbour
  chunk sharing the touched face's seam.* Editing local `x == 0` also dirties the `-X` neighbour,
  `x == 31` the `+X` neighbour, and likewise for Y and Z. A face's visibility depends on the block
  on the other side of a chunk boundary, so both sides have to re-mesh.

That seam rule means a single `Set` at a chunk corner dirties **four** chunks (its own plus three
negative neighbours) — `test_voxel.cpp:69` asserts exactly that.

### Placement in the world

```cpp
void      SetOrigin(const glm::vec3& o);   // world position of voxel (0,0,0)'s min corner
void      SetVoxelSize(float s);           // metres per voxel; clamped to >= 1e-4
glm::ivec3 WorldToVoxel(const glm::vec3& p) const;   // floor
glm::vec3  VoxelToWorld(const glm::ivec3& v) const;  // the voxel's MIN corner
```

Voxel `(5,0,0)` in a 1 m volume at the origin spans world `[5,6] × [0,1] × [0,1]`, so
`VoxelToWorld` gives you the corner, not the centre. Add `0.5f * VoxelSize` per axis for a centre.

### Chunks and the dirty set

```cpp
bool         HasChunk(const glm::ivec3& c) const;
std::size_t  ChunkCount() const;
const std::vector<uint16_t>* ChunkBlocks(const glm::ivec3& c) const;   // nullptr if absent
std::vector<uint16_t>&       EmplaceChunk(const glm::ivec3& c);        // creates all-air; does NOT dirty
void ForEachChunk(const std::function<void(const glm::ivec3&)>& fn) const;   // order unspecified
bool ComputeBounds(glm::ivec3& outMin, glm::ivec3& outMax) const;      // false when empty
void Clear();                                                          // chunks + dirt

void MarkChunkDirty(const glm::ivec3& c);
bool AnyDirty() const;
void TakeDirtyChunks(std::vector<glm::ivec3>& out);   // MOVES the set into `out` and clears it
```

`TakeDirtyChunks` is a drain, not a peek — whoever calls it owns the rebuild. In a scene that is
`Scene::SyncVoxelVolumes`; if you call it yourself, the engine will not re-mesh those chunks.

`ComputeBounds` reports whole-chunk bounds (min corner of the lowest chunk to max corner of the
highest), not the tight bounds of the solid blocks.

`Clear()` drops chunks and dirt but **does not** touch `VoxelRenderData` — the uploaded chunk
meshes, the `Generated` set and `CollisionDirty` are the caller's to clear. The Voxels panel's
Regenerate button does all four.

---

## Saving a world: the `.cvox` sidecar

Voxel data does not live in the scene JSON. The scene stores a *recipe* — palette path, `.cvox`
path, placement, generation parameters — and the blocks ride a sidecar file. That is the E15
"params, not meshes" rule applied at volume scale.

```cpp
volume->Save("project://voxels/world.cvox");   // creates parent directories
volume->Load("project://voxels/world.cvox");
// Pure buffer forms for tests / custom transports:
std::vector<uint8_t> buf;
volume->SaveToBuffer(buf);
volume->LoadFromBuffer(buf);
```

The format is a chunked run-length encoding:

| Offset | Field |
| --- | --- |
| 0..3 | magic `'C' 'V' 'O' 'X'` |
| 4 | version — **1**, and any other value is rejected |
| 5 | `float VoxelSize` |
| 9 | `float3 Origin` |
| 21 | `u32 chunkCount` |
| then, per chunk | `i32 x, y, z`, `u32 runCount`, then `runCount × { u16 value, u32 length }` |

Chunks are emitted in sorted `(x, y, z)` order specifically so that a save → load → save round trip
is **byte-identical** regardless of hash-map iteration order; `test_voxel.cpp:84` pins that over a
100³ patterned block.

Failure behaviour:

- `LoadFromBuffer` returns `false` on a short buffer, a bad magic, a version other than 1, a
  truncated record, runs that overflow 32768 cells, or runs that do not sum to exactly 32768. It
  **clears the existing chunks before** decoding, so a failed load leaves the volume partially
  emptied — load into a fresh `VoxelVolume` if you need to keep the old one.
- Every chunk restored by `LoadFromBuffer` is marked **dirty**, so the next sync re-meshes and
  re-collides the whole file.
- `Save` logs `CS_CORE_ERROR` and returns `false` when the file cannot be opened. `Load` returns
  `false` **silently** when the file is missing — `SyncVoxelVolumes` calls it best-effort, so a
  typo'd `VolumePath` shows up as an empty world with a clean log.
- Both `Save` and `Load` resolve VFS paths (unlike `SceneManager::Load` and `Shader::Create`).

> **A scene overrides the file's placement.** `Origin` and `VoxelSize` are stored in the `.cvox`
> and restored by `Load` — and then `SyncVoxelVolumes` immediately overwrites both from the
> component (`SetVoxelSize(vc.VoxelSize)`, `SetOrigin(world translation)`). The saved placement only
> survives when you load a volume by hand, outside a `VoxelVolumeComponent`.

---

## Meshing chunks

`VoxelMesher` turns one chunk into a `MeshData` — the same GL-free geometry type
`graphics/Mesh.h` uses everywhere else, so a `JobSystem` worker can build it off the main thread and
the main thread uploads it with `Mesh::Create`.

```cpp
static MeshData BuildChunk(const VoxelVolume&, const glm::ivec3& chunk, const BlockPalette&,
                           VoxelMeshMode mode = VoxelMeshMode::Greedy);
static MeshData BuildCollision(const VoxelVolume&, const glm::ivec3& chunk, const BlockPalette&);
```

A face is emitted between a **solid** voxel and a neighbour that does **not occlude** it — that is,
Air or a transparent block. Neighbour lookups go through the volume, so faces on chunk seams are
correct across boundaries. An all-air chunk returns empty `MeshData`.

### Culled vs greedy

| Mode | What it does | Texturing | Use when |
| --- | --- | --- | --- |
| `Culled` | one quad per exposed face, each carrying its own tile UV rect | correct for any atlas | you use an image atlas with per-tile detail |
| `Greedy` | coplanar same-block faces merge into rectangles | one tile rect is **stretched** across the whole run | the default; correct for the flat-colour procedural atlas |

The difference is large. A solid 32³ chunk greedy-meshes to **exactly 6 quads**; three blocks in a
row are 14 quads culled and 6 greedy (`test_voxel.cpp:170`, `:184`, `:200`).

The greedy stretch is the documented trade-off: merged quads span one tile's UV rect across the
whole rectangle, so a textured atlas smears. The procedural atlas paints flat colours per tile, so
stretching is invisible — which is why `Greedy` is the default and why switching to an image atlas
means switching to `Culled`.

### Collision geometry

`BuildCollision` is the same greedy walk with two changes: it merges across **different** solid
block types (grass and dirt tops become one quad — collision does not care which block it is), and
it writes no UVs. That is the minimal triangle soup Jolt's static mesh shape wants.

### Geometry conventions

- Positions are **absolute voxel coordinates**, not chunk-local and not metres. One
  `translate(Origin) * scale(VoxelSize)` transform therefore places every chunk of the volume — and
  that is exactly what the draw path, the picker and the collision baker each build.
- Quads are two CCW triangles wound so front faces point outward; normals are unit and axis-aligned
  (`test_voxel.cpp:223` checks the winding against the stored normal per triangle).

### Ambient occlusion exists but is not used

`VoxelMesher::VertexAO(side1, side2, corner)` is the classic 0–3 corner AO term and is unit-tested,
but **nothing calls it**. The shared `MeshVertex` layout carries no colour channel, so there is
nowhere to put the result. It is kept for a future voxel shader with a vertex-colour attribute.

---

## Putting one in a scene: `VoxelVolumeComponent`

The component is a small reflected recipe plus three runtime `Ref`s that are **not** serialized.

| Field | Default | Meaning |
| --- | --- | --- |
| `PalettePath` | `""` | `AssetPath(".cpal")`; empty → `BlockPalette::CreateDefault()` |
| `VolumePath` | `""` | `AssetPath(".cvox")`; empty → an empty volume |
| `VoxelSize` | `1.0` | metres per voxel (Inspector range 0.05–16) |
| `ViewRadius` | `8` | chunk radius streamed around the camera (range 1–64) |
| `Greedy` | `true` | greedy-merged vs culled render mesh |
| `GenEnabled` | `false` | stream-generate ungenerated chunks in view |
| `Seed` | `1337` | generator seed |
| `SurfaceLevel` | `32.0` | average ground height, in **voxels** of world Y |
| `Amplitude` | `24.0` | ± height variation, in voxels |
| `Frequency` | `0.010` | noise frequency per voxel |
| `Octaves` | `5` | fBm octaves (clamped to ≥ 1 at generate time) |
| `Lacunarity` | `2.0` | |
| `Gain` | `0.5` | |
| `Ridged` | `false` | ridged multifractal (mountains) vs fBm hills |
| `CaveThreshold` | `0.0` | 0 = no caves |
| `CaveFrequency` | `0.05` | |
| `DirtDepth` | `4` | voxels of dirt below the grass surface |
| `SandLevel` | `-1e9` | surface at or below this height is sand; off by default |
| `GrassBlock` / `DirtBlock` / `StoneBlock` / `SandBlock` | `1` / `2` / `3` / `4` | palette ids the generator writes |

Runtime, not reflected: `Ref<VoxelVolume> Volume`, `Ref<BlockPalette> Palette`,
`Ref<VoxelRenderData> Render`, and `std::size_t BuiltGenSignature`.

**There is no `Enabled` field.** Like `TerrainComponent`, `VoxelVolumeComponent` is gated only by
the entity-level `TagComponent::Active` (checked by the draw path).

### What `SyncVoxelVolumes` does, in order

`Scene::SyncVoxelVolumes(cameraPos)` runs at the top of both `Scene::OnRender3D` and
`Scene::BuildRenderDesc`, and Starforge's viewport calls it directly too. Per volume, per call:

1. **Palette** — if null, load `PalettePath`, else `CreateDefault()`. A failed load silently falls
   back to the default.
2. **Volume** — if null, create one and best-effort `Load(VolumePath)`.
3. **Placement** — `SetVoxelSize(VoxelSize)` and `SetOrigin(glm::vec3(WorldOf(entity)[3]))`.
4. **Render data + atlas** — create `VoxelRenderData` if missing; set its mesh mode from `Greedy`;
   rebuild the procedural atlas whenever `VoxelPaletteVersion(palette)` changes.
5. **Streaming generation** (only when `GenEnabled`) — see [below](#generating-a-world).
6. **Re-mesh** — drain the dirty set, cap it at **24 chunks per call** (leftovers are re-marked
   dirty for the next call), build each chunk's `MeshData` on the `JobSystem` (or inline if the job
   system is not initialized), then upload on the main thread. Empty results **erase** the chunk's
   mesh; every re-meshed chunk is added to `Render->CollisionDirty`.

> **Only the world *translation* is used.** Step 3 reads the fourth column of the entity's world
> matrix. Rotating or scaling the volume's entity does nothing — a voxel volume is axis-aligned
> world geometry, like terrain. Use `VoxelSize` for scale.

### How it draws

`Scene::SubmitOpaqueMeshes` walks every `VoxelVolumeComponent` last and submits **one draw per
uploaded chunk mesh** with the shared atlas material, all under the same
`translate(Origin) * scale(VoxelSize)` transform. Consequences worth knowing:

- Each chunk mesh carries its own AABB, so the render queue frustum-culls chunks for free.
- The entity's `Active` state is honoured (`IsActiveInHierarchy`), and the entity id is passed
  through, so voxel chunks write to the entity-ID buffer and are **click-pickable**
  (`ScenePicker.cpp:91` draws them in the ID pass too).
- Because this happens inside `SubmitOpaqueMeshes`, chunk meshes go through the routed
  `SceneDrawContext` — so voxel geometry **casts shadows** and contributes to the snow-coverage
  capture as well as drawing lit.
- Nothing draws when `Render->AtlasMaterial` is null, which happens only if
  `engine://shaders/PBR.glsl` fails to load.

### The procedural atlas

`BuildVoxelAtlas` paints a 16 px tile per palette tile index into an RGBA8 texture, sampled
**Nearest / ClampToEdge** so greedy-stretched quads show no bleeding. Per block it writes the side
tile at the block colour, the top tile at `colour × 1.12` (clamped) and the bottom at
`colour × 0.78` — cheap directional shading with no lighting cost. Tiles no block claims stay mid-
grey (`0.62`). The material is PBR with `Roughness 0.95`, `Metallic 0`, albedo map bound directly.

To use an **image** atlas instead, set the albedo map on `Render->AtlasMaterial` yourself after a
sync and switch the component to `Greedy = false`.

---

## Editing voxels from code

Scripts reach a voxel world through the `Voxels()` proxy on `ScriptableEntity` (3D builds only —
see [`scripting.md`](scripting.md)):

```cpp
uint16_t    Get(int x, int y, int z) const;
void        Set(int x, int y, int z, uint16_t block) const;
void        Set(const glm::ivec3& c, uint16_t block) const;
glm::ivec3  WorldToVoxel(const glm::vec3& p) const;
VoxelRayHit RayCast(const glm::vec3& origin, const glm::vec3& dir, float maxDist) const;
bool        Break(const glm::vec3& origin, const glm::vec3& dir, float maxDist) const;
bool        Place(const glm::vec3& origin, const glm::vec3& dir, float maxDist, uint16_t block) const;
```

**Which volume it edits:** this entity's `VoxelVolumeComponent` if it has one, otherwise **the first
one in the scene**. That fallback is what lets a digger script sit on the player's camera while the
world lives on a different entity. It also means the proxy is only unambiguous in a scene with one
voxel volume — with two, the "first" is whatever entt's iteration order produces.

Every call is a safe no-op (or a zero/empty result) before `SyncVoxelVolumes` has created the
volume, and when the scene has no voxel entity at all.

### The ray cast

`VoxelVolume::RayCast` is an Amanatides & Woo DDA march in world space:

```cpp
struct VoxelRayHit
{
    bool       Hit      = false;
    glm::ivec3 Voxel{ 0 };      // the solid cell that was hit
    glm::ivec3 Normal{ 0 };     // integer face normal, e.g. {0,1,0}
    glm::ivec3 Place{ 0 };      // Voxel + Normal — the empty cell to place into
    uint16_t   Block    = 0;
    float      Distance = 0.0f; // world metres from the ray origin
    glm::vec3  Point{ 0.0f };   // world hit point on the face
};
```

- The direction is normalized internally; a zero-length direction or `maxDistance <= 0` returns a
  miss immediately.
- **If the ray's origin is already inside a solid voxel**, that cell is reported with
  `Normal == {0,0,0}`, `Place == Voxel` and `Distance == 0`. A `Place` call in that state overwrites
  the block you are standing in — guard on `hit.Normal != glm::ivec3(0)` if that matters.
- `Distance` is in metres (the march works in voxel units and multiplies back by `VoxelSize`).
- Solidity comes from the palette, so a `Transparent` solid still stops the ray.

The step budget is `(maxDistance / VoxelSize) * 3 + 8` iterations, which is generous but finite — a
very long ray through a very small `VoxelSize` will stop early rather than hang.

### After an edit

An edit is just `Set`, so the sequence is: cell written → chunk (and seam neighbours) dirty →
**next** `SyncVoxelVolumes` re-meshes and uploads → the chunk lands in `CollisionDirty` → the **next
fixed physics step** rebuilds that chunk's static body. Visual and collision updates are therefore
one frame / one step behind the edit, and the mesh budget means a mass edit spanning more than 24
chunks spreads over several frames.

### The `VoxelDigger` sample script

Starforge generates `VoxelDigger.h` into every new project. It is the reference implementation:

```cpp
class VoxelDigger : public Cosmic::ScriptableEntity
{
public:
    float Reach      = 6.0f;    // max edit distance (world metres)
    int   PlaceBlock = 1;       // palette id RMB places
    float EyeHeight  = 1.6f;

protected:
    void OnUpdate(float) override
    {
        using namespace Cosmic;
        const glm::mat4 world = GetScene().GetWorldTransform(GetEntity());
        glm::vec3 origin = glm::vec3(world[3]);
        origin.y += EyeHeight;
        const glm::vec3 fwd = glm::normalize(glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));

        const bool lmb = Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_LEFT);
        const bool rmb = Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_RIGHT);
        if (lmb && !m_LmbWas) Voxels().Break(origin, fwd, Reach);
        if (rmb && !m_RmbWas) Voxels().Place(origin, fwd, Reach, (uint16_t)PlaceBlock);
        m_LmbWas = lmb;
        m_RmbWas = rmb;
    }
private:
    bool m_LmbWas = false, m_RmbWas = false;
};
```

Note the rising-edge latches — `Break`/`Place` on a held button would fire every frame.

> **The script and the editor brush use opposite mouse buttons.** In `VoxelDigger` (and the
> ForgeBlocks HUD hint) **LMB digs, RMB places**. In the editor's viewport brush **LMB places, RMB
> breaks**. Both are deliberate — in the editor's CAD navigation the camera is on MMB, so LMB is the
> "primary action" — but it surprises people who switch between Play and edit mode.

---

## Editing voxels in the editor

**View ▸ Voxels** (or Entity ▸ World ▸ Voxel Volume, which opens it) with a voxel entity selected.
The reflected recipe fields are edited in the Inspector like any component; the panel adds what a
raw Inspector cannot.

| Control | Effect |
| --- | --- |
| **Edit in viewport** | arms the brush: LMB places `ActiveBlock`, RMB breaks. Suppresses click-to-select while on. |
| **Reach** | brush ray length, 4–256 m (default 64) |
| **Palette** list | swatch + name per block id ≥ 1; click to set the painted block (default id 1) |
| **Greedy meshing** | flips the mode **and marks every resident chunk dirty** so the whole world re-meshes |
| **Chunks / Meshes** | resident chunk count and uploaded mesh count — the fastest sanity check |
| **Regenerate** | `Clear()`s the volume, drops all chunk meshes / `Generated` / `CollisionDirty`, and resets `BuiltGenSignature`. Streaming refills it *if* `GenEnabled` is on. |
| **Clear** | same, but also turns `GenEnabled` **off** so it stays empty |
| **Save .cvox** | writes the volume to `VolumePath`. **Only shown when `VolumePath` is non-empty.** |

The brush unprojects the mouse pixel through the viewport camera to a world ray (near-plane origin,
so it works for orthographic cameras too) and raycasts the grid. It is suppressed while a gizmo is
active or hovered.

Two panel states you will hit before anything works:

- *"Select a Voxel Volume entity (World ▸ Voxel Volume) to author it."* — no voxel entity is the
  primary selection.
- *"Initializing voxel world (render one frame)…"* — the entity exists but `SyncVoxelVolumes` has
  not run yet, so `Volume`/`Palette` are still null.

### Undo

Voxel edits go through `Commands::VoxelEdit`, which applies the change live and then **pushes**
(not executes) a `VoxelEditCommand` holding `{ voxel, oldId, newId }`. Undo replays the ops in
reverse. A no-op edit (same id) records nothing.

> **Every click is its own undo step, and the coalescing machinery never fires.** The command
> implements `MergeKey`/`TryMerge` keyed on a stroke counter, but `ViewportController` increments
> `ctx.VoxelBrush.Stroke` on **every** successful hit, so no two commands ever share a key. There is
> also no drag painting — only rising-edge clicks are handled. `VoxelPanel.h`'s "coalesced per brush
> stroke" comment and the panel tooltip's "Each drag is one undo step" both describe behaviour that
> does not exist.

---

## Generating a world

`VoxelGenerator::GenerateChunk(volume, chunk, recipe)` fills one chunk and marks it dirty. It is
deterministic in `(recipe.Seed, chunk)` — two runs with the same seed produce byte-identical output
(`test_voxel.cpp:302`) — because both noise fields are seeded off the recipe (`Seed` for height,
`Seed ^ 0x5bd1e995` for caves) and nothing else varies.

What it writes, per column:

1. A signed height sample — `Ridged2D` remapped to `[-1,1]` when `Ridged`, else `Fbm2D`.
2. `surface = SurfaceLevel + Amplitude × n`; the column is `shore` when `surface <= SandLevel`.
3. Everything above `floor(surface)` stays air. The surface voxel is `SandBlock` (shore) or
   `GrassBlock`; the next `DirtDepth` voxels are `SandBlock` (shore) or `DirtBlock`; below that,
   `StoneBlock`.
4. If `CaveThreshold > 0`, cells more than one voxel below the surface are hollowed where
   `|Fbm3D(...)| > 1 - CaveThreshold`. **Higher `CaveThreshold` means more cave**, and 0 disables it
   entirely.

`VoxelGenerator::Signature(recipe)` hashes every parameter; `SyncVoxelVolumes` compares it against
`BuiltGenSignature` to notice a recipe change.

### Streaming

With `GenEnabled` on, each `SyncVoxelVolumes` call walks Chebyshev-distance shells outward from the
camera's chunk (`ChunkCoord(WorldToVoxel(cameraPos))`) up to `ViewRadius`, generating the first
chunks it finds that are neither in `Render->Generated` nor already resident. The budget is **2
chunks per call**, so streaming spreads over frames and never hitches; after each new chunk, the six
face-neighbours that already exist are marked dirty so their shared seams re-cull.

> **Keep `ViewRadius` modest.** The shell walk iterates the full `(2r+1)³` cube per radius and
> skips non-shell cells with a `continue`, so one call is `Σ(2r+1)³ = (R+1)²(2(R+1)²−1)` iterations
> — roughly `2·(ViewRadius+1)⁴`. The budget early-out only fires while chunks are still being
> generated, so a *fully populated* neighbourhood — the steady state, when there is nothing left to
> do — pays the full scan **every frame**. At the default 8 that is ~13 000 map lookups per frame
> (fine); at the reflected maximum of 64 it is ~36 million (not fine).

### Two ways to build a world

**Endless** — `GenEnabled = true`, leave `VolumePath` empty, let it stream. This is what
Entity ▸ World ▸ Voxel Volume gives you.

**Bounded and pre-baked** — generate the whole world once, save it, and load it from the scene. This
is what ForgeBlocks does, and the reason is collision: a pre-baked `.cvox` is fully resident the
moment the scene loads, so `OnPhysicsStart` builds every chunk body up front and the player has
ground to stand on the instant Play starts.

```cpp
// From StarforgeApp::BuildForgeBlocks — an 8×2×8-chunk island (256 × 64 × 256 voxels).
Cosmic::VoxelGeneratorRecipe recipe;
recipe.Seed          = 20260708u;
recipe.SurfaceLevel  = 24.0f;
recipe.Amplitude     = 9.0f;
recipe.Frequency     = 0.02f;
recipe.Octaves       = 5;
recipe.CaveThreshold = 0.30f;
recipe.CaveFrequency = 0.045f;
recipe.DirtDepth     = 4;
recipe.SandLevel     = 18.0f;

auto vol = Cosmic::VoxelVolume::Create();
vol->SetVoxelSize(1.0f);
for (int cx = -4; cx < 4; ++cx)
    for (int cz = -4; cz < 4; ++cz)
        for (int cy = 0; cy < 2; ++cy)
            Cosmic::VoxelGenerator::GenerateChunk(*vol, { cx, cy, cz }, recipe);

std::vector<glm::ivec3> drained;
vol->TakeDirtyChunks(drained);                 // we are saving these, not meshing them
vol->Save("project://voxels/world.cvox");
```

The scene entity then mirrors the recipe with `GenEnabled = false`, so the island is the whole
world — and pressing **Regenerate** after flipping `GenEnabled` on reproduces the same terrain,
because the recipe fields match the bake.

> **Changing the generator recipe does not re-terrain an existing world.** The signature check
> clears `Render->Generated`, but the streaming loop *also* skips any chunk where
> `Volume->HasChunk(cc)` is true — and generating a chunk always makes it resident. So a recipe
> change only ever affects chunks that have never been generated. Use the Voxels panel's
> **Regenerate** (which calls `Volume->Clear()`) to actually rebuild. The `rd.Generated.clear()`
> comment claiming it re-terrains untouched chunks is wrong.

---

## Voxel collision

Collision is **one static Jolt mesh body per resident chunk**, and it exists only while a physics
session is running. There is no voxel collision in the editor outside Play.

**At `Scene::OnPhysicsStart`**, after the ordinary collider bake, `ScenePhysics::BuildVoxelBodies`
walks every `VoxelVolumeComponent`, builds `VoxelMesher::BuildCollision` geometry for each resident
chunk, and creates a body per non-empty chunk. `Render->CollisionDirty` is cleared afterwards
(those chunks were just built).

Each chunk body is:

- `MotionType::Static`, positioned at the world origin with identity rotation — the volume's
  `Origin` and `VoxelSize` are **baked into the vertices** (`origin + v.Position * voxelSize`)
  rather than applied as a body transform, the same way terrain is handled.
- `Friction = 0.8`.
- Tagged with the volume entity's UUID (`IDComponent::ID.Value()`), so a raycast or contact
  event reports the voxel entity — not a per-chunk identity.

**Every fixed step**, `ScenePhysics::Step` first calls `RebuildDirtyVoxelChunks`, which drains
`Render->CollisionDirty`, destroys and recreates up to **8 chunk bodies per step**, and re-queues
the remainder. So digging updates collision within one fixed step, which
`tests/test_voxel_collision.cpp:86` verifies end to end: dig a column, mark the chunk, step once,
and a ray that previously stopped at the surface now falls through.

`OnPhysicsStop` destroys every chunk body.

Because the collision mesh merges across block types and ignores the render tile split, it is
cheaper than the render mesh — and because it is a triangle mesh shape, it is static-only. Dynamic
bodies and character controllers collide with it; you cannot make a voxel volume dynamic.

**Walking on voxels.** ForgeBlocks pairs the volume with a `CharacterControllerComponent` sized
`Height 1.8`, `Radius 0.35`, `StepHeight 0.6` — a step height above 0.5 is what lets a character
walk up single 1 m voxels without jumping. See [`physics.md`](physics.md) for the controller.

**Navmesh baking** reads voxels too: `SceneNav` appends each resident chunk's collision geometry to
the bake soup (`SceneNav.cpp:205`), filtered by `IsActiveInHierarchy` and the navmesh's scope. See
[`navigation-and-ai.md`](navigation-and-ai.md).

---

## Common patterns

**One volume per scene.** The `Voxels()` proxy's "first volume in the scene" fallback, the Voxels
panel's single-selection model and the picker's per-entity id all assume it. Multiple volumes render
and collide correctly, but scripts have to fetch the component by hand:

```cpp
auto& reg = GetScene().GetRegistry();
for (auto e : reg.view<Cosmic::VoxelVolumeComponent>())
{
    auto& vc = reg.get<Cosmic::VoxelVolumeComponent>(e);
    if (vc.Volume) vc.Volume->Set(x, y, z, blockId);
}
```

**Batch edits, then let one sync catch up.** `Set` coalesces dirt per chunk, so carving a 10×10×10
region is one dirty chunk (or a handful across seams), not 1000 rebuilds.

**Read the world without a ray.** `Voxels().WorldToVoxel(p)` then `Get` is the cheap "what am I
standing on" query, and it works with no physics session.

**Reading chunk stats from code** needs the extra include:

```cpp
#include "voxel/VoxelRender.h"
const auto& vc = e.GetComponent<Cosmic::VoxelVolumeComponent>();
const size_t chunks = vc.Volume ? vc.Volume->ChunkCount() : 0;
const size_t meshes = vc.Render ? vc.Render->ChunkMeshes.size() : 0;
```

---

## Pitfalls

**"The Voxels panel says *Initializing voxel world*."** The runtime `Volume`/`Palette` come online
in the first `SyncVoxelVolumes`, which runs during render. Let one frame draw.

**"Nothing renders, and the log is clean."** In order of likelihood: the entity (or an ancestor) is
inactive; the volume is empty because `GenEnabled` is off and `VolumePath` is empty or missing; or
`engine://shaders/PBR.glsl` failed to load, leaving `AtlasMaterial` null. A missing `.cvox` is
**silent** by design — check `ChunkCount` in the panel.

**"My generator changes do nothing."** Already-generated chunks are never re-generated. Press
**Regenerate**. See the note above.

**"Regenerate emptied my world and it never came back."** `Regenerate` clears the volume and relies
on streaming to refill. With `GenEnabled` off, nothing refills it — that is what the separate
**Clear** button is for. Reload the scene (or the `.cvox`) to get a pre-baked world back.

**"The volume isn't where I put the entity."** Only the entity's world *translation* is read.
Rotation and scale are ignored. Also: parenting works (the translation comes from the composed world
matrix), but a rotated parent will translate the volume without rotating it.

**"Moving the volume during Play breaks collision."** The chunk bodies bake the volume's origin into
their vertices at build time, and nothing marks chunks dirty when the transform changes. The visuals
follow the entity; the collision does not. Treat a voxel volume's placement as authoring-time data.

**"Textures smear across large flat areas."** Greedy meshing stretches one tile rect over the merged
quad. Set `Greedy = false` (Voxels panel ▸ *Greedy meshing*) for an image atlas — at a large vertex
cost.

**"There's no Save button."** *Save .cvox* only appears when `VolumePath` is set. Set it in the
Inspector first.

**"My saved world loads at the wrong scale."** The `.cvox` carries `Origin`/`VoxelSize`, but a
component-owned volume has both overwritten every sync. Set them on the component.

**"A failed `.cvox` load left my volume empty."** `LoadFromBuffer` clears before it decodes and
bails on the first malformed record. Load into a scratch `VoxelVolume` and only swap on success.

**"`ChunkCount` grows even though I only placed air."** It doesn't — clearing air in a non-resident
region allocates nothing. But `EmplaceChunk` (used by the generator) *does* create an all-air chunk
that counts, and an all-air chunk meshes to nothing.

**"Editing feels one frame behind."** It is. Render updates at the next `SyncVoxelVolumes`,
collision at the next fixed step, and both have per-call budgets (24 chunks meshed, 8 chunk bodies
rebuilt).

**"Frame time collapses when I raise `ViewRadius`."** The streaming shell walk is ~`2·(R+1)⁴`
iterations per frame once the neighbourhood is full. Keep it near the default.

---

## See also

- [`entities-and-components.md`](entities-and-components.md) — the component catalogue,
  `Active`/`Enabled` gates, and what a 2D build sees
- [`rendering-3d.md`](rendering-3d.md) — the queue chunk meshes are submitted into: culling,
  sorting, statistics
- [`materials-and-shaders.md`](materials-and-shaders.md) — `Material`, the PBR uniform contract, and
  swapping the atlas material's albedo map
- [`physics.md`](physics.md) — static bodies, the character controller, queries
- [`navigation-and-ai.md`](navigation-and-ai.md) — baking a navmesh over voxel geometry
- [`scripting.md`](scripting.md) — `ScriptableEntity`, all eight proxies, and which are fenced
- [`assets-and-vfs.md`](assets-and-vfs.md) — `project://` paths and where sidecar assets live
- [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md) — why `voxel/` is 3D only
- `tests/test_voxel.cpp`, `tests/test_voxel_collision.cpp` — the executable specification for
  everything above
