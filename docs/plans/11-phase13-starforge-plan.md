# Phase 13 Plan — Starforge (the Cosmic editor; S14 promotion)

> **Created 2026-07-03.** Promotes the doc 05 §13 S14 backlog rows *Editor app*, *Scene
> serialization*, *Undo/redo*, and *Scripting* into a full phase with work orders.
> **Scope decisions (user-approved 2026-07-03):**
> - **Name: Starforge** ("where worlds are forged"). Ships as `Projects/Starforge` — a normal
>   CosmicApp plugin DLL; no new host machinery.
> - **Logic model: native C++ scripts in a hot-reloadable per-project DLL** (doc 05's
>   "C++-DLL-first is the story"). An embedded-Lua tier is **mapped in §4 but parked** — the
>   reflection layer is designed so Lua bindings can be generated from it later.
> - **Model creation v1: parametric primitives** (live-editable box/sphere/cylinder/cone/plane/
>   torus components) + transform gizmos. CSG booleans are parked (§9 P2).
> - **Import: industry standard.** v1 vendors **assimp** (FBX/OBJ/STL/DAE/PLY — covers Blender
>   via glTF *or* FBX, and CAD via STL) next to the existing cgltf glTF path. True CAD B-rep
>   (**STEP**) lands later as a separate `step2gltf` converter tool (OpenCascade) so the editor
>   never links OCCT (§9 P1).
> - **Shipping: reuse the existing pipeline.** A Starforge project *is* a CosmicApp plugin
>   (scenes + assets + game DLL); "Package" wraps the existing Release build + `package.bat`
>   machinery. **Project library / database:** projects stay fully self-contained folders — a
>   registry index (and any future cloud/DB layer) sits ON TOP and is parked (§9 P7).
>
> **Division of labor:** the compiling `Projects/Starforge` skeleton (editor shell: dock layout,
> viewport w/ CAD nav, hierarchy/inspector/content/console panels, `TODO(E#)` markers) was
> **written up-front by the planning session (2026-07-03)** and sits next to this doc. Each
> numbered work order below is ONE implementation session (Opus-tier). Kickoff prompt at the
> bottom.

---

## 0. Execution notes — READ ONCE, THEY APPLY TO EVERY ITEM

1. **Build/test (non-interactive):** never run `build_all.bat`/`build.bat` (they `pause`). Use
   the VS-bundled cmake recipe in `00-MASTER-ROADMAP.md` §"Working agreement". Outputs land in
   `build/Runtime/<Config>/`; run `CosmicApp.exe` and pick **Starforge** (or
   `CosmicApp --project Starforge`). Tests: `build/Runtime/<Config>/CosmicTests.exe`. Re-run the
   cmake **configure** after adding new *engine* source files (engine GLOB lacks
   CONFIGURE_DEPENDS); Starforge's own glob is CONFIGURE_DEPENDS so app files need no
   reconfigure.
2. **Engine rules (doc 05 §0/§1):** no `gl*`/`GL_*` outside `platform/OpenGL/`; new GPU state =
   `RendererAPI`/`RenderCommand` verbs; GPU-owning classes non-copyable; claim binding points in
   `renderer/BindingPoints.h` first. **Rule 8 analog for this phase:** the engine gains
   *generic editor-tier* modules (`reflect/`, `SceneSerializer`, `CommandStack`, `scripting/`,
   `SceneManager`, `DebugDraw`, `FileWatcher`) — it NEVER gains Starforge-specific names or UI.
   Panels, workflows, and anything with an ImGui call that isn't a reusable widget live in
   `Projects/Starforge`.
3. **Testability split:** `CosmicTests` links only `Cosmic` and includes only engine headers.
   Anything that needs a unit test therefore lands engine-side (that's where reflection,
   serialization, GUIDs, hierarchy math, command stack, and script-host logic go). Headless —
   no GL in tests. `doctest::Approx.epsilon` is RELATIVE; use absolute tolerances for world
   coordinates.
4. **Compatibility gate (non-negotiable):** Engine3DDemo, Frontier, SF_Telem, and ViperSim must
   build and run identically after every item. New components are additive; existing component
   structs may gain fields only at the END (ABI note in the work order) and never change
   defaults in ways that alter current rendering. Smoke-run Engine3DDemo + Frontier when an item
   touches `Cosmic/src/scene/` or `renderer/`.
5. **App-side VFS rule:** `project://` must be resolved in the CALLING DLL —
   `Cosmic::FileSystem::Resolve("project://...")` inside Starforge code. Starforge calls
   `FileSystem::SetActiveProject(<open project name>)` when a project opens (the skeleton boots
   with `"Starforge"` as a placeholder until E6).
6. **Plugin contract (existing, do not reinvent):** a project DLL exports
   `InitializePluginContexts(Cosmic::HostContext)` (sets ImGui/ImPlot contexts) and
   `CreatePluginLayer()` → `Cosmic::Layer*`. The Launcher lists any DLL exporting
   `CreatePluginLayer`; `--project <Name>` boots one directly. Starforge itself uses this; user
   projects add a second export (§3.4).
7. **State-restore contracts** (doc 10 note 5) apply to any pass the editor adds (ID-buffer,
   thumbnails, material preview): restore depth ON/ON, cull None, blend Alpha, and re-bind the
   framebuffer you replaced (`GetBoundFramebuffer`/`BindFramebufferHandle` pattern).
8. **Process:** one work order per session, on a phase branch; finish with the item's
   **Acceptance** (build + `CosmicTests` + the manual check), update the item's status banner
   here (✅ + date + one-line result). **Never run git write commands — the user commits.**

---

## 1. What Starforge IS (product definition)

**One sentence:** Starforge is the app where you assemble Cosmic scenes visually (spawn/import
models, place lights, tune rendering), attach C++ simulation logic to entities, press Play to
run it, record/plot any value like a test rig, and package the result as a standalone app.

**Pillars (ranked):**
1. **Simulation-first.** Play/pause/single-step, fixed-timestep correctness, and telemetry
   recording/plotting of any reflected property (the existing columnar telemetry + DataRecorder
   + replay stack) are core features, not afterthoughts. This is the differentiator over
   Unity/Unreal for engineering work.
2. **C++-native logic.** Scripts are real C++ classes with the full engine API and full speed —
   compiled into the project's DLL, hot-reloaded by the editor. One language across engine, sim,
   and gameplay.
3. **CAD-grade viewport feel.** The S5 investment (SolidWorks nav, ViewCube, ID-buffer picking,
   ImGuizmo, frame/snap views) is the editor's viewport from day one.
4. **Everything ships.** Any project opens in the Launcher, runs standalone via
   `--project`, and packages with the existing installer pipeline. The editor never produces an
   artifact that needs the editor to run.

**Anti-goals (v1):** no visual scripting, no AAA asset-cooking pipeline (loose files are fine),
no multi-user/cloud, no in-editor poly-modeling beyond parametric primitives, no live script
reload *during* Play (edit-mode reload only), no attempt to edit Frontier's hand-coded worlds
(Frontier predates the scene format; it stays a code showcase).

**Editor surface (where each need lands):**

| You asked for | Work order |
| --- | --- |
| Create my own models (gizmo-editable) | E15 primitives (+ P2 CSG parked) |
| Import CAD/Blender models | E16 assimp + .cmeta import settings (+ P1 STEP parked) |
| Save scenes | E2 serializer, E6 shell, E10 content browser |
| Write per-entity logic (C++ scripts) | E11 script host, E12 hot-reload build |
| Logic ↔ graphics alignment | scripts ARE components: reflected fields appear in the Inspector (E1+E8), serialize with the scene (E2), record to telemetry (E20) |
| Update rendering/lighting per scene/entity | E4 environment + camera components, E17 material editor + environment panel |
| New scenes / transitions | E5 SceneManager (async load + fade), E6 scene tabs |
| Sim + graphics extras | E13 play/pause/step, E18 terrain/water/particle authoring, E20 telemetry panel, E9 debug draw, §9 parked (physics, sequencer, CSG…) |
| Ship as an app | E19 package flow |
| Project database someday | §9 P7 (design constraint honored: self-contained folders) |

---

## 2. Architecture

### 2.1 Process topology (nothing new at the host level)

```
CosmicApp.exe ── Cosmic.dll (engine)
   │
   ├─ Launcher ──────────► any project DLL (Frontier.dll, SF_Telem.dll, …)
   │
   └─ --project Starforge ► Starforge.dll  (the editor — a plugin like any other)
                               │
                               │ opens a project folder (MyRover/)
                               ├─ reads  MyRover/project.cproj      (TOML manifest)
                               ├─ mounts project:// → MyRover/assets/
                               ├─ loads  scenes/*.cscene            (JSON, E2)
                               └─ LoadLibrary(MyRover.dll)          (game module, E12)
                                     └─ calls ONLY CosmicModule_Register(ModuleRegistry&)
                                        → script factories + custom components + reflection

Standalone run of the SAME project (no editor anywhere):
CosmicApp.exe --project MyRover
   └─ MyRover.dll::CreatePluginLayer() → Cosmic::PlayerLayer(manifest)
        └─ PlayerLayer calls CosmicModule_Register on its own module handle,
           loads the startup scene, ticks scripts. Identical sim, zero editor code.
```

**The key trick — one project DLL, two exports.** A Starforge-made project DLL exports BOTH the
existing plugin entry (`CreatePluginLayer` → engine `PlayerLayer`, so Launcher/`--project`/
`package.bat` work unchanged) AND `CosmicModule_Register` (scripts + components). The editor
never calls `CreatePluginLayer`; the standalone player never runs editor code. The engine's
`CS_REGISTER_COMPONENT` string-hash type IDs already make component types agree across the
exe/editor-DLL/game-DLL boundary — that macro was written for exactly this.

### 2.2 A project on disk (self-contained; the future-DB design constraint)

```
MyRover/
├── project.cproj            # TOML (utils/Config): name, engine min-version, startup scene,
│                            # window title/size, fixed-dt, packaging options
├── CMakeLists.txt           # generated from template (E12); standalone COSMIC_SDK pattern
├── src/
│   ├── Module.cpp           # generated: the two exports; CS_MODULE boilerplate
│   └── scripts/             # user C++ scripts (one .h/.cpp per script class)
│       └── HoverController.h/.cpp
├── assets/                  # ALL referenced by project:// paths (relocatable)
│   ├── scenes/Main.cscene   # JSON scene (E2)
│   ├── prefabs/*.cprefab    # JSON single-root subtree (E14)
│   ├── materials/*.cmat     # JSON PBR material (E2/E17)
│   ├── models/  textures/  audio/   # imported sources + .cmeta sidecars (E16)
└── build/                   # cmake out (git-ignored); DLL lands in SDK build/Runtime/<cfg>/
```

Everything a project is lives under one folder → the parked "project library/DB" (§9 P7) is an
*index over folders*, never a migration. Starforge keeps a recent-projects registry at
`user://starforge/projects.toml` for its homescreen.

### 2.3 File formats (decisions closed)

| File | Format | Why |
| --- | --- | --- |
| `.cscene` / `.cprefab` / `.cmat` | **JSON** via newly vendored **nlohmann/json** (single header, MIT) | deep nesting + arrays (TOML is wrong for entity trees); diffable text; the reflection registry (E1) makes (de)serialization one generic visitor |
| `project.cproj`, editor prefs, projects registry | **TOML** via existing `utils/Config` (toml++) | flat key/value config is what Config already does; consistent with the engine |
| Entity identity | **64-bit random UUID** (`core/UUID`, E2) stored on every entity as `IDComponent`; hex string in JSON | stable references (parenting, EntityRef script fields, prefab sources) across sessions; 64-bit is collision-safe at scene scale and cheap |
| Unknown component blocks on load | preserved as **opaque JSON** and re-emitted on save | opening a scene in an editor build that lacks some game module must not destroy data |

### 2.4 Frame/data flow inside the editor

- **Edit mode:** `EditorLayer` ticks the editor camera (Orbit-CAD / Fly toggle — both engine
  controllers exist), binds `Application::GetFrameBuffer()` (the workspace viewport FBO the
  engine already displays in the docked "Viewport" window), renders the open `Scene` through the
  engine `SceneRenderer`, then panels draw. `Scene::OnUpdate` (systems) does NOT run; scripts do
  NOT exist. Gizmo edits go through the CommandStack (E7).
- **Play mode (E13):** serialize the edit scene to an in-memory JSON snapshot → deserialize into
  a fresh runtime `Scene` → instantiate scripts (E11) → tick `OnUpdate`/`OnFixedUpdate`
  normally. Stop destroys the runtime scene and the edit scene is untouched (snapshot restore
  semantics for free, and it dogfoods the serializer every single Play). Pause/step implements
  the `docs/design/responsive-rendering-and-pause.md` Feature-B semantics.
- **Selection** flows through the existing `EntitySelection` bus (S5) so `ScenePicker` clicks,
  Hierarchy clicks, and gizmo targets stay in sync.

---

## 3. Scripting contract — C++ DLL-first (v1)

### 3.1 The script class (engine `scripting/ScriptableEntity.h`, E11)

```cpp
class COSMIC_API ScriptableEntity
{
public:
    virtual ~ScriptableEntity() = default;
protected:
    virtual void OnCreate() {}                 // after all entities of the scene exist
    virtual void OnStart() {}                  // first frame of Play, after every OnCreate
    virtual void OnUpdate(float ts) {}         // variable timestep
    virtual void OnFixedUpdate(float fixedDt) {} // sim-grade fixed timestep
    virtual void OnEvent(Cosmic::Event& e) {}
    virtual void OnDestroy() {}

    Entity GetEntity() const;                  // the owning entity (components via GetComponent<T>)
    Scene& GetScene()  const;                  // spawn/destroy/find-by-tag/find-by-uuid
    // convenience passthroughs: Input, Telemetry hooks (E20), SceneManager::Request (E5)
private:
    friend class ScriptHost;
    Entity m_Entity;                           // injected by the host before OnCreate
};
```

`NativeScriptComponent { std::string ClassName; /* runtime-only */ ScriptableEntity* Instance; }`
is the serialized link: the scene stores the *name*; the host resolves it through the
`ModuleRegistry` at Play/load. Reflected script fields (see 3.3) are serialized as a JSON object
inside the component block and pushed into the fresh instance after construction.

### 3.2 Registration (in the game module)

```cpp
// scripts/HoverController.h  (user code — this is the whole ceremony)
class HoverController : public Cosmic::ScriptableEntity
{
public:
    float TargetAltitude = 5.0f;   // shows in the Inspector, saves with the scene
    float Kp = 2.0f, Kd = 0.4f;
protected:
    void OnFixedUpdate(float dt) override;
};

// src/Module.cpp (generated once by the scaffold; user appends one line per script)
CS_MODULE_BEGIN(MyRover)
    CS_SCRIPT(HoverController,
        CS_FIELD(TargetAltitude, .Range(0.f, 100.f)),
        CS_FIELD(Kp), CS_FIELD(Kd));
    // custom components work too:  CS_COMPONENT(ThrusterComponent, CS_FIELD(MaxThrustN));
CS_MODULE_END()
```

`CS_MODULE_BEGIN/END` expand to the two exports (`CreatePluginLayer` → `PlayerLayer`,
`CosmicModule_Register`); `CS_SCRIPT`/`CS_COMPONENT` register a factory + reflection descriptor
(E1) + `CS_REGISTER_COMPONENT` specialization. Result: **a script is just a component** — the
Inspector, serializer, undo stack, and telemetry panel all get it for free.

### 3.3 Hot reload (E12) — edit mode only (v1)

Unload → rebuild → reload, with state carried across by the serializer:

1. Serialize every module-owned component (scripts + custom components) per entity to JSON.
2. Destroy those component instances; unregister module types; `FreeLibrary`.
3. Drive cmake (the roadmap's non-interactive recipe) with `-DGAME_HOT_SUFFIX=_hotN` so each
   build emits `MyRover_hotN.dll` — unique filenames sidestep Windows DLL/PDB locks entirely.
4. `LoadLibrary` the new DLL → `CosmicModule_Register` → re-add components from the JSON.
5. On build failure: keep the old module loaded, print the compiler output in the Console panel.

Gotchas documented up front: entt storage for module-owned types MUST be fully cleared before
`FreeLibrary` (dangling vtables otherwise); reload is DISABLED while Play runs (v1 — stop
first); stale `_hotN` DLLs are cleaned on project close.

### 3.4 Standalone player (`layers/PlayerLayer`, E13)

Engine-side generic layer: reads `project.cproj`, sets the VFS project, registers the module (its
own handle — no second LoadLibrary), `SceneManager::Load(startupScene)`, ticks scripts, renders
via SceneRenderer, optional pause menu. `CreatePluginLayer` in every scaffolded project returns
one. This is what makes "ship from the editor" free.

---

## 4. Lua tier — mapped now, parked until the unlock (user-requested design)

**Preamble — what Lua would buy over the C++-DLL approach (honest trade-off):**
- **Iteration speed:** edit → save → running in <100 ms, vs. a cmake+MSVC cycle (seconds to tens
  of seconds). For gameplay-flavored tuning loops this dominates.
- **Crash isolation:** a Lua error is a caught exception → red console line; a C++ script bug is
  an access violation in the editor process. (Mitigated in v1 by autosave-before-Play, not
  solved.)
- **Live-edit during Play:** interpreted scripts can be swapped mid-run safely; native hot
  reload during Play is deliberately out of scope (v1).
- **Accessibility/modding:** non-C++ users (or shipped-app end users) can write behavior without
  the toolchain; scripts become data.
- **What it costs:** a bindings surface to maintain, a second language, GC pauses, and a perf
  ceiling that is wrong for the simulation tier — which is why the **hot path stays C++
  forever**; Lua is for glue/orchestration/tuning.

**The map (parked work orders):**
- **L1 — embed + bind core.** Vendor **sol2 + Lua 5.4** (header-friendly, MIT).
  `LuaScriptComponent { std::string ScriptPath; }` parallel to `NativeScriptComponent`, same
  lifecycle callbacks. Bindings are **generated from the E1 reflection registry** — every
  registered component/field is automatically readable/writable from Lua
  (`entity.Transform.Position.y = 3`), so the binding surface maintains itself. Hand-bind only
  the ~10 service entry points (Input, SceneManager, Telemetry, spawn/destroy, log).
- **L2 — editor integration.** Script asset type in the content browser, file-watcher live
  reload (including during Play), error → Console with file:line, Lua fields exposed in the
  Inspector via a `Fields` table convention mirrored into reflection.
- **L3 — interop + budget.** Call registered C++ script methods from Lua and vice versa
  (message/event style, not direct vtables); per-frame Lua time shown in the profiler; doc page
  "which tier does my logic belong in".
- **Unlock condition (doc 05 S14 discipline):** C++ reload latency measurably hurts a real
  project's tuning loop, or a non-programmer/modding need appears. Do not build speculatively.

---

## 5. Stage A — engine seams (all headless-testable)

### E1 — Reflection registry (`Cosmic/src/reflect/`)

**Files:** NEW `reflect/TypeRegistry.h/.cpp`, `reflect/TypeDescriptor.h`; MODIFY
`scene/Components.h` (register built-ins at static-init or explicit `RegisterEngineTypes()`),
`Cosmic.h` (export); NEW `tests/test_reflect.cpp` (+ tests/CMakeLists list).

**Spec:** a runtime type registry keyed by the same string hash `CS_REGISTER_COMPONENT` uses.
Per component: display name, category, and an ordered field list
`{ name, FieldKind, byte offset or get/set thunks, UI hints (range/step/tooltip/color-flag),
flags (ReadOnly, HideInInspector, NoSerialize) }`.
`FieldKind`: Bool, Int32, UInt32, Float, Vec2, Vec3, Vec4, Quat, Color (vec4-with-flag), String,
AssetPath (string-with-flag + asset-type tag), EntityRef (UUID), Enum (name↔value table).
Registration API is builder-style
(`Reflect::Class<TransformComponent>("Transform").Field("Position", &TransformComponent::Position)…`);
the `CS_SCRIPT/CS_FIELD` macros (§3.2) are sugar over it. Also entt glue: enumerate an entity's
registered components; add/remove/copy a component by descriptor (needed by Inspector "Add
Component", serializer, undo, and the hot-reload strip/restore).
**Register every existing engine component** from `Components.h` (Tag, Transform incl. quat
policy note, SpriteRenderer, MeshRenderer, DirectionalLight, PointLight, Terrain/Water/Particle
holders as opaque-asset stubs for now) so E8's inspector is immediately full.

**Gotchas:** registry must live in the ENGINE DLL (single instance process-wide; export an
accessor, no static-in-header duplication across DLLs). `Ref<Mesh>`-type members are NOT fields —
they surface later as AssetPath fields once E16 gives them stable paths (MeshRenderer v1
reflects `Color`/`CastShadows` + an AssetPath that E2 resolves through `AssetLibrary`).

**Acceptance:** tests prove: enumerate fields of a registered type; get/set through descriptors
round-trips; entt add-by-descriptor creates a live component; unknown hash → nullptr, no crash.

### E2 — UUIDs + JSON serialization (`scene/SceneSerializer`)

**Files:** NEW `core/UUID.h/.cpp`, `scene/SceneSerializer.h/.cpp`; VENDOR
`Cosmic/dependencies/nlohmann/json.hpp` (+ engine CMake include dir); MODIFY `scene/Scene.h`
(`CreateEntity` also emplaces `IDComponent`; `FindByUUID`), `scene/Components.h`
(`IDComponent{ UUID }` + registration), `Cosmic.h`; NEW `tests/test_scene_serializer.cpp`.

**Spec:** `UUID` = 64-bit from `std::mt19937_64` seeded by `random_device` (hex string in JSON).
`SceneSerializer::Save(scene, path)` / `Load(scene, path)` walk the E1 registry — ONE generic
visitor, zero per-component code. Schema:
`{ "cosmic_scene": 1, "entities": [ { "id": "…", "components": { "Transform": {…}, … } } ] }`.
EntityRef fields serialize as target UUID; loader resolves after all entities exist (two-pass).
Unknown component keys are kept as raw JSON on the entity (opaque blob component) and re-emitted
on save. Same visitor powers `.cprefab` (single root + descendants, E14) and `.cmat`
(material params → a plain reflected struct). AssetPath fields resolve through
`AssetLibrary::Get*` on load (cache makes this idempotent).

**Gotchas:** write via a temp file + atomic rename (crash-safe saves). Float formatting: emit
shortest round-trip (nlohmann default is fine; do NOT truncate). Keep nlohmann out of public
engine headers (pimpl or .cpp-only include) — client DLL compile times.

**Acceptance:** tests: save→load→save produces identical JSON (incl. an unknown-component blob
and an EntityRef); UUID collision test over 1e6 draws; headless (no GL — use null mesh refs).

### E3 — Hierarchy (parent/child + world transforms)

**Files:** MODIFY `scene/Components.h` (`RelationshipComponent{ UUID Parent; std::vector<UUID>
Children; }` + registration), `scene/Scene.h/.cpp` (`SetParent(child,parent,keepWorldPose)`,
`GetWorldTransform(entity)` walking the parent chain, `DestroyEntity` recursion option,
iteration order helper), `Scene::OnRender3D` + `renderer/SceneRenderer.cpp` (use world
transforms), E2 serializer (already generic — Children order preserved); NEW
`tests/test_hierarchy.cpp`.

**Spec:** UUID-based links (survive serialization; no entt-handle staleness). Entities without a
`RelationshipComponent` behave exactly as today — the 2D path and every shipped app see zero
change. World transform = parent world × local; computed on demand with a per-frame memo table
in the render path (editor-scale scenes; no dirty-flag machinery in v1 — documented).
`SetParent(…, keepWorldPose=true)` rewrites the local transform so the entity doesn't jump.

**Gotchas:** cycle prevention (parenting to own descendant must refuse + warn). Quat-vs-Euler
policy: composition uses `GetTransform()` matrices — never compose Euler angles directly.

**Acceptance:** tests: three-deep chain world position; reparent keeps world pose within 1e-4
absolute; destroy-with-children; cycle refused. Engine3DDemo renders identically (flat scenes).

### E4 — CameraComponent + EnvironmentComponent

**Files:** MODIFY `scene/Components.h` (+ registrations, + E1 fields), `renderer/SceneRenderer`
(consume an environment-holder entity when present; explicit setters stay for Frontier),
`Cosmic.h`; NEW `tests/test_scene_components.cpp` (projection math).

**Spec:** `CameraComponent{ bool Primary; float FovDeg, Near, Far; enum Projection {Persp,Ortho};
float OrthoSize; }` — Play mode renders from the first Primary camera (falls back to editor cam
+ a Console warning). `EnvironmentComponent` (scene-level; editor keeps exactly one entity named
"Environment"): sun dir/color/intensity link (drives the first DirectionalLight), sky mode
(Procedural / Detailed / HDRI path), time-of-day params (the Frontier DayNightCycle recipe is
the reference, but engine-generic), fog params, IBL source + intensity, post block (exposure,
bloom on/threshold/intensity, SSAO on/radius, FXAA on, lens flare on). SceneRenderer applies it
each frame when present.

**Gotchas:** every field must default to the CURRENT SceneRenderer defaults so a scene without
the component renders unchanged (compat gate). Do not remove the programmatic setters Frontier
uses.

**Acceptance:** unit tests for projection matrices + defaults-equality; Engine3DDemo/Frontier
unchanged; a hand-written .cscene with an Environment block renders sky/fog/post as specified.

### E5 — SceneManager (async load + transitions)

**Files:** NEW `scene/SceneManager.h/.cpp`; MODIFY `Cosmic.h`; NEW `tests/test_scenemanager.cpp`
(state machine only).

**Spec:** engine service owned by whoever ticks it (EditorLayer or PlayerLayer — not a global
singleton; pattern-match `SerialLink`). `Request("project://scenes/Next.cscene", Transition::Fade)`
→ state machine: FadeOut → async parse+build on `JobSystem` (CPU side) → main-thread GPU
finalize (mesh/texture uploads — the Frontier `World::IsLoading()` split is the reference) →
swap → FadeIn. Exposes `IsLoading()/Progress()` for the LoadingScreen-style overlay; scripts
call `Request` (E11 passthrough). Also sync `Load` for the editor's File▸Open.

**Gotchas:** GL objects only on the main thread (hard rule); the async stage produces CPU-side
staging data. A Request during a transition queues (no re-entrancy).

**Acceptance:** state-machine unit test (fake loader); in-app: two scenes fade between each
other from a script button without a frame hitch >1 load-frame.

---

## 6. Stage B — the editor app (Starforge.dll)

### E6 — Editor shell v1 (project open/create, homescreen, menus, autosave)

**Files:** `Projects/Starforge/src/*` (skeleton hardening: `StarforgeApp`, `EditorContext`,
`panels/*`); template files under `Projects/Starforge/templates/` (project.cproj, CMakeLists,
Module.cpp, Main.cscene — consumed by E12's scaffold; inert until then).

**Spec:** homescreen (SF_Telem tile pattern; recent projects from `user://starforge/projects.toml`,
"New Project" wizard = name + location → scaffold from templates, "Open" = folder picker
validating `project.cproj`). On open: `FileSystem::SetActiveProject`, load startup scene via E5,
window title = project + dirty `*`. Menus: File (New/Open/Save/Save As/Recent, New Scene, Exit
to launcher), Edit (Undo/Redo — wired in E7), Entity (create menu — E8), View (panels toggle,
Reset Layout). Ctrl+S/N/O/Z/Y. Autosave: dirty scene → `user://starforge/autosave/<proj>/` every
5 min + on Play (E13 reuses); offer recovery on next open after a crash. Editor prefs
(camera speed, autosave period, theme) in `user://starforge/editor.toml`.

**Acceptance:** create project → entity edits → Save → close → reopen → identical scene;
kill -9 mid-edit → reopen offers the autosave.

### E7 — CommandStack (engine) + editor undo/redo

**Files:** NEW `core/CommandStack.h/.cpp` (engine, generic: `ICommand{Do,Undo,Merge?}`, ring
depth ~256, coalescing key, dirty-marker hook); NEW `tests/test_commandstack.cpp`; Starforge:
`src/commands/*` — concrete commands: ReflectedFieldEdit (any E1 field: before/after bytes),
TransformEdit (gizmo-coalesced: one command per drag), CreateEntity/DestroyEntity (E2 JSON
snapshot incl. children), Add/RemoveComponent (descriptor + JSON), Reparent, RenameTag,
multi-selection batch wrapper.

**Spec:** every mutation in Starforge goes through the stack from this item on (Inspector edits,
gizmo drags, hierarchy ops). Gizmo: begin-drag captures "before", end-drag pushes one command.
Inspector `DragFloat`: coalesce while `IsItemActive`, commit on deactivate-after-edit.

**Gotchas:** DestroyEntity undo must restore the SAME UUIDs (E2 snapshot does) or later commands'
entity references dangle — commands reference entities by UUID, never by `entt::entity`.

**Acceptance:** engine tests (do/undo/redo/merge/depth-overflow); in-app: drag gizmo → Ctrl+Z
restores exact prior pose; delete a 5-entity subtree → undo restores hierarchy + selection.

### E8 — Hierarchy panel + reflection-driven Inspector

**Files:** Starforge `panels/HierarchyPanel.*` (upgrade), `panels/InspectorPanel.*` (rewrite on
E1), `src/widgets/PropertyRows.h` (shared field-kind → widget mapping).

**Spec:** Hierarchy: E3 tree (indent guides, expand state persisted per session), click/ctrl/
shift select (multi via `EntitySelection`), drag-drop reparent (Esc cancels), context menu
(Create Empty/child, primitives (E15 fills), lights, camera, duplicate Ctrl+D w/ fresh UUIDs,
delete, rename F2), search filter. Inspector: for each registered component of the selection —
collapsing header + per-field widget by FieldKind (Vec3 = xyz drag row w/ per-axis color chips;
Color = picker; AssetPath = slot accepting content-browser drag (E10) + "locate" button;
EntityRef = drag an entity from Hierarchy; Enum = combo; ranges/tooltips from hints), remove-
component context item, "Add Component" popup listing registry by category (INCLUDING game-module
scripts once E11/E12 land — they're just registry entries). Multi-select: common components,
mixed values show `—`, edits fan out as one batch command.

**Acceptance:** every built-in component fully editable with undo; a fake registered test type
appears in Add Component with correct widgets; no direct component writes remain outside
commands.

### E9 — Viewport tools: picking, gizmo-undo, DebugDraw grid, view modes

**Files:** engine NEW `renderer/DebugDraw.h/.cpp` (immediate line batch: `Line/WireBox/WireSphere/
Axes/Grid(spacing,extent)`, flushed inside SceneRenderer's main pass, depth-tested toggle);
Starforge `src/ViewportController.*` (wire `ScenePicker` click + `Gizmo::Manipulate` inside
`BeginViewportOverlay` — Engine3DDemo's S5 wiring is the copy-paste reference), toolbar strip
(translate/rotate/scale, local/world, snap values, camera Fly/Orbit toggle, speed), camera
bookmarks (Ctrl+1..9 save / 1..9 recall), view-mode menu (Lit / Wireframe via the existing
fill-mode verb / ID-buffer visualize for debugging).

**Spec:** LMB pick (ScenePicker, additive w/ Ctrl), W/E/R gizmo modes (viewport-hovered only),
F frames selection (S5.2 exists), grid = DebugDraw::Grid at Y=0 with 1 m spacing (10 m major
lines), axes triad at origin. All gizmo edits push E7 TransformEdit commands.

**Gotchas:** ScenePicker needs the ID attachment — confirm `Application::GetFrameBuffer()`
carries the S4.6 entity-ID attachment when the editor requests it (Engine3DDemo precedent; if
its FBO spec differs, add the attachment behind an opt-in, not globally). DebugDraw ships engine-
side but must be a no-op unless called (compat gate).

**Acceptance:** click-select exact mesh under cursor incl. occlusion; box/sphere/light billboards
selectable (billboard pick = ID quad); grid/axes render; Frontier + Engine3DDemo unchanged.

### E10 — Content Browser + FileWatcher + asset ops

**Files:** engine NEW `utils/FileWatcher.h/.cpp` (`ReadDirectoryChangesW` thread → main-thread
event queue; tested with a temp dir); Starforge `panels/ContentBrowserPanel.*` (rewrite).

**Spec:** tree + grid views of `project://` (assets root); type icons (Lucide) + texture
thumbnails (`AssetLibrary::GetTexture` + ImGui::Image; mesh/material thumbnails parked to E17's
preview rig); double-click: scene→open (save prompt), material→inspector, texture→popup preview;
drag sources: AssetPath payload for Inspector slots (E8), scene-drop spawns (model → entity with
MeshRenderer at picked point, prefab → instantiate (E14)); context menu: New Folder/Material/
Scene/Prefab-from-selection, rename, delete (recycle bin via `SHFileOperation`, not hard delete),
show-in-explorer; import = drag OS files in (copy + E16 importer once it lands). FileWatcher
refreshes the panel + hot-reloads changed textures through `AssetLibrary` (re-upload in place so
existing `Ref`s see it — add `AssetLibrary::Reload(path)`).

**Acceptance:** edit a texture in an external app → viewport updates within a second; all asset
ops undo-safe where feasible (rename/delete are NOT undoable — confirm-dialog instead,
documented).

---

## 7. Stage C — logic & play

### E11 — Script host (engine `scripting/`)

**Files:** NEW `scripting/ScriptableEntity.h`, `scripting/ModuleRegistry.h/.cpp`,
`scripting/ScriptHost.h/.cpp`, `scripting/ModuleMacros.h` (`CS_MODULE_BEGIN/END`, `CS_SCRIPT`,
`CS_COMPONENT`, `CS_FIELD`); MODIFY `scene/Components.h` (`NativeScriptComponent`), `Scene.h`
(script tick hooks called by ScriptHost), `Cosmic.h`; NEW `tests/test_scripthost.cpp` (register
a script in-exe — no DLL needed — instantiate, tick, field push/pull, destroy).

**Spec:** per §3.1–3.2. `ScriptHost::Instantiate(scene)` resolves every `NativeScriptComponent`
by ClassName → factory → inject entity → push reflected field values → `OnCreate` all →
`OnStart` all; `Tick(ts)` / `FixedTick(dt)` / `Destroy(scene)`. Unresolved ClassName = Console
error + component kept inert (never a crash). Field pull-back on Stop is NOT done (edit scene is
authoritative — documented).

**Acceptance:** headless test drives the full lifecycle; a test script mutating its Transform
moves an entity in a scripted tick loop.

### E12 — Game-module build & hot reload (editor)

**Files:** Starforge `src/GameModule.*` (LoadLibrary/registry lifecycle per §3.3),
`src/BuildRunner.*` (JobSystem-backgrounded cmake configure+build, streamed to Console panel),
E6's `templates/` become live (scaffold fills name tokens — the Launcher's
`GenerateProjectTemplate` replaceAll pattern is the reference); template `CMakeLists.txt` gets
`GAME_HOT_SUFFIX` support (`OUTPUT_NAME ${PROJECT_NAME}${GAME_HOT_SUFFIX}`).

**Spec:** per §3.3 numbered steps. Toolbar: "Build Scripts" (Ctrl+B) + auto-build toggle
(FileWatcher on `src/`); status chip (building / ok / N errors). First open of a project with no
built module: prompt to build. Editor locates cmake via the vswhere recipe (cache the path in
editor.toml).

**Gotchas:** all module-owned entt storages cleared BEFORE FreeLibrary (E1's descriptor
enumeration makes this precise); compile errors must round-trip readable (UTF-8, no PowerShell
stderr-wrap — use the roadmap's Tee pattern); never auto-reload while Play runs.

**Acceptance:** edit `HoverController.h` gain value → Ctrl+B → reload keeps entity's serialized
field values; introduce a compile error → old module keeps running + error visible; 20
consecutive reloads leak no handles (Process Explorer check documented).

### E13 — Play / Pause / Step + PlayerLayer

**Files:** engine NEW `layers/PlayerLayer.h/.cpp` (§3.4); engine: implement
`docs/design/responsive-rendering-and-pause.md` Feature B semantics if not yet present
(`Application::SetPaused` — sim frozen, UI+render live) honoring that doc; Starforge
`src/PlayState.*` + toolbar (▶ ⏸ ⏭ ⏹), viewport border tint in Play (the universal cue).

**Spec:** Play = snapshot-serialize edit scene → build runtime scene → `ScriptHost::Instantiate`
→ tick loop (fixed dt from project.cproj). Pause freezes `OnUpdate/OnFixedUpdate` (render+panels
live; Inspector edits write the RUNTIME instances — discarded on Stop, standard editor
semantics). Step = advance exactly one fixed step. Stop = destroy runtime scene + scripts,
edit scene never moved. Camera: Play renders from the Primary CameraComponent (E4);
"eject" toggle returns the editor camera while sim continues. Autosave fires on every Play
(E6). PlayerLayer makes `CosmicApp --project MyRover` run the same scene standalone.

**Acceptance:** Play→Stop round-trips a complex scene byte-identically (serializer diff);
pause+step advances deterministic sim exactly one dt (telemetry timestamps prove it); the
scaffolded template project runs standalone from the Launcher with zero edits.

### E14 — Prefabs v1

**Files:** engine: `SceneSerializer` subtree save/instantiate (mostly exists via E2 visitor);
MODIFY `scene/Components.h` (`PrefabComponent{ std::string SourcePath; }`); Starforge: hierarchy/
content-browser hooks.

**Spec:** "Save as Prefab" (selection root → `.cprefab`, fresh UUIDs on instantiate, hierarchy
preserved); instantiate via content-browser drag into viewport/hierarchy; instance ops: "Apply
to Prefab" (overwrite asset from this instance) and "Revert to Prefab" (re-instantiate in
place, keep root transform). NO per-field override tracking and NO live propagation to open
instances in v1 (documented as the v2 upgrade with the field-level diff design sketched in the
work order).

**Acceptance:** rover prefab with children + scripts instantiates 10× with unique UUIDs; apply/
revert round-trip; scenes referencing a missing prefab load with a placeholder + warning.

---

## 8. Stage D — content & rendering tools, ship, sim

### E15 — Parametric primitives + material assignment

**Files:** engine MODIFY `graphics/Mesh.h/.cpp` (+`CreateTorus(radius, tubeRadius, segs, sides)`
— the only missing factory), NEW `scene/Components.h` `PrimitiveMeshComponent{ enum Shape; params…;
bool Dirty; }` (+E1 registration incl. ranges); Scene render path regenerates the sibling
`MeshRendererComponent.MeshAsset` when Dirty (editor sets Dirty via Inspector edits — normal
reflected fields); Starforge: Entity▸Create menu (Box/Sphere/Cylinder/Cone/Plane/Torus at the
picked point or 5 m ahead), default PBR material.

**Spec:** primitives serialize by PARAMS (mesh rebuilt on load — scenes stay tiny and text-
diffable). Editing `Segments` in the Inspector live-rebuilds the mesh (undo-safe via E7 field
commands). Assigning a `.cmat` via the Inspector AssetPath slot switches from the default
material.

**Acceptance:** create each shape; edit params w/ undo; save/reload rebuilds identical geometry
(vertex-count + AABB assertions in a headless test for the factories, incl. torus normals).

### E16 — assimp import pipeline (Blender/CAD meshes in)

**Files:** VENDOR `Cosmic/dependencies/assimp` (static lib, importers trimmed to
FBX/OBJ/STL/DAE/PLY — glTF stays cgltf; turn OFF exporters/tests in its CMake); engine NEW
`assets/MeshImport.h/.cpp` (`Import(sourcePath, ImportSettings) → Ref<Model>`), `.cmeta` sidecar
(TOML: scale factor, up-axis, merge-meshes, generate-normals, flip-UVs) written next to the
copied source; `AssetLibrary::GetModel` learns non-glTF extensions via MeshImport; Starforge:
import dialog (drag-in or menu; defaults: STL mm→m ×0.001 CAD preset, FBX cm→m ×0.01, DAE/OBJ
×1; up-axis auto from format metadata where present).

**Spec:** import = copy source into `project://models/` + write `.cmeta` + load through the
importer; FileWatcher re-imports on source change. Materials: FBX/OBJ materials map to `.cmat`
files on first import (diffuse/normal/roughness textures copied alongside); STL gets the default
material. Multi-mesh sources become one parent entity + child MeshRenderers (E3).

**Gotchas:** assimp is the phase's only heavyweight dep — pin the version in the vendored
README; build it once as a static lib linked into Cosmic.dll (no new runtime DLL). Units are THE
CAD trap — the settings live in `.cmeta` so re-import is deterministic; never guess silently
(show the assumed scale in the dialog).

**Acceptance:** a Blender-exported FBX + glTF of the same object land at identical world size; a
SolidWorks-exported STL (mm) imports at correct meters; re-import after editing `.cmeta` scale
updates the placed entities.

### E17 — Material editor + Environment panel (+ preview rig)

**Files:** Starforge `panels/MaterialEditorPanel.*`, `panels/EnvironmentPanel.*`,
`src/PreviewRig.*` (tiny offscreen FrameBuffer + SceneRenderer-lite: one mesh, key light, IBL;
also generates content-browser thumbnails for meshes/materials — E10's parked half).

**Spec:** Material editor: edit `.cmat` (reflected struct → auto-UI: albedo/normal/rough/metal/
AO/emissive slots + factors), live preview sphere/box/imported-mesh toggle, save/revert, undo.
Environment panel: E4's component via reflection UI + curated layout (ToD slider with sun
preview, sky mode combo incl. night/moon (SkyDetail exists), fog, IBL rebake button, post
toggles incl. lens flare). Both write through E7 commands.

**Acceptance:** author a rusted-metal .cmat from imported textures, assign to an E16 model, tune
environment to a sunset w/ bloom — all undoable, all persisted, Frontier untouched.

### E18 — World-systems authoring (terrain / water / particles)

**Files:** Starforge `panels/WorldSystemsPanel.*` (+ small per-system editors); engine ONLY IF
gaps appear while wiring (each must be its own noted deviation).

**Spec (v1 = parameters, not brushes):** Terrain: create/edit `TerrainComponent` via a reflected
recipe (size/resolution, noise/ridged params + optional heightmap import → bake through the
existing packed height/normal texture path, 4 splat layer texture slots, snow band) + regenerate
button (JobSystem + progress). Water: ocean/lake presets (engine `water/Presets.h` exists),
Gerstner stack params, v2 feature toggles, reflection setup handled by SceneRenderer. Particles:
emitter editor over the reflected `EmitterDesc` (spawn rate, lifetime, velocity cone, size/color
curves as LookupTable refs, soft/flipbook/stretch flags), live preview in-scene, save as
`.cemitter` preset + load. Sculpt/paint brushes = §9 P5 (parked).

**Acceptance:** from an empty scene, author an island-ish terrain + ocean + a campfire emitter
purely in-editor, save, reload, Play — no code written.

### E19 — Package & ship

**Files:** Starforge `src/Packager.*` + Package dialog (menu: File▸Package…); template addition:
per-project icon slot (parked if .rc plumbing fights back — note it).

**Spec:** pipeline (all existing machinery, orchestrated): ensure Release build of engine +
project DLL (BuildRunner) → stage `dist/<Project>/`: `CosmicApp.exe` (renamed `<Project>.exe`),
`Cosmic.dll`, `<Project>.dll`, `assets/` (engine assets + `assets/projects/<Project>/`),
`boot.cfg` (project name — Application already boots a named project; add reading `boot.cfg`
next to the exe as the no-args default if `--project` absent: ~10-line engine change, flagged in
the work order) → optional zip. "Open dist folder" button.

**Acceptance:** package the template project; copy `dist/` to a machine/path with NO
`COSMIC_SDK` and no repo; double-click runs straight into the scene (no Launcher).

### E20 — Simulation instrumentation (telemetry panel)

**Files:** Starforge `panels/TelemetryPanel.*`; engine `scripting/ScriptableEntity` telemetry
passthrough (thin — the columnar telemetry system + DataRecorder + replay all exist).

**Spec:** any reflected numeric field of any entity/script can be marked **Recorded** (right-
click in Inspector or drag into the panel): during Play the panel samples per fixed step into
the existing telemetry storage; live ImPlot scopes (docked, multi-plot, pause/scroll); Stop
keeps the take — scrub the timeline to inspect values (values only, not scene state — v1
documented); export CSV via the existing exporter; takes autosave to `user://starforge/takes/`
with the DataRecorder failsafe pattern. Scripts can also push custom channels
(`Telemetry().Push("thrust_N", v)`).

**Acceptance:** record a bouncing-ball script's height + a PID script's error for 30 s → live
plots during Play, CSV matches fixed-dt sample count exactly, take reloads after editor restart.

### E21 — Polish, stats, docs, acceptance demo

**Files:** Starforge misc; `docs/design/starforge-ui.md` (user guide); README §1.5 additions
(only if new scripts/flags appeared — E19's `boot.cfg` counts); update this doc + roadmap
banners.

**Spec:** scene stats overlay (entities, draws, culled count — S12.1 counters exist, GPU
profiler panel docked from F3), keyboard-shortcut reference window, crash-safe `.bak` rotation
on save, theme pass (Starforge accent via ThemeManager), empty-state hints in every panel,
first-run sample project ("Forge Playground": primitives + a script + terrain + water + an
emitter + telemetry take).
**Phase acceptance demo (the whole point, recorded):** from a fresh launch — new project →
import a Blender FBX + a CAD STL → build a small scene (terrain, water, lights, environment
sunset, primitives) → write `HoverController.h` in the editor-scaffolded project → hot-reload →
Play with live telemetry plots → Stop → Package → run the shipped exe on a clean path. That
recording is Phase 13's definition of done.

---

## 9. Parked (each with its unlock, S14 discipline)

| # | Item | Sketch | Unlocks when |
| --- | --- | --- | --- |
| P1 | **STEP/CAD B-rep import** | separate `tools/step2gltf` exe on OpenCascade (tessellation quality flags, assembly→hierarchy, units from file); editor shells out, then E16 ingests the glTF | a real STEP-only workflow appears (STL/FBX insufficient) |
| P2 | **CSG booleans** | vendor `manifold` (MIT); Union/Subtract/Intersect on primitive/imported solids → baked Mesh + kept recipe for re-edit | modeling-in-editor outgrows primitives |
| P3 | **Physics (Jolt) gate** | vendored Jolt; RigidBody/Collider components (reflected structs — E1/E2 make them serialize/inspect for free); fixed-step integration beside ScriptHost | a project needs contacts/stacks/ragdolls (sim 6DOF stays app-side per doc 03) |
| P4 | **Sequencer/cinematics** | keyframe tracks on reflected fields + camera cuts; LookupTable curves + a timeline panel | trailer/demo-recording need |
| P5 | **Terrain sculpt/splat brushes** | RTT brush stamps into the height/splat textures + undo tiles | param terrain stops being enough |
| P6 | **Binary asset pack** | single-file pak + index for shipping (loose files fine today) | shipped-app size/IO matters |
| P7 | **Project library / database** | index over self-contained project folders (§2.2 constraint); SQLite or service later; homescreen already reads a registry file | multiple machines / team / cloud sync |
| L1–L3 | **Lua tier** | §4 | §4's unlock |

---

## 10. Order & dependencies

```
E1 ─► E2 ─► E3 ─► E4 ─► E5          (Stage A: engine seams, strictly ordered)
E2 ─► E6 ─► E7 ─► E8 ─► E9 ─► E10   (Stage B: editor core; E6 needs E2+E5, E8 needs E1/E3/E7)
E1 ─► E11 ─► E12 ─► E13 ─► E14      (Stage C: E11 needs E1/E2; E13 needs E5/E11; E14 needs E2)
E8 ─► E15   E10 ─► E16 ─► E17 ─► E18   E13 ─► E19   E11 ─► E20   all ─► E21
```
Suggested execution: straight E1→E21. E15/E16 can interleave anywhere after their arrows if a
session wants variety. ~21 sessions ≈ Frontier's F1–F17 scale.

---

## Kickoff prompt (paste to start each implementation session)

> You are implementing ONE work order from `docs/plans/11-phase13-starforge-plan.md` in
> `C:\dev\Cosmic`. Read that doc's §0 execution notes first — they bind. Then read your work
> order (E__) fully, including Files/Spec/Gotchas/Acceptance. Read the referenced engine files
> by content before editing (code moves). The compiling `Projects/Starforge` skeleton has
> `TODO(E__)` markers at every wiring point — search for yours. Rules that bite: engine gains
> only generic modules (never Starforge-specific names); CosmicTests links engine-only; the
> compat gate (Engine3DDemo/Frontier/SF_Telem/ViperSim unchanged) is non-negotiable; build with
> the non-interactive cmake recipe from `00-MASTER-ROADMAP.md`; never run git write commands —
> finish with the Acceptance procedure + update the work order's status banner (✅ + date + one
> line).
