# Scenes & Serialization — Guide

**What this covers:** creating, saving and loading scenes; the `.cscene` file format and the
reflection registry that generates it; UUIDs and `EntityRef` fields; **prefabs**; async scene
transitions with `SceneManager`; undo/redo with `CommandStack`; and the guarantee that opening a
scene in a build that does not know one of its component types **loses nothing**.
**Source of truth:** `Cosmic/src/scene/SceneSerializer.{h,cpp}`, `scene/Scene.{h,cpp}`,
`scene/SceneManager.{h,cpp}`, `scene/Components.h`, `core/UUID.h`, `core/CommandStack.{h,cpp}`,
`reflect/TypeDescriptor.h`, `reflect/TypeRegistry.{h,cpp}`, `reflect/TypeRegistry3D.cpp`,
`assets/AssetLibrary.cpp`, `layers/PlayerLayer.cpp`, `Projects/Starforge/src/Prefabs.h`,
`Projects/Starforge/src/commands/EditorCommands.h`, `tests/test_scene_serializer.cpp`,
`tests/test_crossbuild_scene.cpp`, `tests/test_scenemanager.cpp`, `tests/test_commandstack.cpp`
**API Reference:** [../reference/ecs.md](../reference/ecs.md) (`Scene`, `Entity`, components) —
`SceneSerializer`, `SceneManager`, `CommandStack`, `UUID` and the reflection registry have **no
reference chapter yet**; this chapter and the headers are the current source ·
**How it works:** [../systems/ecs-scene.md](../systems/ecs-scene.md)
**Configuration:** both. Everything here compiles in the 2D build, and the cross-build guarantee in
[Nothing is ever lost](#nothing-is-ever-lost--opaquecomponentscomponent) is *why* the split is safe.
See [../systems/build-2d-3d-split.md](../systems/build-2d-3d-split.md).

A `.cscene` is a JSON file, and **nothing in the engine writes it by hand.** One generic visitor
walks the reflection registry: every component type that registered its fields is written and read
with zero per-component code. Add a reflected field to a component and it serializes; add a whole
new component type in your game DLL and it serializes. The same visitor produces `.cprefab` files
and standalone reflected assets like `.cmat`.

That design is also why a scene survives builds that disagree about what components exist. A block
this build cannot name is kept as verbatim text and written back out untouched — so a 3D scene can
be opened, edited and saved by the 2D editor with every mesh, light and terrain intact.

---

## Quick start

```cpp
#include <Cosmic.h>

// A scene is a plain object — Ref<Scene> because everything that holds one
// (SceneManager, the editor, a flow) shares ownership.
Cosmic::Ref<Cosmic::Scene> scene = Cosmic::Scene::Create();

Cosmic::Entity crate = scene->CreateEntity("Crate");
crate.GetComponent<Cosmic::TransformComponent>().Position = { 0.0f, 1.0f, -4.0f };

// Save. Always resolve through the VFS — never hand a bare relative path.
const std::string path = Cosmic::FileSystem::Resolve("project://scenes/Main.cscene");
if (!Cosmic::SceneSerializer::Save(*scene, path))
    CS_ERROR("Could not save the scene.");

// Load. Load does NOT clear the target first — load into a FRESH scene.
Cosmic::Ref<Cosmic::Scene> loaded = Cosmic::Scene::Create();
if (!Cosmic::SceneSerializer::Load(*loaded, path))
    CS_ERROR("Could not load the scene.");

// Entity identity survived the trip.
Cosmic::UUID id = crate.GetComponent<Cosmic::IDComponent>().ID;
Cosmic::Entity same = loaded->FindByUUID(id);   // valid, tagged "Crate"
```

---

## Create a scene

`Scene::Create()` is the factory (`Ref<Scene>` — `std::make_shared` under the hood). A default scene
is empty: no entities, no systems, no physics session, no flow.

**Create entities through the scene, never through the registry.** `Scene::CreateEntity(name)`
emplaces three components every time — an `IDComponent` with a fresh random `UUID`, a
`TransformComponent`, and a `TagComponent` holding the name — and indexes the UUID so
`FindByUUID` is O(1). An entity you produce with `scene->GetRegistry().create()` has no `IDComponent`
and is therefore **invisible to the serializer entirely**: `SaveToString` iterates
`registry.view<IDComponent>()`, so such an entity is silently absent from the file.

`CreateEntityWithUUID(id, name)` is the same thing with a caller-supplied identity — that is what
the loader calls. A zero/invalid UUID falls back to a fresh one.

```cpp
Cosmic::Entity e = scene->CreateEntity("Turret");
// e already has: IDComponent, TransformComponent, TagComponent{"Turret"}

Cosmic::Entity restored = scene->CreateEntityWithUUID(Cosmic::UUID(0xa1), "Turret");
```

## Save and load a `.cscene`

Four entry points, two on files and two on strings:

| Call | Does | Returns |
| --- | --- | --- |
| `SceneSerializer::Save(scene, path)` | Rotate a backup, then write atomically | `false` on I/O failure (logs) |
| `SceneSerializer::Load(scene, path)` | Read the file and load into `scene` | `false` on open/parse failure (logs) |
| `SceneSerializer::SaveToString(scene)` | Serialize to JSON text | the text (never fails) |
| `SceneSerializer::LoadFromString(scene, text)` | Parse text into `scene` | `false` on parse / missing `entities` |

`path` is a **resolved disk path**, not a VFS URI — every call site in the tree wraps it in
`FileSystem::Resolve("project://…")` first. The string variants are what the editor uses to snapshot
the edit scene before Play, and what the tests use headlessly.

Three behaviours worth knowing before you build a workflow on these:

**`Load` does not clear the scene first.** It creates entities on top of whatever is already there.
Every caller in the tree loads into a freshly created `Scene`; do the same.

**`Save` is crash-safe in two ways.** It writes to `<path>.tmp` and renames over the target, so a
half-written file never replaces a good one. Before that it copies any existing file to `<path>.bak`
— exactly **one** backup, the last successfully saved version. A failed backup copy is logged and
the save proceeds; a save is never blocked by backup trouble.

**Failure is a `false` return plus a log line, never an exception or an assert.** Check the return.

```cpp
Cosmic::Ref<Cosmic::Scene> fresh = Cosmic::Scene::Create();
if (!Cosmic::SceneSerializer::Load(*fresh, Cosmic::FileSystem::Resolve("project://scenes/Level2.cscene")))
{
    CS_ERROR("Level2 failed to load — keeping the current scene.");
    return;   // `fresh` may be partially populated; discard it
}
m_Scene = fresh;   // swap only after success
```

## What is inside a `.cscene`

```json
{
  "cosmic_scene": 1,
  "entities": [
    {
      "id": "0000000000000501",
      "components": {
        "Camera": { "Primary": true, "ProjectionType": 0, "FovDeg": 60.0,
                    "Near": 0.1, "Far": 1000.0, "OrthoSize": 10.0 },
        "Tag": { "Tag": "Hover Cube" },
        "Transform": {
          "Position": [0.0, 1.0, 0.0], "Rotation": [0.0, 0.0, 0.0],
          "Scale": [1.0, 1.0, 1.0], "RotationQuat": [1.0, 0.0, 0.0, 0.0],
          "UseQuatRotation": false
        }
      }
    }
  ]
}
```

The shape is deliberately small:

- **`cosmic_scene`** is the schema version. It is written as `1` and — honestly — nothing reads it
  back today; `LoadFromString` only requires an `entities` array.
- **`id`** is the entity's UUID as 16 lowercase hex characters. It is a top-level key, *not* a
  component block, because `IDComponent` is not reflected — identity is not user-editable.
- **`components`** maps a component's **registered name** to its reflected fields. The name is the
  reflection name, not the C++ type name: `TagComponent` is `"Tag"`, `TransformComponent` is
  `"Transform"`, `SpriteRendererComponent` is `"SpriteRenderer"`. So the reflected field path for a
  tag is `Tag.Tag`, and for a position it is `Transform.Position`.

### How values are encoded

| `FieldKind` | JSON |
| --- | --- |
| `Bool` / `Int32` / `UInt32` / `Float` | the scalar |
| `Vec2` / `Vec3` / `Vec4` / `Color` | array, in component order (`Color` is a `vec4`) |
| `Quat` | array in **`[w, x, y, z]`** order |
| `String` / `AssetPath` | string |
| `EntityRef` | the target UUID's 16-char hex string |
| `Enum` | the integer value on write; **either** an integer **or** an option name is accepted on read |

Reading is deliberately tolerant. A missing or short array yields zeros (a `Vec4`/`Color` defaults
its `w` to `1.0`); a wrong JSON type yields the kind's zero value rather than failing the load.
A field absent from the file keeps whatever the component's C++ default constructor set.

### Determinism, and the two things people expect that are not true

`SaveToString` sorts entities by UUID before emitting them, so entt's internal storage order never
leaks into the file. Component blocks are emitted in whatever order the registry iterates, but that
does not show either, because of the next point.

> **`SaveToString` emits `dump(2)` — pretty-printed with a two-space indent, not compact.** Scene
> files are meant to diff in a PR. Do not size buffers or write parsers assuming one line.

> **A round-trip is semantically stable, not byte-stable against the original file.** nlohmann's
> JSON objects are `std::map`-backed, so parsing and re-dumping **sorts every object's keys**. A
> hand-written file with keys in authoring order comes back alphabetized, and whitespace is
> normalized. What *is* guaranteed — and tested — is that **save → load → save is byte-identical**
> (`tests/test_scene_serializer.cpp`, `tests/test_crossbuild_scene.cpp`). Compare dumps to dumps,
> never a dump to the file you typed.

### Flags that change what gets written

Three registration flags on a field affect serialization (`reflect/TypeDescriptor.h`):

| Flag | Builder call | Effect |
| --- | --- | --- |
| `Field_NoSerialize` | `.NoSerialize()` | Runtime-only; skipped on both save and load |
| `Field_OmitIfTrue` | `.OmitIfTrue()` | A `bool` is omitted **while true**, written when false |
| `Field_HideInInspector` | `.HideInInspector()` | Still serialized — only hidden from the editor UI |

`OmitIfTrue` is why you will not find `"Enabled": true` or `"Active": true` anywhere in a scene
file. Both default to true and sit on nearly every entity; omitting them keeps files that predate
the flags byte-identical, and a missing key loads back as `true`. Toggle one off and the key
appears.

## Reflection is what drives all of this

The serializer has no list of component types. It asks `Reflect::GetRegistry()`, which is a
process-wide singleton **owned by the engine DLL** — so the engine, the editor and your game module
all see one instance. Register a component and four subsystems light up at once: the Inspector
(rows per field), the serializer (this chapter), the undo `CommandStack`, and the editor's
copy/paste of component values.

```cpp
// In your project's Module.cpp — see scripting.md for the CS_COMPONENT form.
Cosmic::Reflect::Class<ThrusterComponent>("Thruster", "Gameplay")
    .Field("MaxThrustN",  &ThrusterComponent::MaxThrustN).Range(0.0f, 5000.0f)
    .Field("Nozzle",      &ThrusterComponent::Nozzle).AsAssetPath("mesh")
    .Field("Target",      &ThrusterComponent::TargetId).AsEntityRef()
    .Field("Enabled",     &ThrusterComponent::Enabled).OmitIfTrue()
    .Field("CachedThrust",&ThrusterComponent::CachedThrust).NoSerialize();
```

The first string is the **serialization name** — it becomes the JSON key and is what a future build
matches on, so treat it as a file-format decision. The second is the Inspector category.

`FieldKind` is deduced from the C++ member type (`bool`, `int32_t`, `uint32_t`, `float`, `glm::vec2`
/`vec3`/`vec4`/`quat`, `std::string`, any enum, and `uint64_t` → `EntityRef`). Anything else is a
compile error, by design. `.Color()`, `.AsAssetPath("texture")` and `.AsEntityRef()` refine the
deduced kind; `.Range()`, `.Step()`, `.Doc()`/`.Tooltip()`, `.Degrees()`/`.Meters()`/`.Seconds()`
and `.EnumValue(name, v)` are UI hints and are **never** serialized.

> A component type that crosses the project-DLL boundary must also carry `CS_REGISTER_COMPONENT(T)`
> at global scope in its header. That is a separate mechanism (it pins entt's type id to a hash of
> the type name instead of a per-module counter) and it is required for storage correctness whether
> or not you reflect the type. See
> [`entities-and-components.md`](entities-and-components.md#custom-components).

### Blocks the serializer handles by hand

Five things in a `.cscene` are *not* plain reflected fields, because their payloads are not
reflectable kinds. They are the complete list of special cases in `SceneSerializer.cpp`:

| Key | Belongs to | Why it is special |
| --- | --- | --- |
| `"id"` | the entity | `IDComponent` is not reflected; identity is not editable |
| `"Relationship"` | `RelationshipComponent` | Structural, not reflected — see below |
| `NativeScript.Fields` | `NativeScriptComponent` | A `name → boxed value` map; kinds come from the *script's* descriptor |
| `SystemScript.Fields` | `SystemScriptComponent` | Same, via the `SystemDescriptor` |
| `Tilemap.Cells` | `TilemapComponent` | A `vector<uint16_t>`, written as a flat int array |
| `MeshRenderer.MaterialPaths` | `MeshRendererComponent` (3D) | A `vector<string>`; omitted entirely when empty |

The hierarchy block is worth spelling out. Only `Children` is written, only when non-empty:

```json
"Relationship": { "Children": ["00000000000000b1", "00000000000000b2"] }
```

The `Parent` back-link is **not** serialized — it is rebuilt on load. Loading runs in two passes:
every entity is created first, then each stored `Children` list is replayed through
`Scene::SetParent(child, owner, /*keepWorldPose=*/false)`. `keepWorldPose` is false because saved
transforms are already **local**; rewriting them at parenting time would compound the parent
transform in twice. Children order is preserved because `SetParent` appends.

## UUIDs and `EntityRef` fields

`Cosmic::UUID` is a 64-bit value. `UUID()` draws a random one that is never 0; `UUID(0)` is the
reserved null. `ToString()` gives 16 lowercase hex chars and `FromString()` inverts it, returning 0
on a parse failure. A million-draw collision test lives in `tests/test_scene_serializer.cpp`.

UUIDs exist so that references survive save/load. There are three kinds in the engine, all
UUID-based for the same reason: parent/child links, `PrefabComponent::SourcePath`'s instance
identity, and **`EntityRef` fields**.

An `EntityRef` field is a `uint64_t` member holding another entity's UUID. Declare it with
`.AsEntityRef()`; it is written as hex and read back as the same number, so the reference points at
the same logical entity even though the underlying `entt::entity` handle is different after a load.

```cpp
struct TurretComponent
{
    uint64_t TargetId = 0;   // a UUID value, 0 == unset
};

Cosmic::Reflect::Class<TurretComponent>("Turret", "Gameplay")
    .Field("Target", &TurretComponent::TargetId).AsEntityRef();

// Resolving one at runtime:
auto& turret = self.GetComponent<TurretComponent>();
if (Cosmic::Entity target = scene->FindByUUID(Cosmic::UUID(turret.TargetId)))
    Aim(target);
// FindByUUID returns an INVALID Entity when the id is unknown or the entity died —
// test it, do not assume.
```

> `EntityRef` resolution is **not** remapped by `InstantiatePrefab`. Prefab instantiation remaps
> parent/child links but leaves `EntityRef` field values as the original UUIDs, so a prefab whose
> members point at each other by `EntityRef` will have every instance pointing at the *first*
> instance's entities (or at nothing). Point at entities by tag inside a prefab, or resolve in
> `OnStart`.

## Prefabs

A prefab is a self-contained `.cprefab` holding one entity **and its whole subtree**. The schema is
the scene schema plus a root pointer:

```json
{ "cosmic_prefab": 1, "root": "00000000000000a1", "entities": [ … ] }
```

Two engine calls do the work:

```cpp
// Save `root` + every descendant (preorder, children in stored order).
bool ok = Cosmic::SceneSerializer::SavePrefab(*scene, root,
              Cosmic::FileSystem::Resolve("project://prefabs/Rover.cprefab"));

// Instantiate into a scene with FRESH UUIDs so instances coexist.
Cosmic::Entity inst = Cosmic::SceneSerializer::InstantiatePrefab(*scene,
              Cosmic::FileSystem::Resolve("project://prefabs/Rover.cprefab"));
if (!inst) { /* bad path or bad JSON — logged */ }
```

What `InstantiatePrefab` guarantees, and what it does not:

- **Fresh UUIDs.** Every entity in the subtree gets a new identity, so you can instantiate the same
  prefab many times into one scene. The internal hierarchy is rebuilt by remapping old ids to new.
- **The returned root has no parent.** The caller places it — set its transform, or `SetParent` it.
- **The root is stamped with a `PrefabComponent`** whose `SourcePath` is the path you passed. Note
  that this is the *resolved disk path*; Starforge overwrites it with the VFS path immediately after
  instantiating, which is what you want for a relocatable project. Do the same if you instantiate
  from code and expect the project to move.
- **There is no override tracking and no live propagation.** An instance is a plain detached copy
  that remembers where it came from. Editing the asset does not change open instances.

### Apply and revert

Both are app-side compositions of the two engine calls, implemented in
`Projects/Starforge/src/Prefabs.h`, and both are trivial to reproduce in your own tooling:

- **Apply** = `SavePrefab(scene, instanceRoot, sourcePath)`. The instance's current subtree
  overwrites the asset.
- **Revert** = capture the root's `TransformComponent`, `DestroyEntity(root, /*children=*/true)`,
  `InstantiatePrefab(...)`, then write the captured transform back onto the fresh root. The
  instance is replaced in place; its position survives, everything else comes from the asset.

> In Starforge v1, prefab save/instantiate/apply/revert **mark the scene dirty but are not on the
> undo stack**, with one exception: a prefab dropped into the viewport is recorded as a single
> undoable create step (`Commands::RecordSpawn`). Revert in particular is not undoable — it destroys
> the subtree outright.

## Nothing is ever lost — `OpaqueComponentsComponent`

When the loader meets a component block whose name is not in the reflection registry, it does not
warn and drop it. It appends `(name, verbatim JSON text)` to an `OpaqueComponentsComponent` on that
entity, and `SaveToString` re-parses and re-emits every stored block alongside the real ones.

This is the mechanism that makes the whole 2D/3D split safe. A scene authored in the 3D editor
carries `MeshRenderer`, `DirectionalLight`, `Terrain`, `NavMesh` and the rest; the 2D engine
registers none of those types, so **every one of them takes the opaque path**. Open the project in
the 2D editor, move a sprite, save, reopen in 3D — the meshes, lights and terrain are exactly as
authored. `tests/test_crossbuild_scene.cpp` asserts this from both sides: in the 2D build the
3D blocks come back key-for-key and value-for-value; in the 3D build they load into real components
with the authored values intact.

It is not limited to the build split. The same path covers:

- A scene that uses a **game module that is not loaded** — open a project's scene in a build of the
  editor without that module and its custom components ride through untouched.
- A scene written by a **newer engine** with component types this build predates.
- A component **you removed** from your module — its data survives in the file until something
  rewrites the block.

```cpp
// Inspecting what a scene carried that this build could not name:
auto& reg = scene->GetRegistry();
for (auto handle : reg.view<Cosmic::OpaqueComponentsComponent>())
    for (const auto& [name, json] : reg.get<Cosmic::OpaqueComponentsComponent>(handle).Blocks)
        CS_WARN("Unknown component '{0}' preserved verbatim.", name);
```

The one thing preservation cannot do is *interpret*. An opaque block is text: the 2D editor cannot
show you the mesh, and a script cannot read the field. If a block is unexpectedly opaque, the cause
is almost always a module that failed to load or a renamed reflection name.

> Renaming a component's **registration name** silently orphans every existing scene: the old blocks
> become opaque and the new type loads its defaults. The data is still in the file, but nothing
> reads it. Treat reflection names as a file format.

## Switching scenes — `SceneManager`

`SceneManager` is a plain engine service, **not a singleton** — you own one and tick it, the same
pattern as `SerialLink` and `ScriptHost`. It drives a scene swap through a four-state machine so a
load frame never shows as a hitch:

```
Idle --Request--> FadeOut --> Loading --> FadeIn --> Idle
```

```cpp
class GameLayer : public Cosmic::Layer
{
    Cosmic::SceneManager m_Scenes{ 0.4f };   // fade seconds

    void OnAttach() override
    {
        // Synchronous, no fade — parse, swap, done. This is the editor's File▸Open.
        if (!m_Scenes.Load(Cosmic::FileSystem::Resolve("project://scenes/Main.cscene")))
            CS_ERROR("startup scene failed to load");
    }

    void OnUpdate(float ts) override
    {
        m_Scenes.OnUpdate(ts);                       // advance the state machine
        Cosmic::Ref<Cosmic::Scene> active = m_Scenes.GetActiveScene();
        if (!active) return;

        if (m_Scenes.IsLoading())
            DrawFadeOverlay(m_Scenes.FadeAlpha());   // 0 = clear .. 1 = opaque

        // …tick + render `active`…
    }

    void GoToLevel2()
    {
        // Resolve yourself — SceneManager passes the string straight to
        // SceneSerializer::Load and does NOT touch the VFS.
        m_Scenes.Request(Cosmic::FileSystem::Resolve("project://scenes/Level2.cscene"),
                         Cosmic::SceneTransition::Fade);
    }
};
```

The two `Request` overloads differ only in how the next scene is produced — a `.cscene` path, or a
`SceneLoader` (`std::function<Ref<Scene>()>`) you write yourself for a procedurally built level.
Both are queued: `Request` during a transition stores the request and honours it when the current
one finishes, **latest pending wins**, so a button mashed three times still performs one swap.

Things to hold onto:

- **`SceneManager` does not resolve VFS paths.** Both `Load(path)` and the `Request(path, …)`
  overload hand the string straight to `SceneSerializer::Load`, which opens it as a filesystem path.
  Wrap every path in `FileSystem::Resolve` yourself — `PlayerLayer` does, and a bare
  `"project://…"` will simply fail to open.
- **"Async" means "hidden", not "threaded".** The loader runs on the **main thread**, in one
  `OnUpdate` during the `Loading` frame, because GL resource creation is main-thread only. The fade
  hides that single frame. A CPU-prepass split onto the `JobSystem` is a recorded follow-up, not
  shipped.
- **A failed load keeps the current scene.** A loader returning `nullptr` logs an error, leaves
  `GetActiveScene()` alone, and sets `LastLoadSucceeded()` to false. Check it after a transition
  finishes if the difference matters.
- **`Progress()` is transition progress, not load progress** — 0→0.5 across the fade-out, 0.5 during
  the load frame, 0.5→1 across the fade-in. There is no byte-level load percentage to report.
- **`Load()` bypasses the machine entirely** — no fade, no queueing, immediate swap, and it resets
  the state to `Idle`.
- **The manager does not tick your scene, run scripts, or bind physics.** It only owns the swap.
  Whatever swapped the scene must re-instantiate scripts and rebind physics —
  `PlayerLayer::RebindScripts` is the reference implementation of that pattern (see
  [`scripting.md`](scripting.md#who-drives-the-scripthost)).

The state machine is pure logic with no GL, which is why it is headless-tested end to end in
`tests/test_scenemanager.cpp`.

## Undo and redo — `CommandStack`

`CommandStack` is an engine-generic, bounded do/undo/redo history. It knows nothing about scenes,
the editor or ImGui: it stores `ICommand` objects and plays them backwards and forwards. Starforge's
concrete commands (reflected-field edits, gizmo transforms, create/destroy, reparent, tile and voxel
strokes) subclass `ICommand` and live in the editor, but the stack is reusable by any tool you
build.

```cpp
class SetGravity : public Cosmic::ICommand
{
public:
    SetGravity(World& w, float before, float after)
        : m_World(w), m_Before(before), m_After(after) {}

    void Do()   override { m_World.Gravity = m_After;  }
    void Undo() override { m_World.Gravity = m_Before; }
    std::string Name() const override { return "Set Gravity"; }

private:
    World& m_World;
    float  m_Before, m_After;
};

Cosmic::CommandStack stack(256);   // max depth; oldest entries drop past it
stack.SetDirtyCallback([this] { MarkDocumentDirty(); });

stack.Execute(Cosmic::CreateScope<SetGravity>(world, world.Gravity, -3.7f));
stack.Undo();   // false if there was nothing to undo
stack.Redo();
```

### `Execute` versus `Push` — the distinction that matters

- **`Execute(cmd)`** — the change has **not** happened yet. The stack calls `Do()` now and records
  it. Use for discrete actions: create, delete, a menu operation, a checkbox toggle handled in code.
- **`Push(cmd)`** — the change is **already live**. An ImGui drag mutated the component this frame;
  you captured the "before" on activate and now want it in the history. The stack records without
  calling `Do()`. `Undo()` restores the captured before; `Redo()` re-applies via `Do()`, so **`Do()`
  must be idempotent with respect to the current state**.

Both clear the redo branch, and both fire the dirty callback. `Clear()` does not.

### Coalescing

A continuous drag should be one undo step, not four hundred. When a command is added, the stack
offers it to the current top via `TryMerge()`, but **only when both `MergeKey()` strings are equal
and non-empty** and no barrier intervened. A merging command absorbs the newer one's post-state and
no new history entry appears. Default `MergeKey()` is empty, so nothing coalesces unless you opt in
— key it on the thing being edited, e.g. `"xform:" + uuid`.

Call `SetMergeBarrier()` when the user ends a gesture (mouse release) so the next drag starts a
fresh entry even though its key matches.

### Using it with scenes

The rule Starforge follows, and the reason its undo is reliable: **commands reference entities by
UUID, never by raw `entt::entity` handle.** A delete-then-undo recreates the entity with the same
UUID but a different handle; a command holding the old handle would undo into a recycled slot.
`EditorCommands.h` documents this as the E7 gotcha and is a good template for your own commands.

Three lifecycle facts that surprise people:

- **`CommandStack` is not thread-safe.** Drive it from the main/UI thread only.
- **Entering or leaving Play clears the stack.** There is no undo across the play boundary — the
  runtime scene is a throwaway copy of the edit scene.
- **A script hot reload clears the stack too**, because the scene is round-tripped through JSON and
  rebuilt around the new module (see [`scripting.md`](scripting.md#hot-reload)).

## Reflected assets that are not scenes

The same visitor serializes any *single* registered type to its own file. That is how `.cmat`
material assets work, and how the editor saves world-system recipes:

```cpp
// Signature takes the entt type hash + a void* to a live instance.
static std::string SaveReflectedToString(uint32_t typeId, const void* instance);
static bool        LoadReflectedFromString(uint32_t typeId, void* instance, const std::string& json);
static bool        SaveReflectedToFile(uint32_t typeId, const void* instance, const std::string& path);
static bool        LoadReflectedFromFile(uint32_t typeId, void* instance, const std::string& path);
```

```cpp
Cosmic::MaterialAsset asset;
asset.Albedo = { 0.8f, 0.2f, 0.1f, 1.0f };
Cosmic::AssetLibrary::SaveMaterialAsset(asset, "project://materials/Rust.cmat");
// …which is exactly:
Cosmic::SceneSerializer::SaveReflectedToFile(
    entt::type_hash<Cosmic::MaterialAsset>::value(), &asset,
    Cosmic::FileSystem::Resolve("project://materials/Rust.cmat"));
```

The emitted shape is `{ "cosmic_type": "<name>", "fields": { … } }`, also `dump(2)`. On read the
loader accepts **either** that wrapped form **or** a bare field object, which makes hand-written
asset files pleasant. An unregistered `typeId` or a null instance returns `"{}"` / `false` rather
than crashing. `MaterialAsset` is registered as `"Material"` in the shared type registry — see
[`materials-and-shaders.md`](materials-and-shaders.md) for the `.cmat` field set itself.

---

## Common patterns

**Snapshot / restore a scene in memory.** This is how Play mode works, and it dogfoods the
serializer on every press: `SaveToString` the edit scene, `LoadFromString` into a fresh `Scene`, run
that, throw it away on stop, restore the untouched original. No deep-copy code exists in the engine
because this is cheaper to keep correct.

```cpp
const std::string snapshot = Cosmic::SceneSerializer::SaveToString(*editScene);
Cosmic::Ref<Cosmic::Scene> runtime = Cosmic::Scene::Create();
if (!Cosmic::SceneSerializer::LoadFromString(*runtime, snapshot))
    return;   // never swap on a failed parse
```

**Autosave before anything destructive.** Starforge writes the edit scene to
`user://starforge/autosave/<Project>/<Scene>.cscene` before entering Play. `Save`'s `.bak` rotation
covers the crash case; an autosave covers the "I pressed Play and lost my layout" case.

**Load-only-what-you-need at boot.** The standalone player reads the startup scene name from
`project://project.cproj` and hands it to `SceneManager::Load`. A screen flow, when present, takes
over scene selection entirely (see [`flow-and-story.md`](flow-and-story.md)).

**Author content in code, save it once.** Every Starforge sample generator builds a scene with
`CreateEntity`/`AddComponent` and then calls `SceneSerializer::Save`. It is a perfectly good way to
produce a starting `.cscene` you then edit by hand.

---

## Pitfalls

**"My entity isn't in the saved file."** It has no `IDComponent` — it was created via
`scene->GetRegistry().create()` instead of `Scene::CreateEntity`. `SaveToString` only walks entities
that have one.

**"Loading added a second copy of everything."** `SceneSerializer::Load` does not clear the target
scene. Load into a fresh `Scene::Create()` and swap.

**"The file I saved doesn't byte-match the file I wrote by hand."** Expected. nlohmann sorts object
keys on parse and the dump is re-indented. Save→load→save *is* byte-identical; that is the invariant
to test against.

**"My component came back with default values."** Its registration name changed, or its module was
not loaded when the scene was read. Look for the block in `OpaqueComponentsComponent` — if it is
there, the data survived and the type is what went missing.

**"`"Enabled": true` never appears in my scene file."** Correct — `Field_OmitIfTrue`. A missing key
loads as `true`.

**"My `LODGroupComponent` levels vanished after a save."** `Levels` is neither reflected nor
special-cased; LOD levels are code-only and do not survive a round-trip. Only `Color` and
`CastShadows` are registered on that component.

**"Prefab instances all point at the same target entity."** `EntityRef` field values are not
remapped by `InstantiatePrefab`. Resolve references by tag in `OnStart`, or set them after
instantiating.

**"Revert to prefab lost my child edits and I can't undo."** By design in v1 — revert destroys the
subtree and re-instantiates, and prefab operations are not on the undo stack.

**"The parent link isn't in my scene file."** Only `Children` is serialized; `Parent` is rebuilt on
load. If a child's parenting is wrong, look at the parent's `Relationship` block.

**"Child transforms drifted after a load."** Something re-parented with `keepWorldPose = true` after
load. The loader deliberately uses `false` because saved transforms are already local.

**"The fade plays but the scene never changes."** The loader returned `nullptr`. Check
`LastLoadSucceeded()` and the log — a failed load keeps the current scene on purpose. The usual
cause is a **`project://` path handed to `SceneManager` unresolved**; it does not touch the VFS.

**"Scripts stopped running after a `SceneManager` swap."** The manager owns the swap and nothing
else. Re-instantiate the `ScriptHost` and rebind physics on the new scene.

**"My undo restored the wrong entity."** The command captured a raw `entt::entity`. Capture the
UUID instead — handles are recycled.

**"Undo is empty after I pressed Play."** Entering and leaving Play clears the stack, as does a
script hot reload.

---

## See also

- [`entities-and-components.md`](entities-and-components.md) — what is *in* a scene: the entity
  handle, the full component catalogue, hierarchy, `Active`/`Enabled`.
- [`scripting.md`](scripting.md) — `NativeScriptComponent`, the reflected script fields that ride
  along in `.cscene`, and hot reload.
- [`../reference/ecs.md`](../reference/ecs.md) — exact `Scene` / `Entity` / component signatures.
- [`../systems/ecs-scene.md`](../systems/ecs-scene.md) — how the registry and the Phase 29 file
  partition work.
- [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md) — the configuration split the
  opaque-preservation guarantee protects.
- [`assets-and-vfs.md`](assets-and-vfs.md) — `FileSystem::Resolve`, the `project://` / `user://`
  roots, and `AssetLibrary`.
- [`materials-and-shaders.md`](materials-and-shaders.md) — `.cmat`, the biggest consumer of the
  reflected-struct path.
- [`flow-and-story.md`](flow-and-story.md) — `.cflow` screen flows, which drive scene selection
  above `SceneManager`.
- [`editor-ui-and-theming.md`](editor-ui-and-theming.md) — the editor shell that hosts the
  Inspector and the undo history UI.
