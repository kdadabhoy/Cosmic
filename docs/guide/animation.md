# Animation — Guide

**What this covers:** skeletal animation end to end — `Skeleton` and `AnimationClip`, importing
skinned meshes (glTF/GLB via cgltf, FBX/DAE via assimp), `AnimatorComponent`, playing and scrubbing
clips, `CrossfadeTo` and the pose-space blend model, **joint sockets** (attaching entities to
animated joints), and how GPU skinning actually works — the binding-10 palette SSBO and its shadow
twin.
**Source of truth:** `Cosmic/src/graphics/Skeleton.{h,cpp}`, `graphics/AnimationClip.{h,cpp}`,
`graphics/Mesh.h` (`SkinVertex`, `CreateSkinned`), `scene/Components3D.h`
(`AnimatorComponent`, `SocketComponent`), `scene/Scene3D.cpp` (`UpdateAnimators`,
`FindAnimatorFor`, `SubmitOpaqueMeshes`), `scene/Scene.cpp` (`WorldOf`'s socket override),
`renderer/Renderer3D.cpp`, `renderer/ShadowMap.cpp`, `renderer/SceneRenderer.cpp`,
`renderer/BindingPoints.h`, `assets/MeshImport.cpp`, `assets/AssetLibrary.cpp`,
`reflect/TypeRegistry3D.cpp`, `Cosmic/assets/shaders/PBRSkinned.glsl`,
`Projects/Starforge/src/editors/AnimationEditor.cpp`,
`Projects/Starforge/src/panels/InspectorPanel.cpp`, `tests/test_animation.cpp`,
`tests/test_crossfade.cpp`, `tests/test_sockets.cpp`
**API Reference:** *none — `graphics/Skeleton.h` and `graphics/AnimationClip.h` have **no row** in
the [reference manifest](../reference/README.md), so this chapter is the client-facing source.* ·
**How it works:** *none — there is no `docs/systems/` explainer for skeletal animation either.*
**Configuration:** **3D only.** `AnimatorComponent` and `SocketComponent` live in
`scene/Components3D.h`, `Scene::UpdateAnimators` is compiled in `scene/Scene3D.cpp`, and the
`Animator()` script proxy sits inside `#ifndef COSMIC_2D_ONLY`. `Skeleton.h` and `AnimationClip.h`
themselves are pulled in *by* `Components3D.h`, so they are absent from a 2D tree too. Naming any of
it in a `COSMIC_2D_ONLY` build is a compile error, by design — see
[`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md).

> **Parked: the controller graph.** Blend trees and animation state machines are an explicit
> non-feature. The `FEATURE-MATRIX` row *"Animation blend trees / state machines (full controller
> graph + editor)"* is ✖ with no phase assigned. What ships is **one clip per animator** plus a
> **timed crossfade to a next clip** — deliberately the minimal tier a playable character needs
> (idle ↔ walk ↔ run). Everything in this chapter is that tier; nothing here is a partial
> controller graph waiting to be finished.

Six pieces make up the whole system, and it pays to know which one you are touching:

| Piece | What it is | Lives on | GL? |
| --- | --- | --- | --- |
| `Skeleton` | joint tree: name, parent, bind local, inverse-bind, plus the import-correction matrix | the `Mesh` (`GetSkeleton()`) | no |
| `AnimationClip` | one named clip: per-joint TRS keyframe channels + `Duration` | `AssetLibrary`'s clip-set cache | no |
| skinned `Mesh` | ordinary geometry **plus** a second vertex buffer of joints/weights at attribute locations 4/5 | the ECS `MeshRendererComponent` | yes |
| `AnimatorComponent` | play head, clip path, and the per-frame **palette** it publishes | an entity at or above the mesh | no |
| `SocketComponent` | "follow joint *N* of my animated ancestor" | any descendant of the rig | no |
| binding-10 SSBO | every skinned draw's palette for the frame | `Renderer3D` (and `ShadowMap`) | yes |

The data flow is one line, and it is worth memorising:

```
locals  = clip.Sample(skeleton, t, loop)      // per-joint parent-relative transforms
palette = skeleton.ComputePalette(locals)     // per-joint skinning matrices  -> SSBO 10
joints  = ImportCorrection * globals          // per-joint model frames       -> sockets, overlay
```

`Scene::UpdateAnimators` runs all three every frame and writes the results into the component.

---

## Quick start

### Animate an imported model in the editor

1. **File ▸ Import Model…**, point it at a `.glb` / `.gltf` / `.fbx` / `.dae`, and hit **Import**.
   The rich import copies the source into `project://models/`, writes a `.cmat` per material,
   spawns the entity (or a parent plus one child per sub-mesh), assigns the materials, and — if the
   file has clips — **adds an `AnimatorComponent` with its first clip already set**.
2. Select the entity. The Inspector's **Animator** section has a **Clip** combo listing every clip
   inside the source file; picking one writes `"file#ClipName"` through the undo stack.
3. It plays immediately — animators advance in **edit mode** too, not just in Play.
4. To pose it by hand, untick **Playing** and drag **NormalizedTime**.

> **Use the import popup, not a viewport drop.** Dragging an already-imported model from the
> Content Browser into the viewport spawns an entity with **only** `MeshRendererComponent::MeshPath`
> set — no material and no animator (`ViewportController.cpp:1601`). A skinned mesh with no
> material draws its bind pose forever and logs nothing. Add an **Animator** and a **Material** in
> the Inspector, or re-run the import.

### Animate one in code

```cpp
Cosmic::Entity fox = scene->CreateEntity("Fox");
{
    auto& mr = fox.AddComponent<Cosmic::MeshRendererComponent>();
    mr.MeshPath     = "project://models/Fox.glb";
    mr.MaterialPath = "project://materials/Fox.cmat";   // REQUIRED — see the pitfall below

    auto& an = fox.AddComponent<Cosmic::AnimatorComponent>();
    an.ClipPath = "project://models/Fox.glb#Run";       // "file#name", "file#index", or bare "file"
    an.Speed    = 1.0f;
    an.Loop     = true;
    an.Playing  = true;
}
```

Nothing else is needed: `Scene::SyncPrimitiveMeshes` resolves the mesh and material,
`Scene::UpdateAnimators` samples the clip, and `Scene::SubmitOpaqueMeshes` routes the draw through
the skinned shader.

### Switch clips from a script

```cpp
void OnUpdate(float) override
{
    const bool moving = glm::length(Character().GetVelocity()) > 0.1f;
    Animator().CrossfadeTo(moving ? "project://models/Fox.glb#Run"
                                  : "project://models/Fox.glb#Idle", 0.2f);
}
```

`CrossfadeTo` is idempotent — calling it every frame with the clip already playing cancels nothing
and starts nothing.

---

## Importing a rigged model

Skins and clips ride the ordinary model import (see [`assets-and-vfs.md`](assets-and-vfs.md) for the
import pipeline as a whole). What matters for animation:

| Format | Backend | Skins | Clips |
| --- | --- | --- | --- |
| `.gltf` / `.glb` | cgltf (always available) | ✅ skin 0 | ✅ |
| `.fbx`, `.dae` | assimp (`COSMIC_WITH_ASSIMP`) | ✅ | ✅ |
| `.obj` | the engine's own parser | ✖ never | ✖ |
| `.stl`, `.ply` | assimp | ✖ | ✖ |

The Content Browser knows the difference: `.gltf`, `.glb`, `.fbx` and `.dae` are registered as
*riggable* and offer **Open in Animation Editor**; `.obj`, `.stl` and `.ply` are not.

### One skeleton per file

- **glTF** imports **skin 0** only. A file with more skins logs
  `"'X' has N skins — only skin 0 imports (A2 v1)"`, and a mesh bound to a different skin imports
  statically with its own warning.
- **assimp** builds the closure of every mesh's bone nodes into one skeleton, so multi-mesh FBX rigs
  that share a hierarchy come through as one.

Joint order is whatever the importer discovered (glTF: the skin's joint array; assimp: discovery
order), and vertex joint indices index that array directly. Parents need **not** precede children —
`Skeleton::ComputeGlobals` resolves any ordering with a memoized walk.

### The `.cmeta` scale and `ImportCorrection`

Imported geometry has the `.cmeta` unit-scale and up-axis matrix **M** baked into its vertices. Joint
animation is authored in the source's own model space, so the palette has to be conjugated:

```
palette' = M · (global · inverseBind) · M⁻¹
```

The importer stores `M` and `M⁻¹` on the skeleton (`ImportCorrection` / `ImportCorrectionInv`) and
`ComputePalette` applies it. The practical consequence: **re-importing at a different scale is
safe** — geometry and animation rescale together, and at the bind pose every palette entry is
conjugated identity, i.e. identity, so a skinned mesh with no animator renders exactly as it
imported.

### Clip conversion details

- **Names.** glTF uses the animation's name, falling back to `Clip_<i>`; assimp likewise. These are
  the strings the `#fragment` matches and the Inspector combo lists.
- **Duration.** glTF derives it as the **maximum key time** across the clip's samplers (it is not
  read from the file); assimp converts `mDuration / mTicksPerSecond`, defaulting to 25 ticks/s when
  the file does not say.
- **Interpolation.** glTF `LINEAR` and `STEP` both import as plain keyframes (STEP is approximated
  as linear); `CUBICSPLINE` imports the middle value of each in/value/out triple and drops the
  tangents.
- **What is dropped.** Channels targeting nodes that are not skin joints are ignored. Morph-target
  weight channels are out of scope. **A clip left with no channels is discarded entirely** — so a
  file whose animations only move non-joint nodes yields *no clips at all*, and
  `AssetLibrary::GetAnimationClip` warns `"'X' contains no animation clips."`

### Merged vs per-sub-mesh, and the all-or-nothing skin rule

`AssetLibrary::GetMesh("file")` merges the whole file into one mesh; `"file#3"` imports one
sub-mesh. On the merged path a skin survives **only when every sub-mesh is skinned** — mixing a
static prop into one skinned vertex buffer would give it bogus joint-0 influences, so a mixed file
imports as a static mesh instead. Import such a file per sub-mesh (the editor's multi-mesh spawn
does this automatically) and let the animator sit on the parent.

Weights are capped at the strongest **4 influences per vertex** (assimp replaces the weakest when a
stronger one arrives) and are renormalized **in the shader**, not at import.

---

## Playing a clip: `AnimatorComponent`

| Field | Default | Reflected | Meaning |
| --- | --- | --- | --- |
| `ClipPath` | `""` | ✅ `AssetPath("animation")` | `"file#clip"` — clip **name**, clip **index**, or a bare file (its first clip) |
| `Speed` | `1.0` | ✅ range −4…4 | playback rate; negative plays backwards |
| `Loop` | `true` | ✅ | wrap by `Duration` vs clamp to the ends |
| `Playing` | `true` | ✅ | advance the play head |
| `NormalizedTime` | `0.0` | ✅ range 0…1 | the play head. Written while playing; **authoritative while paused** |

Everything else on the component is runtime-only and never serialized: `ClipRef`,
`ResolvedClipPath`, `SkelRef`, `Palette`, `JointModelMatrices`, the scratch buffers, `TimeSeconds`,
and the whole crossfade block.

### Where the animator goes

The animator does **not** have to be on the mesh entity. Two independent searches connect them:

- **Down**, to find the skeleton: `UpdateAnimators` walks the animator entity **and its
  descendants** for the first `MeshRendererComponent` whose mesh `IsSkinned()`, and takes that
  mesh's skeleton. This is why a multi-mesh import can hang one animator on the parent.
- **Up**, at draw time: `FindAnimatorFor(entity)` walks from the mesh entity **up** the parent chain
  (bounded at 64 levels) for the first `AnimatorComponent`.

Put the animator on the root of the rig and both searches meet in the middle.

`SkelRef` is retried every frame until it resolves, because a freshly loaded scene resolves meshes a
frame later than it creates entities.

### Who ticks it

| Host | When |
| --- | --- |
| `PlayerLayer` (`PlayerLayer.cpp:261`) | every frame while the app is **not paused** |
| Starforge, Play mode (`StarforgeApp.cpp:809`) | after `ScriptHost::Tick`, so a `CrossfadeTo` issued this frame lands this frame |
| Starforge, **edit mode** (`StarforgeApp.cpp:1321`) | every frame — rigs animate in the editor viewport |

Two consequences. First, **pausing freezes the pose** in both hosts (neither calls
`UpdateAnimators` while paused) — that is what makes scrubbing work. Second, skeletal animation
behaves differently from sprite flipbooks: `Scene::UpdateSpriteAnimations` is only called in Play,
so 2D flipbooks are frozen in the editor while skinned rigs are not.

`Scene::OnUpdate` also calls `UpdateAnimators`, but that method has **no callers** in the engine,
the sample projects or the tests — every host ticks the animator directly. Don't rely on it.

### The play head

While `Playing`:

```
TimeSeconds   += deltaTime * Speed;                 // may run backwards
if (!Loop)     TimeSeconds = clamp(TimeSeconds, 0, Duration);
NormalizedTime = ResolveTime(TimeSeconds, Loop) / Duration;
```

While paused, the relationship inverts — `TimeSeconds = NormalizedTime * Duration` — which is
exactly why dragging the Inspector's `NormalizedTime` slider re-poses the mesh live.

`ResolveTime` wraps into `[0, Duration)` when looping (correctly for negative times) and clamps to
`[0, Duration]` when not. `Duration == 0` always resolves to 0.

### Sampling

`AnimationClip::Sample(skeleton, t, loop, outLocals)`:

- seeds `outLocals` from the skeleton's **bind locals**, so a joint with no channel holds its bind
  pose;
- per channel, samples position and scale with linear interpolation and rotation with a
  **shortest-arc slerp** (negating one quaternion when the dot product is negative);
- clamps outside the key range rather than extrapolating;
- skips channels whose `JointIndex` is out of range.

A channel that animates *nothing* leaves the bind local untouched. A channel missing one track falls
back to neutral values for that part (bind translation, identity rotation, unit scale) — in practice
theory only, since both importers emit full TRS for animated joints.

`BakeFixedRate(hz)` resamples every channel onto keys at `0, 1/hz, 2/hz, … Duration` and returns a
new clip; the source is untouched. Reach for it when authored keys are pathologically sparse or
dense. `hz <= 0` or `Duration <= 0` returns a copy.

### Failure modes

| Situation | What happens |
| --- | --- |
| `ClipPath` names a missing clip | `AssetLibrary` warns `"clip 'X' not found in 'Y' (N clip(s))"`, `ClipRef` stays null |
| the file has no clips | warns `"contains no animation clips"` |
| the file fails to parse | `CS_CORE_ERROR`, **not cached**, so the next request retries |
| `ClipRef` null but a skeleton resolved | `Palette` is cleared → **static bind-pose draw**, and joint frames are still published from the bind pose (sockets keep working) |
| no skinned mesh under the animator | `Palette` and `JointModelMatrices` are both cleared |
| `Palette.size() != skeleton.JointCount()` | the draw falls back to the static path silently |

The theme: **nothing about a broken animator stops the mesh drawing.** It draws in its bind pose.
"My model renders but never moves" is almost always one of the rows above.

### Clip paths and the cache

`AssetLibrary` parses a model file's clips **once** and caches the whole set keyed by the base path;
`GetAnimationClip` returns a `Ref<AnimationClip>` aliased into that vector, so the set stays alive as
long as any clip does. `GetAnimationClipNames(path)` lists them — that is what the Inspector combo
and the Animation Editor use. An empty clip set **is** cached ("this file has no clips" is a valid
answer); a parse failure is not.

---

## Scrubbing and inspecting in the editor

**Inspector.** With one entity selected, the Animator section renders the reflected fields plus a
**Clip** combo. The combo finds the driven model file by walking this entity and its descendants for
a `MeshRendererComponent` with a non-empty `MeshPath` (stripping any `#submesh` fragment), lists the
clips inside it, and writes `"<file>#<name>"` through `Commands::SetField` so the change is undoable.
The clip set is loaded only while the combo is open, so a broken source cannot spam the log every
frame. When a clip is resolved its duration is shown beside the combo.

**Animation Editor.** Double-click a `.gltf`/`.glb`/`.fbx`/`.dae` in the Content Browser, or
right-click it ▸ **Open in Animation Editor**. It opens as a tabbed document in the editor host
(**View ▸ Editors (Animation / Flow / Story)**), with four panes:

- **Skeleton tree** (left) — every joint, parented, with the joint count.
- **Preview** (centre) — an interactive `PreviewRig` with its own framebuffer, a bone overlay drawn
  from the posed joint frames, orbit-drag, and click-to-select-nearest-joint.
- **Joint details** (right) — name, index, parent, bind local translation, posed model position, and
  a **Socket target** section with a *Copy joint name* button.
- **Timeline** (bottom) — clip selector plus one track per animated joint, with transport and scrub.

It is **inspect-only by design**: it plays, scrubs and inspects. It does not author joint transforms
or clips, and it reports `Dirty() == false` always because there is nothing to save.

---

## Crossfading between clips

`AnimatorComponent::CrossfadeTo(clipPath, seconds)` sets intent; `UpdateAnimators` does the work
(it owns the `AssetLibrary` access).

| Call | Effect |
| --- | --- |
| `CrossfadeTo("", …)` | cancels any pending fade |
| `CrossfadeTo(currentClip, …)` | cancels any pending fade — **not** a restart |
| `CrossfadeTo(other, 0)` or negative | hard switch: `ClipPath = other`, `NormalizedTime = 0` |
| `CrossfadeTo(other, 0.25f)` | starts (or **re-targets** an in-flight) fade over 0.25 s |

During a fade both play heads advance at `Speed` and `FadeElapsed` accumulates — **only while
`Playing`**. The pose is `AnimationClip::BlendLocals(currentLocals, nextLocals, w)` with
`w = clamp(FadeElapsed / FadeDuration, 0, 1)`. When `w` reaches 1 the next clip is **promoted**:
`ClipPath`, `ClipRef`, `ResolvedClipPath` and `TimeSeconds` all take the next clip's values and the
fade state resets.

`BlendLocals` blends in **pose space, per joint**: each local is decomposed into translation, a
rotation quaternion (from the scale-normalized 3×3) and scale; translation and scale mix linearly,
rotation slerps the shortest arc; the parts are recomposed as `T · R · S`. It is pure, static and
headless-tested, so you can call it directly on your own pose arrays. `out` may alias `a`. It
processes `min(a.size(), b.size())` joints and grows `out` if needed — it never shrinks it.

### From a script

The `Animator()` proxy drives **this entity's** animator (no ancestor search):

```cpp
void CrossfadeTo(const std::string& clipPath, float seconds) const;
void Play(const std::string& clipPath) const;   // == CrossfadeTo(path, 0)
void SetPlaying(bool) const;
bool IsCrossfading() const;                     // true while NextClipPath is set
std::string CurrentClip() const;
```

All calls are no-ops when the entity has no `AnimatorComponent`. Note `IsCrossfading()` reports
"a fade is pending **or** in flight" — it is true from the moment `CrossfadeTo` is called, before
`UpdateAnimators` has resolved the target.

Because Starforge and `PlayerLayer` both tick scripts **before** animators, a `CrossfadeTo` issued in
`OnUpdate` takes effect the same frame.

---

## Attaching things to joints: sockets

A `SocketComponent` makes an entity follow a named joint of an animated ancestor:

```cpp
struct SocketComponent
{
    std::string Joint;                              // target joint NAME, e.g. "hand.r"
    glm::vec3   Position{ 0.0f };                   // offset from the joint (metres)
    glm::quat   Rotation{ 1.0f, 0.0f, 0.0f, 0.0f }; // offset rotation (w, x, y, z)
    glm::vec3   Scale{ 1.0f };
};
```

`Scene::GetWorldTransform` composes:

```
socketWorld = ancestorWorld · jointFrame · (T(Position) · R(Rotation) · S(Scale))
```

`jointFrame` is `AnimatorComponent::JointModelMatrices[j]` — `ImportCorrection · global`, **without**
the inverse-bind — so an entity placed at the joint frame sits *on* the joint. (The skinning palette
is a different thing; do not confuse the two.)

**Resolution.** From the socket entity, walk up the parent chain (bounded at 4096 levels) for the
first ancestor carrying an `AnimatorComponent` with a resolved skeleton and non-empty
`JointModelMatrices`, whose skeleton contains a joint of that name.

**When it resolves, the entity's own `TransformComponent` is bypassed entirely** — the offset lives
on the socket. When it does not resolve — no animated ancestor, an unknown joint name, or a rig that
has not posed yet — the entity falls back to the ordinary parent-relative transform. That fallback is
the compat guarantee: a socket behaves as a plain child until its rig comes online, and entities
without the component are untouched.

Sockets work even with **no clip**: a rig with a skeleton and no `ClipRef` still publishes bind-pose
joint frames, so a sword attached to `hand.r` sits correctly on an un-animated model.

Worked example, straight out of `tests/test_sockets.cpp` — a two-joint chain whose tip is bound 1 m
above the root, with a clip lifting the tip's local translation from `(0,1,0)` to `(0,3,0)` over 2 s:

```cpp
Cosmic::Entity rig = scene.CreateEntity("Rig");
rig.GetComponent<Cosmic::TransformComponent>().Position = { 5.0f, 0.0f, 0.0f };
auto& an   = rig.AddComponent<Cosmic::AnimatorComponent>();
an.Playing = false;     // the scrubbed NormalizedTime is authoritative
an.Loop    = false;     // clamp at the ends

Cosmic::Entity sword = scene.CreateEntity("Sword");
auto& sc    = sword.AddComponent<Cosmic::SocketComponent>();
sc.Joint    = "tip";
sc.Position = { 0.0f, 0.0f, 1.0f };
scene.SetParent(sword, rig, /*keepWorldPose*/ false);

an.NormalizedTime = 0.5f;          // t = 1 s -> tip local (0,2,0)
scene.UpdateAnimators(0.0f);
// world(sword) == (5, 2, 1)
```

The offset's translation is rotated by the **joint's** rotation, not by the socket's own — the
socket rotation only reorients the attached entity's axes. `test_sockets.cpp:108` pins both halves.

**Authoring flow.** Open the rig in the Animation Editor, click the joint, hit **Copy joint name**,
then add a **Socket** component (Inspector ▸ Add Component) to a **child of the rig** and paste the
name. All four fields are ordinary reflected kinds, so they serialize and undo for free.

**Cost.** `Skeleton::Find` is a linear name scan run on every `GetWorldTransform` of a socketed
entity, and the resolution walk recurses into `WorldOf(parent)`. Fine for a handful of props on a
character; not something to put on hundreds of entities per frame.

---

## How the GPU actually skins

You do not have to call any of this — `SubmitOpaqueMeshes` does — but knowing the shape explains
every failure mode.

**The mesh.** `Mesh::CreateSkinned(data, skin, skeleton)` uploads the canonical vertex layout plus a
**second vertex buffer** of `SkinVertex { vec4 Joints; vec4 Weights; }` at attribute locations
**4** and **5**. Joint indices are stored as **floats** (the VAO layer's attribute pointers are
float-typed; float32 is exact well past any realistic joint count) and the shader rounds them back
to `int`. Sizes that disagree fall back to a static mesh with a warning. Static shaders simply never
consume locations 4/5, so a skinned mesh renders fine through the ordinary path.

**The palette.** `Skeleton::ComputePalette(locals, out)` accumulates locals into model-space globals
and produces `ImportCorrection · global · inverseBind · ImportCorrectionInv` per joint. A `locals`
array of the wrong size logs a warning and yields all-identity.

**The upload.** `Renderer3D::DrawMeshSkinned(mesh, transform, material, palette, jointCount, id)`
**copies** the palette into a per-scene staging vector and records the copy's base index on the draw
command. At `Flush`, the whole staging array is uploaded into one std430 SSBO at
**`Bindings::SkinningSsbo == 10`** — a single upload for every skinned draw in the scene — and each
draw sets `u_SkinBase` to its own offset. The buffer grows geometrically and is only recreated when
exceeded. Staging is cleared after `Flush`.

`PBRSkinned.glsl`'s vertex stage is the whole contract:

```glsl
layout(location = 4) in vec4 a_Joints;
layout(location = 5) in vec4 a_Weights;
layout(std430, binding = 10) readonly buffer SkinPalette { mat4 u_Palette[]; };
uniform int u_SkinBase;

float wsum = a_Weights.x + a_Weights.y + a_Weights.z + a_Weights.w;
vec4  w    = (wsum > 1e-6) ? a_Weights / wsum : vec4(1.0, 0.0, 0.0, 0.0);
mat4  skin = w.x * u_Palette[u_SkinBase + int(a_Joints.x + 0.5)] + /* … */;
vec4  world = u_Model * (skin * vec4(a_Position, 1.0));
```

Everything past the vertex stage matches `PBR.glsl` exactly — the two fragment stages are kept in
sync by hand, the same convention `PBRInstanced.glsl` follows.

**The shadow twin.** `ShadowDepthSkinned.glsl` is the deforming-shadow counterpart, driven by
`ShadowMap::DrawCasterSkinned`. It keeps its **own** binding-10 buffer and uploads **per caster at
base 0** — the lit pass's `Renderer3D` upload happens later in the frame and re-binds the slot, so
the two never fight. If the shader fails to load, casters fall back to their bind pose with an error
logged once.

**The coverage pass** (top-down snow accumulation) deliberately draws the **bind pose** — coverage
tolerance is metres-scale, so animation-scale deformation is noise there.

**Routing.** `SceneDrawContext::DrawMeshSkinned` picks the destination by pass:
`Main`/`Reflection` → `Renderer3D::DrawMeshSkinned`; `ShadowDepth` →
`ShadowMap::DrawCasterSkinned`; `TopDownDepth` → the bind-pose coverage caster.

**Fallbacks.** `Renderer3D::DrawMeshSkinned` draws statically — no warning — when the palette is
null, `jointCount` is 0, or the material has **no skinned twin**. Calling it outside
`BeginScene`/`EndScene` warns and is ignored.

**Where the twin comes from.** `AssetLibrary::BuildMaterial` sets
`engine://shaders/PBRSkinned.glsl` as the skinned twin on **every** material it builds — i.e. every
`.cmat`. Materials you construct by hand with `Material::Create(shader)` have none until you call
`SetSkinnedShader` yourself.

**Queue behaviour.** Skinned draws form their **own state group** (the key uses the twin shader, so
they sort together and never share a bind with the static path) and are **never auto-instanced** —
each has its own palette base. Their cull box is the bind-pose AABB **padded by half its extent on
every side**, a cheap conservative allowance for a pose moving geometry outside its bind bounds.

**Limitation: non-uniform bone scale.** Normals go through `mat3(skin)` plus the engine's
`u_NormalMatrix` and a normalize — exact for the rigid or uniformly-scaled palettes character rigs
produce. A rig with non-uniform bone scale would need a per-joint inverse-transpose; that is a
documented limitation, shared with `PBRInstanced.glsl`.

---

## What is parked

Beyond the FEATURE-MATRIX row quoted at the top, these are explicit v1 boundaries rather than
oversights:

- **Blend trees / state machines / a controller graph** — ✖, no phase. `CrossfadeTo` is the whole
  blending surface.
- **Authoring joint transforms or clips.** The Animation Editor is inspect-only; the runtime never
  writes a `Skeleton` or an `AnimationClip`. There is no `.canim` file — clips live in the source
  model.
- **Multi-material skinned meshes.** The M5 per-slot submesh path explicitly excludes skinned
  meshes; a skinned mesh always draws with its single `MaterialAsset`.
- **Morph targets / blend shapes.** glTF weight channels are skipped at import.
- **More than one skin per file**, root motion, IK, and animation events.

---

## Common patterns

**Animator on the root, meshes below.** One `AnimatorComponent` on the rig root drives every skinned
`MeshRendererComponent` beneath it, because the skeleton search walks down and the draw-time search
walks up.

**Locomotion by crossfade.** Call `Animator().CrossfadeTo(...)` unconditionally each frame with the
clip your state machine wants; the no-op-when-same rule makes that free.

```cpp
class FoxController : public Cosmic::ScriptableEntity
{
public:
    std::string Model = "project://models/Fox.glb";   // reflected script field

protected:
    void OnUpdate(float) override
    {
        const float speed    = glm::length(Character().GetVelocity());
        const bool  grounded = Character().IsGrounded();
        const char* want = !grounded    ? "#Jump"
                         : speed > 3.0f ? "#Run"
                         : speed > 0.1f ? "#Walk"
                                        : "#Idle";
        Animator().CrossfadeTo(Model + want, 0.15f);
    }
};
```

**Scrub instead of play.** Set `Playing = false` and drive `NormalizedTime` yourself for cutscene
poses, turntables or gameplay-driven blends — the same path the Inspector slider uses.

**Blend poses yourself.** `AnimationClip::Sample` and `AnimationClip::BlendLocals` are static and
GL-free. Sample two clips into your own arrays, blend at any weight, then
`skeleton.ComputePalette(locals, palette)` and call `Renderer3D::DrawMeshSkinned` directly.

**Props on characters.** Socket the prop entity under the rig, offset it in the Socket component,
and let `GetWorldTransform` do the rest — physics attach points and scripts read the composed
transform too, not just the renderer.

---

## Pitfalls

**"The model renders but never animates."** Check, in order: (1) is
`MeshRendererComponent::MaterialAsset` (or `MaterialPath`) set? The skinned route requires a
material — a skinned mesh on the Lambert `Color` path silently draws its bind pose, and a
**viewport-dropped** model has no material at all. (2) Did the clip resolve (is `ClipPath` a real
`file#clip`)? (3) Is `Playing` on? (4) Is the animator on, or above, the mesh entity? (5) Is the
mesh actually skinned — a mixed static/skinned file merges to a **static** mesh.

**"It animates in the editor but not in the packaged build."** `PlayerLayer` skips
`UpdateAnimators` while the app is paused. Check `Application::IsPaused` and `SetPauseOnMinimize`
(which defaults to **false**, so this is rare).

**"The clip combo is empty / says *No clips in …*."** Either the file has no animations, or every
animation targets non-joint nodes and was discarded at import. Check the Console for
`"contains no animation clips"`.

**"It plays at the wrong speed after an FBX re-export."** Assimp defaults to **25 ticks/second**
when the file omits `mTicksPerSecond`. Fix the exporter, or `BakeFixedRate` the clip.

**"Scrubbing does nothing."** `NormalizedTime` is only authoritative while `Playing` is **false**.
Untick Playing first.

**"The socket props are at the origin / at the wrong place."** An unresolved socket falls back to
the entity's own local transform, so a socket entity with a zero local sits on its parent. Verify the
joint name exactly (`Skeleton::Find` is case-sensitive and does no fuzzy matching) and that the
socket entity is a **descendant** of the animator entity.

**"The socket ignores the transform I set in the Inspector."** By design — while the socket
resolves, the entity's `TransformComponent` is bypassed. Put the offset in the Socket component's
`Position`/`Rotation`/`Scale`.

**"The character pops at the end of a crossfade."** The fade promotes the next clip and adopts
`NextTimeSeconds` as the new play head. If the two clips have very different phases, either match
their loop points or use a longer fade — there is no phase matching.

**"The crossfade never finishes."** `FadeElapsed` only advances while `Playing`. A paused animator
holds the blend indefinitely.

**"Shadows don't deform."** Only the material's skinned twin drives the lit pass;
`ShadowDepthSkinned.glsl` drives the shadow pass, and it is loaded lazily on first use. A load
failure logs `"ShadowDepthSkinned shader failed to load — skinned casters draw bind pose"` once.
Snow-coverage capture always uses the bind pose, intentionally.

**"The rig disappears at the edge of the screen."** The cull box is the bind-pose AABB padded by
50 % per side. An extreme pose (a fully extended limb far outside bind bounds) can still be culled
while visible.

**"Weights look wrong on a dense mesh."** Only the **4 strongest** influences per vertex survive
import; the rest are discarded, and the survivors are renormalized in the shader.

**"`Skeleton::ComputeGlobals` hangs."** The parent walk has no cycle guard — a hand-built skeleton
whose parent chain loops will spin forever. Both importers produce trees, so this only bites
code-built rigs.

---

## See also

- [`entities-and-components.md`](entities-and-components.md) — `AnimatorComponent`,
  `SocketComponent` and `MeshRendererComponent` in the full component catalogue
- [`rendering-3d.md`](rendering-3d.md) — the queue skinned draws enter: state groups, culling,
  `Model` vs `Mesh`, and why skinned draws never auto-instance
- [`materials-and-shaders.md`](materials-and-shaders.md) — `Material`, `.cmat`, the shader
  contract, and `SetSkinnedShader`
- [`lighting-and-environment.md`](lighting-and-environment.md) — the pass graph the shadow twin
  runs in
- [`assets-and-vfs.md`](assets-and-vfs.md) — model import, `.cmeta` sidecars, `AssetLibrary` caching
- [`scripting.md`](scripting.md) — `ScriptableEntity` and all eight proxies, including `Animator()`
- [`scenes-and-serialization.md`](scenes-and-serialization.md) — how reflected fields (including
  `Quat` ordering: `[w, x, y, z]`) are written
- [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md) — why this whole tier is 3D
  only
- `tests/test_animation.cpp`, `tests/test_crossfade.cpp`, `tests/test_sockets.cpp` — the executable
  specification for the sampling, blending and socket math above
