# Phase 29 Plan — Engine Split: pure-2D build, `engine-2d` branch, pluggable physics

> **STATUS 2026-07-24 — ☐ PLANNED, nothing implemented.** This document is the complete,
> self-contained work order set for splitting Cosmic into two build configurations (full 3D and
> pure 2D), cutting the `engine-2d` branch, giving physics a swappable backend seam, and
> hard-testing **both** engines. Every file:line anchor below was verified against `HEAD`
> (`30150b3`) on 2026-07-24.
>
> **Created 2026-07-24** from a user request: *"I want a Pure 2D engine branch and the extended
> 3D branch… all the current 2D features implemented in this 3D branch should also go into the
> 2D branch… I also want to hardcore test the 2D branch in detail."* Followed by: complete
> separation (flags or deletion, my call); Jolt or any physics may ship, **but the user must be
> able to write their own later for a single app**; the assistant plans and performs the repo
> rework; SF_Telem must keep working; `build.bat` on the 2D engine must be measurably faster;
> both engines get a rigorous test plan; documentation is planned now and written last.
>
> **Work orders:** W0–W10, one per phase. Each is independently executable — §12 gives every
> phase its own context, anchors, gotchas, DoD, verification commands, and a copy-paste prompt.
>
> **Depends on:** nothing. This phase is pure refactor + tooling; it adds no gameplay feature.
> It does not block Phase 28 (Forge Isle) — but Forge Isle is 3D and stays a `main`-only project.

---

## 0. Execution notes

Roadmap build recipe (§12 restates it per phase). Doc 13 §0 engine rules apply. **The compat
gate is absolute here:** the 3D build must be behaviour-identical and pixel-identical at every
phase — this is a code-motion refactor, not a feature. New GPU state via `RendererAPI` verbs +
`BindingPoints.h`; `tests/check_gl_conformance.ps1` green in both configurations.

**Re-verify before edit.** Every line number in this document is a *starting point*. Quoted code
moves. Find the target by content (function name, comment text, member name) and confirm the
surrounding lines match before editing. Several anchors in the first draft of this plan were off
by 5–7 lines; the ones below are corrected, but they will drift again as work lands.

**Git.** The assistant runs local git — branch, checkout, merge, commit — with commits authored
by the repo's configured identity (`kdadabhoy`) and **no `Co-Authored-By: Claude` trailer**. The
assistant **never pushes**. Every `git push` is the user's, called out explicitly in §12 and §13.
This is a deliberate, user-granted exception to the standing "no git writes" rule for this phase
only.

**Both configurations stay green.** No phase may land with the 3D build broken, and from W6
onward no phase may land with the 2D build broken. This is the single most important rule in the
document.

---

## 1. Why, and the decisions of record

Cosmic's 2D feature set — sprites, sprite animation, tilemaps, 2D lights, canvas UI, flow/story
graphs, the 2D camera rig — was built during the 3D era, *inside files shared with 3D*:
`Scene.cpp`, `Components.h`, `TypeRegistry.cpp`, `SceneRenderer`, `ScriptableEntity.h`. Nothing
regressed; the 2D stack simply has no independent existence. A 2D game today drags terrain,
voxel, water, nav, assimp and the whole 3D renderer through the compiler and into the DLL.

**Locked decisions (user, 2026-07-24):**

| # | Decision |
|---|---|
| 1 | **Complete separation** between 2D and 3D — mechanism the assistant's call (§3 chooses flag-after-partition). |
| 2 | `main` becomes the **3D trunk** (fast-forwarded from `phase-7-3d-foundations`); **`engine-2d`** is cut from it. |
| 3 | **Jolt ships on both branches**, plus a **pluggable backend seam** so the user can write their own physics for a single app (§6). |
| 4 | **SF_Telem, ViperSim and the tooling apps live on both branches**, working. SF_Telem is the canary for the umbrella-header fences. |
| 5 | **Particles are excluded from the 2D build in v1.** `ParticleEmitterComponent` still round-trips opaquely, so scenes are safe. A 2D-native particle path is future work. |
| 6 | **GitHub Actions is untouched.** No workflow edits. CI keeps watching `main` with the 3D config only. |
| 7 | **Cross-branch changes are carried manually.** `git merge` remains available and near-conflict-free by construction, but nothing in the design depends on it. |
| 8 | Commits authored as the user, **no Claude trailer**; assistant runs local git, never pushes. |
| 9 | **`build.bat` in the 2D tree must be measurably faster** — a recorded gate, not an assumption (§5). |
| 10 | **Both engines get rigorous post-separation testing**, not just 2D. |
| 11 | **Documentation is planned now (§10), written in W10** after the separation is verified working. |

---

## 2. Verified starting state (2026-07-24, `HEAD` = `30150b3`)

- Working tree **clean**. Current branch `phase-7-3d-foundations`.
- `main` = `a1daece` ("Glad Fixes and Phase 7 More Explicitly Planned", 2026-07-02).
- `git merge-base main phase-7-3d-foundations` = `a1daece` — **`main` is a strict ancestor**.
  `git rev-list --left-right --count main...phase-7-3d-foundations` = `0  44`. **Fast-forwardable.**
- `origin` = `https://github.com/kdadabhoy/Cosmic.git`. Remote branches: `main`,
  `phase-7-3d-foundations`, plus two stale ones (`SF-CrashFixes` `59252f7`, `SF-Improvements`
  `fa6ed9f`) that predate `main`'s HEAD and are ignored by this plan.
- **`Projects/SF_Telem` has an identical tree hash on both branches** (`3d743d7e…`) — zero
  SF_Telem changes across all 44 commits.
- **No `CMakePresets.json` exists.** Configuration is via the `.bat` scripts.
- `tests/CMakeLists.txt` **hand-lists 59 `.cpp` files** (L7–65), exactly matching what is on
  disk. One `add_test(NAME CosmicTests …)` at L80–83; doctest runs all suites inside it.
  Suite size at HEAD: **355 cases**.
- `.github/workflows/ci.yml` triggers on `main` **only** — the current working branch has no CI.
  `release.yml` is `workflow_dispatch` and throws if `build/Runtime/Release/<app>.dll` is missing.
- Source-file census (drives §5): engine `src/` = **104 `.cpp`**; vendored = assimp **159**,
  Jolt **133**, recastnavigation **26**, glfw 68, imgui 7, implot 3.
- Doc numbering: `27-phase28-flagship-sample-plan.md` = doc 27 / Phase 28 ⇒ **this is doc 28 /
  Phase 29**. Documentation work orders run through **D40** ⇒ new ones start at **D41**.

---

## 3. Mechanism — flag-after-partition

Partition the chokepoint files **on the trunk** so every translation unit is classifiable
*shared* / *2D* / *3D*. Then `COSMIC_2D_ONLY=ON` excludes the 3D file set via CMake
`list(FILTER … EXCLUDE REGEX …)`, plus `#ifndef COSMIC_2D_ONLY` fences in the handful of shared
headers that must mention both worlds. The compile definition is **PUBLIC on the `Cosmic`
target** so Starforge, every project, and the tests all see it.

**Why not delete the 3D code on `engine-2d`?** Because git treats *modified-on-main /
deleted-on-2d* as a conflict, per file, forever — and manual porting would mean reconstructing
missing context on every carry-over. Flag-after-partition keeps **both branches carrying
byte-identical tracked files**: the only thing that differs between them is the build cache and
which preset you pick. Carrying a change across is copying the same file to the same path.

**Why not a soft "2D preset" flag with no partition?** Because then the 2D build still compiles
every 3D TU — no build-time win, no honest separation, and decision 1 explicitly asked for
complete separation.

The 3D build is affected only by pure code motion plus fences that are inert when the flag is
off. That is what the golden-image baselines in W2 exist to prove.

---

## 4. What the 2D build excludes

| Category | Excluded when `COSMIC_2D_ONLY=ON` |
|---|---|
| Engine dirs | `src/terrain/`, `src/voxel/`, `src/water/`, `src/nav/`, `src/particles/` |
| `renderer/` | `Renderer3D.*`, `EnvironmentMap.*`, `ShadowMap.*`, `CoverageCapture.*`, `InstanceSet.*` |
| `graphics/` | `Model.*`, `Skeleton.*`, `AnimationClip.*`, `CgltfImpl.cpp` — Mesh/Material/Buffer/Font/Gizmo/Texture/Shader **stay** (generic GPU infra) |
| `scene/` | new `Scene3D.cpp`, `SceneNav.*`, `ScenePicker.*`, `WorldSystemRecipes.*`, new `Components3D.h` |
| `reflect/` | new `TypeRegistry3D.cpp` |
| `assets/` | `MeshImport.cpp` (`AssetLibrary` stays) |
| Vendored | **assimp (159 TUs)** and **recastnavigation (26 TUs)** — `add_subdirectory` not even configured. **Jolt (133 TUs) stays.** |
| Projects | `Frontier`, `Engine3DDemo`, `ForgeIsle` (root-scanner skip-list) |
| Starforge TUs | `panels/VoxelPanel.*`, `panels/WorldSystemsPanel.*`, `editors/AnimationEditor.*` |

### 4.1 `src/physics/` is kept in full — and that deletes a lot of planned work

Because decision 3 keeps Jolt on both branches, the physics stack is **shared**, not 3D-only.
Consequences, all verified:

- `Scene.h`'s physics block (**95–119**, incl. `GetPhysics()` at 119) — **unfenced, unchanged**.
- `Scene.cpp`'s `OnPhysicsStart` **130–134**, `OnPhysicsStep` **136–140**,
  `DispatchPhysicsEvents` **142–146**, `OnPhysicsStop` **148–155** — **stay in `Scene.cpp`**.
- `Scene.h:401` `std::unique_ptr<ScenePhysics> m_Physics` — unfenced.
- `PlayerLayer.h:36` include and `PlayerLayer.h:75` `PhysicsWorld m_Physics` (by value) and its
  call sites at **112**, **170**, **173**, **187**, **195**, **292**, **294** — unfenced.
- `StarforgeApp.h:247` `Cosmic::PhysicsWorld m_Physics` and the play-session sites
  (**660–661**, **696**, **698**, **756**, **763**, **786–788**, **799–801**) — unfenced.
- `Components.h:11` `#include "physics/PhysicsTypes.h"` — **stays**.
- `ScriptableEntity.h:33` `#include "physics/ScenePhysics.h"` — **NOT fenced** (33 is physics;
  34–37 are voxel/nav and *are* fenced).
- `PhysicsProxy` (**116–171**), `Physics()` (**172**), `CharacterProxy` (**175–190**),
  `Character()` (**191**) — unfenced. No gameplay script changes anywhere.
- Five of the six `test_physics_*` files stay in the 2D suite.

Only the genuinely-3D collider paths inside `ScenePhysics.cpp` get fenced (§6.3).

### 4.2 Audit-at-implementation set

Likely kept in 2D; confirm each has no 3D linkage before deciding: `camera/PerspectiveCamera`,
`camera/OrbitCameraController`, `camera/FlyCameraController`, `camera/NavigationCube`,
`graphics/Gizmo`, `math/Frustum`, `src/CosmicPCH.h`.

---

## 5. Build-time budget — the measured gate

### 5.1 Where the time goes

1. **Vendored dependencies — the dominant term.** assimp (**159** TUs, template-heavy) and
   recastnavigation (**26**) are never configured: **185 of ~396 vendored TUs vanish**. Jolt's
   133 stay by decision 3.
2. **Engine + project TUs.** ~**22 of ~104** engine `.cpp` files excluded, and they skew heavy
   (`Renderer3D`, `VoxelMesher`, `Terrain`, `Water`, `Model`, `SceneNav`). Three whole projects
   skipped — `Frontier` alone is 27 files. Starforge loses 3 TUs.
3. **Header cost — the incremental win.** Splitting `Components.h` stops the 15 3D component
   definitions, and the `Skeleton.h` / `AnimationClip.h` / `ParticleSystem.h` includes they drag,
   from being parsed by **every** TU that includes `Components.h`. Same for the `Cosmic.h`
   umbrella, which every project header pulls.

**Target: ≈40–55% off a clean build.** Recorded, not assumed — W2 captures the 3D baseline and
W8 captures the 2D number, both written into `docs/systems/build-2d-3d-split.md`. If the
measurement lands materially short, report it and evaluate `COSMIC_WITH_JOLT=OFF` (another 133
TUs) rather than quietly accepting it.

### 5.3 Recorded result — the split MISSED its target, and `/MP` is why

**The partition alone came in at ≈20–25% off a clean Release build, against a 40–55% target.**
Reported rather than quietly accepted, per the rule above. The shortfall is **not** the file
partition failing to exclude what it claimed — the 2D tree configures **50 `.vcxproj` targets
against the 3D tree's 78**, neither assimp nor Recast is configured, and the TU count drops
572→347 (−39%). The excluded work is genuinely gone; wall time simply did not track TU count.

**Root cause: `/MP` (multi-processor compilation) existed in exactly one place in the whole
build — `Cosmic/dependencies/assimp/CMakeLists.txt:301`.** Every other target, the engine
included, compiled its translation units **strictly serially**. `cmake --build --parallel` does
not compensate: on the Visual Studio generator it gives MSBuild parallel *projects*, not
parallel *files*, and this build is a deep dependency chain of relatively few projects. So the
3D baseline was inflated by assimp being the only component allowed to use the whole machine,
and the split removed *the cheapest TUs per unit of wall-clock* while leaving every serial
bottleneck (Jolt 133, Cosmic ~100, Starforge, CosmicTests) intact.

**`/MP` has since been made global** — one `add_compile_options(/MP)` at MSVC scope in the root
`CMakeLists.txt`, ahead of every `add_subdirectory`, so it reaches the engine, the projects, the
tests and the vendored dependencies alike.

#### The measured numbers

Clean Release build (deleted `build/`, configure + build), 16 logical cores, same machine, same
session, measured on the **post-W9 tree**. Both configurations 0-warn, suites green.

| Configuration | without `/MP` | with `/MP` | cut |
|---|---|---|---|
| 3D (`C:\dev\Cosmic`) | 546.4 s | **169.4 s** | **−69.0%** |
| 2D (`C:\dev\Cosmic-2D`) | 411.7 s | **123.1 s** | **−70.1%** |

Derived from the same four runs:

| Comparison | Result |
|---|---|
| The 2D partition alone, no `/MP` (546.4 → 411.7) | **−24.7%** — the honest "split only" number |
| The 2D partition measured on top of `/MP` (169.4 → 123.1) | **−27.3%** |
| `/MP` alone, 3D (546.4 → 169.4) | **−69.0%** |

**`/MP` is worth roughly 2.8× the entire engine split, and costs no functionality.** That is the
headline finding of the build-time work, and it is independent of Phase 29 — the build was
simply never parallel.

**Why the split's number moved between W8 and now.** W8 measured the partition at −20.3% and the
partition-on-top-of-`/MP` at −36.4% (169.9 → 108.1). Re-measured after W9, the second figure is
−27.3% instead. The 3D `/MP` build is unchanged (169.9 → 169.4) but the 2D one grew 108.1 →
123.1. The reason is that with `/MP` the build is governed by the **critical path**, not total
work: in 3D, assimp and Jolt still dominate and W9's five new doctest-heavy test TUs disappear
into parallel slack, whereas in 2D assimp is gone, `CosmicTests` sits much closer to the
critical path, and those same TUs land on it directly. Expect the 2D figure to stay sensitive to
test-suite growth in a way the 3D figure is not.

**Other levers, for the record.** `COSMIC_WITH_JOLT=OFF` measured 387.7 → 284.0 s (−27%) on the
pre-`/MP` 2D tree, and `/MP` + Jolt-off together reached 97.1 s. Jolt-off costs a feature, so it
stays available rather than adopted.

**Gotcha, hit once during this work:** do **not** express the flag as
`-DCMAKE_CXX_FLAGS=/MP`. That *replaces* CMake's MSVC defaults (`/DWIN32 /D_WINDOWS /EHsc`),
silently disabling exceptions, which surfaces as 222 doctest static-assert failures rather than
anything that mentions flags. Use `add_compile_options`, which appends.

**D41 must use these tables, not the §5.1 estimate**, and must state the `/MP` finding — a
reader who sees "20%" without it will conclude the partition underdelivered, when the real
finding is that the build was never parallel to begin with.

### 5.2 The `.bat` design

All scripts ship **identically on both branches** — no divergence.

- **`build.bat` / `build_all.bat` become mode-preserving.** Today `build.bat` force-passes
  `-DCOSMIC_BUILD_ENGINE_ONLY=OFF` and reconfigures when the cache disagrees. Add the same
  pattern for the new flag but *read-only*: `findstr` `COSMIC_2D_ONLY:BOOL=ON` in
  `build\CMakeCache.txt` and echo `[MODE] 2D-only engine` or `[MODE] full 3D engine`. **Do not
  force the value.** The CMake cache is sticky, so a tree configured 2D stays 2D — which is
  exactly what makes plain `build.bat` in the `engine-2d` worktree the fast build.
- **New `build_2d.bat`** — explicit setter. Reconfigures with `-DCOSMIC_2D_ONLY=ON` when the
  cache says OFF or is absent, then builds. Mirrors `build.bat`'s existing structure.
- **New `build_3d.bat`** — the symmetric setter (`-DCOSMIC_2D_ONLY=OFF`), so switching a tree
  back is one command.
- **New `build_all_2d.bat`** — clean full build, 2D mode (mirrors `build_all.bat`).
- **`build_engine.bat`** gains the same mode echo.

All of them keep the trailing `pause` — that is the point of the `.bat` files, and it is exactly
why an AI session must never invoke them (§12 verification blocks use `cmake.exe` directly).

---

## 6. Pluggable physics backend

**Requirement:** *"I must be able to write my own physics later on if I would prefer (for a
single app or something)."*

### 6.1 Shape — dispatcher, not abstract base

`PhysicsWorld` (`physics/PhysicsWorld.h:44`) is today a concrete, Jolt-free, pimpl'd class
(`struct Impl` fwd-declared at **:137**, `std::unique_ptr<Impl> m_Impl` at **:140**, definition at
`PhysicsWorld.cpp:169`). It is held **by value** at `PlayerLayer.h:75` and `StarforgeApp.h:247`,
and passed by reference through `Scene::OnPhysicsStart(PhysicsWorld&)` (`Scene.h:103`).

Making `PhysicsWorld` abstract would force every one of those to become
`unique_ptr<IPhysicsWorld>` + factory. Instead **`PhysicsWorld` stays exactly what it is and
becomes a dispatcher** over a backend interface — the `RenderCommand` → `RendererAPI` idiom the
codebase already uses. Its public API does not change by one character, so **no call site moves
and no gameplay script changes.**

### 6.2 The pieces

**NEW `Cosmic/src/physics/PhysicsBackend.h`**

```cpp
namespace Cosmic
{
    // Mirrors PhysicsWorld's public surface 1:1 — lifecycle, bodies, velocity/force,
    // queries, characters, events, stats, debug draw. All parameters are PhysicsTypes.h
    // vocabulary (glm + PODs); no Jolt, no GL, no entt.
    class COSMIC_API IPhysicsBackend
    {
    public:
        virtual ~IPhysicsBackend() = default;
        virtual const char* Name() const = 0;
        virtual void Init(const PhysicsSettings& settings) = 0;
        virtual void Shutdown() = 0;
        virtual bool IsInitialized() const = 0;
        virtual void Step(float fixedDt) = 0;
        // … CreateBody/DestroyBody/Set|GetBodyTransform/MoveKinematic/velocities/forces/
        //   IsActive/Activate/RayCast/SphereCast/OverlapSphere/OverlapBox/
        //   CreateCharacter/DestroyCharacter/UpdateCharacter/GetCharacterTransform/
        //   SetCharacterPosition/IsCharacterGrounded/GetCharacterGroundNormal/
        //   GetCharacterVelocity/DrainContactEvents/GetStatistics/DebugDraw
    };

    class COSMIC_API PhysicsBackendRegistry
    {
    public:
        using Factory = std::function<std::unique_ptr<IPhysicsBackend>()>;
        static void Register(std::string name, Factory factory);
        static bool Has(const std::string& name);
        static std::vector<std::string> Names();
        static void SetDefault(const std::string& name);   // the app-level override
        static const std::string& Default();
        static std::unique_ptr<IPhysicsBackend> Create(const std::string& name);
    };

    // Explicit, NOT static-init: called from PhysicsWorld::Init. Registers "jolt"
    // (when COSMIC_WITH_JOLT) and always "null".
    COSMIC_API void RegisterBuiltinPhysicsBackends();
}
```

The registry map is a **function-local static** (Meyers singleton) inside `PhysicsBackend.cpp`,
so there is no static-initialization-order question across the DLL boundary. Built-in
registration is an explicit call, not a file-scope registrar object, for the same reason.

**`PhysicsTypes.h`** — `PhysicsSettings` (**:148–158**) gains `std::string Backend;` (empty ⇒
`PhysicsBackendRegistry::Default()` ⇒ `"jolt"`). Everything else in that header is already
backend-neutral and unchanged.

**`PhysicsWorld.{h,cpp}`** — the header's public block (**:53–131**) is untouched. Only
**:137/:140** change: `struct Impl` disappears, `m_Impl` becomes
`std::unique_ptr<IPhysicsBackend> m_Backend`. Every method body in the `.cpp` becomes a one-line
forward. `Init` calls `RegisterBuiltinPhysicsBackends()` then
`PhysicsBackendRegistry::Create(settings.Backend.empty() ? Default() : settings.Backend)`, logs
and falls back to `"null"` if the name is unknown.

**NEW `Cosmic/src/physics/backends/JoltBackend.cpp`** — the entire current `PhysicsWorld.cpp`
Jolt body moves here: the `<Jolt/…>` includes (**:10–43**), `EnsureJoltGlobalInit()`
(**:92–104**), the contact listener / query filters, and `struct Impl` (**:169**) which becomes
`class JoltBackend final : public IPhysicsBackend`. This is a mechanical move — the member
functions already have exactly the right signatures.

**NEW `Cosmic/src/physics/backends/NullBackend.cpp`** — a no-op backend that creates no bodies,
steps nothing, and returns empty queries. Always registered. It makes `COSMIC_WITH_JOLT=OFF` a
valid configuration and doubles as the minimal reference implementation.

**`ScriptableEntity.h`** — `PhysicsProxy::World()` (**:121–125**) still returns `PhysicsWorld*`.
Nothing in `Projects/ForgeIsle/src/scripts/PlayerController.h` (**:120–123**),
`assets/templates/src/scripts/WalkController.h` (**:42–45**), `PhysicsBall.h` (**:20**) or any
test changes.

### 6.3 Writing your own backend (the deliverable capability)

```cpp
// In your project's layer OnAttach — before any Play session starts:
Cosmic::PhysicsBackendRegistry::Register("my2d", []{ return std::make_unique<My2DPhysics>(); });
Cosmic::PhysicsBackendRegistry::SetDefault("my2d");
```

That is the whole integration. `ScenePhysics` keeps translating components → `BodyDesc` →
`CreateBody`, so authored scenes, the editor inspector, serialization and scripts are all
unchanged. A backend that only cares about XY simply ignores Z.

Contracts a backend must honour, all documented in W10's `docs/systems/physics-backends.md`:
the fixed-step rule (`Step` called exactly once per accumulated fixed-dt, after scripts'
`OnFixedUpdate`, before write-back and event dispatch — `ScenePhysics.h:13–16`);
`DrainContactEvents` moves and clears; `PhysicsSettings::ThreadCount == 0` means
single-threaded/deterministic (honour it or document that you ignore it); `RayHit::EntityId`
round-trips the owning entity UUID.

### 6.4 3D fences inside the physics layer

| Site | What | Action |
|---|---|---|
| `ScenePhysics.cpp:44–57` | `PrimitiveMeshData` (uses `Mesh::BuildBox/Plane/Cylinder/Cone/Sphere/Torus`) | fence |
| `ScenePhysics.cpp:139–175` | `BuildColliderDesc` mesh-collider branch (`MeshRendererComponent`) | fence |
| `ScenePhysics.cpp:176–213` | `BuildColliderDesc` terrain heightfield branch | fence |
| `ScenePhysics.cpp:263–264` | `TerrainColliderComponent` in the `any_of` collider probe | fence that name only |
| `ScenePhysics.cpp:283` | `BuildVoxelBodies()` call in `BuildBodies` | fence |
| `ScenePhysics.cpp:288–373` | `MakeVoxelChunkBody` / `BuildVoxelBodies` / `RebuildDirtyVoxelChunks` | fence |
| `ScenePhysics.cpp:381` | `RebuildDirtyVoxelChunks()` call in `Step` | fence |
| `ScenePhysics.cpp:502–505` | voxel body teardown | fence |
| `ScenePhysics.cpp:10–15` | includes `graphics/Mesh.h`, `terrain/Terrain.h`, `voxel/*` ×4 | fence 11–15; `Mesh.h` follows the mesh-collider fence |
| `ScenePhysics.h:27` | `#include "voxel/VoxelVolume.h"` — present **only** for `IVec3Hash`/`IVec3Eq` | fence, together with the `ChunkBodyMap` typedef and `m_VoxelBodies` at **:94–95** |
| `PhysicsWorld.cpp:8` | `#include "renderer/Renderer3D.h"` — used only by `DebugDraw` (**:947–984**, `JPH_DEBUG_RENDERER`) | moves into `JoltBackend.cpp` and is fenced there |

2D keeps `RigidBodyComponent` + Box/Sphere/Capsule colliders + `CharacterControllerComponent` —
the dimension-agnostic subset. `MeshCollider` and `TerrainCollider` are 3D-only.

**2D collider overlay (new, small):** with `Renderer3D` gone from the 2D build, `DebugDraw` is a
no-op there. Add a Renderer2D collider overlay in `ViewportController` (Box/Sphere/Capsule
projected onto XY, drawn with `Renderer2D::DrawRect`/`DrawLine`) so 2D physics is visually
debuggable. Sits alongside the existing 2D pixel grid.

### 6.5 Precedent

This is not a novel abstraction for this codebase. `RendererAPI`/`RenderCommand`
(`renderer/RendererAPI.h:77`, `RenderCommand.h:59`) is the same dispatcher-over-interface shape;
`ITelemetrySink` (`ScriptableEntity.h:62–69`) is the canonical "let an app plug in its own
implementation, reached through a `ScriptableEntity` proxy" pattern — the exact mechanism
`Physics()` uses; and `Projects/ViperSim/src/sim/IDynamics.h:39–56` is an in-tree, working proof
of a swappable dynamics interface at app level. `docs/design/modularity-audit.md` **§G3** filed
precisely this registry-keyed-factory gap ("replaceable, not coexistable") — W10 closes it.

---

## 7. Partition surgery — the verified anchor map

### 7.1 `scene/Components.h` (1309 lines, 34 components) → + NEW `Components3D.h`

**Stays in `Components.h` (19):**

| Group | Components (line ranges) |
|---|---|
| Neutral (10) | `IDComponent` 34–41, `OpaqueComponentsComponent` 50–56, `RelationshipComponent` 67–75, `TagComponent` 80–93, `TransformComponent` 98–139, `CameraComponent` 661–684, `EnvironmentComponent` 696–762, `NativeScriptComponent` 1168–1181, `SystemScriptComponent` 1192–1203, `PrefabComponent` 1211–1218 |
| 2D (4) | `SpriteRendererComponent` 145–204, `SpriteAnimationComponent` 215–255, `TilemapComponent` 273–350, `Light2DComponent` 361–371 |
| Physics-viable (5) | `RigidBodyComponent` 983–1003, `BoxColliderComponent` 1008–1017, `SphereColliderComponent` 1020–1029, `CapsuleColliderComponent` 1033–1043, `CharacterControllerComponent` 1070–1080 |

`EnvironmentComponent` is deliberately shared — its `Ambient2D` field drives 2D lighting.

**Moves to NEW `Components3D.h` (15):** `MeshRendererComponent` 382–421,
`PrimitiveMeshComponent` 434–456, `LODGroupComponent` 473–501 (nested `Level` 475–479),
`AnimatorComponent` 517–587, `SocketComponent` 608–617, `DirectionalLightComponent` 624–633,
`PointLightComponent` 639–652, `TerrainComponent` 790–823, `WaterComponent` 833–857,
`ParticleEmitterComponent` 868–912, `VoxelVolumeComponent` 929–965 (+ the `VoxelRenderData` fwd
decl at **770**), `MeshColliderComponent` 1049–1057, `TerrainColliderComponent` 1061–1065,
`NavMeshComponent` 1099–1131, `NavAgentComponent` 1145–1156.

**Includes:** `Components.h` drops `graphics/Skeleton.h` (**8**), `graphics/AnimationClip.h`
(**9**), `particles/ParticleSystem.h` (**10**) — they move to `Components3D.h`. It **keeps**
`physics/PhysicsTypes.h` (**11**).

**Registration:** the `CS_REGISTER_COMPONENT` block at **1241–1274** (34 entries, global scope)
splits along the same line — 19 stay, 15 move into `Components3D.h`. ODR holds because every TU
naming a 3D component includes `Components3D.h`.

**`entt::type_hash` specializations at 1284–1308** (Tag, Transform, SpriteRenderer) are
2D/neutral only — they **stay** in `Components.h`. There are no 3D `type_hash` specializations.

`Components3D.h` begins with `#include "scene/Components.h"`. Consumers that must add the
include: `Scene3D.cpp`, `SceneRenderer.cpp`, `ScenePicker.cpp`, `SceneNav.cpp`,
`ScenePhysics.cpp`, `TypeRegistry3D.cpp`, the 3D Starforge TUs, and `Cosmic.h` (inside a fence,
so `Frontier`/`Engine3DDemo`/`ForgeIsle` get it transitively in 3D builds).

### 7.2 `reflect/TypeRegistry.cpp` → + NEW `TypeRegistry3D.cpp`

`TypeRegistry3D.cpp` exports `RegisterEngine3DTypes()`, called from `RegisterEngineTypes` inside
a fence. Physics registrations at **:308–360** stay put **except** `MeshCollider` (**:345–348**)
and `TerrainCollider` (**:353**). `/bigobj` is already on the target (`Cosmic/CMakeLists.txt`
**L151–153**) — the split reduces pressure on it rather than adding.

### 7.3 `scene/Scene.cpp` (1560 lines) → + NEW `Scene3D.cpp`

**Moves to `Scene3D.cpp` (verbatim):**

| Function | Lines |
|---|---|
| `PrimitiveSignature` (anon ns) | 48–61 |
| `BuildPrimitiveMesh` (anon ns) | 65–80 |
| **`GatherSceneLights` (anon ns)** | **86–117** |
| `OnNavStart` / `OnNavStep` / `OnNavStop` | 158–168 / 170–174 / 176–183 |
| `SyncPrimitiveMeshes` | 185–238 |
| `SyncWorldSystems` | 240–295 |
| `SyncVoxelVolumes` | 297–419 |
| `SyncNavMeshes` | 421–431 |
| `OnRenderWorldFX` | 433–488 |
| `OnRender3D` | 1162–1214 |
| `FindAnimatorFor` | 1219–1233 (doc comment 1216–1218) |
| `UpdateAnimators` | 1235–1363 |
| `SubmitOpaqueMeshes` | 1366–1479 |
| `BuildRenderDesc` | 1490–1560 (doc comment 1488–1489) |

**Stays in `Scene.cpp`:** ctor/dtor 120–125, entity CRUD + hierarchy 490–742, `OnUpdate` 744–779
(**fence the `UpdateAnimators(deltaTime)` call at line 749**), `OnFixedUpdate` 781–807, **the
four physics methods 130–155**, legacy `OnRender` 809–919, `UpdateSpriteAnimations` 921–947,
`OnRenderSprites` 949–1130, `OnRender2DLights` 1132–1160, **`FindEnvironment` 1481–1486**,
`WorldOf` 595–649.

**`Scene.h` fences — nav only:** the nav block **121–140** (`OnNavStart` 129, `OnNavStep` 133,
`OnNavStop` 136, `GetNav()` 140), `m_NavRuntime` at **402**, and the moved 3D declarations
(`SubmitOpaqueMeshes` **390**, `FindAnimatorFor` **394**). The physics block 95–119 and
`m_Physics` at 401 are **not** fenced (§4.1). The `struct SceneRenderDesc;` fwd decl at **22**
stays.

### 7.4 `scene/SceneSerializer.cpp` (631 lines)

Exactly **two** direct 3D casts exist in the whole file — `:188`
(`static_cast<MeshRendererComponent*>`) and `:270` (`const` variant), both for material slots.
Fence both. The neighbouring string compares at `:185` and `:268` are not type dependencies.
A grep for all 15 3D component type names returns no other hits.

**The cross-build safety net:** `OpaqueComponentsComponent` captures unknown component blocks
verbatim on load at **:120–126** (when `registry.FindByName(compName)` returns null, the raw
JSON is stashed via `get_or_emplace<OpaqueComponentsComponent>(…).Blocks.emplace_back(name,
compJson.dump())`) and re-emits them on save at **:285–293**. A 2D build can therefore open a 3D
scene and re-save it with every 3D component preserved byte-for-byte. `test_crossbuild_scene`
(W9) proves it.

### 7.5 `scripting/ScriptableEntity.h` (498 lines)

| Line | Item | Action |
|---|---|---|
| 30 | `scene/Components.h` | keep |
| 31 | `scene/FlowMachine.h` | keep |
| **33** | `physics/ScenePhysics.h` | **keep — not fenced** |
| 34–35 | `voxel/VoxelVolume.h`, `voxel/BlockPalette.h` | fence |
| 36–37 | `scene/SceneNav.h`, `nav/NavWorld.h` | fence |
| 110 | `Telemetry()` | keep |
| 116–171 / 172 | `PhysicsProxy` / `Physics()` | keep |
| 175–190 / 191 | `CharacterProxy` / `Character()` | keep |
| 242 | `Flow()` | keep |
| 297 | `Voxels()` (+ its proxy) | fence |
| 327 | `Animator()` (+ its proxy) | fence |
| 362 | `Nav()` (+ its proxy) | fence |
| 380–383 | `OnCollisionEnter/Exit`, `OnTriggerEnter/Exit` virtuals | keep |

Template fallout is exactly one file: `NavCritter.h` (the only template script using `Nav()`) —
fence its include and its cases in `test_template_scripts.cpp`. `PaddleController`, `PongBall`,
`StoryUiBinding`, `BouncingBall`, `WalkController`, `PhysicsBall` are all 2D-build-clean.

### 7.6 `renderer/SceneRenderer.{h,cpp}` — the spine

2D keeps flowing through `SceneRenderer` in **both** builds: HDR target → sprites via
`DrawTransparent` → Light2D multiply → PostProcessStack tonemap/FXAA/bloom/vignette →
`DrawOverlay2D` for UI. A separate 2D compositor would duplicate the pass contract and invite
pixel drift; the frame-lifecycle spec (`docs/design/frame-lifecycle.md` §5 step 7) already puts
2D/UI after post, and that contract is preserved unchanged.

**`SceneRenderer.h` — `SceneRenderDesc` is 193–232:**

| Line | Member | 2D |
|---|---|---|
| 195 | `View`, `Projection`, `CameraPosition` | keep |
| 196 | `SetCamera(const Camera&)` | keep |
| 198 | `Renderer3D::SceneLightsDesc Lights` | **fence** |
| 199 | `TimeSeconds`, `Exposure` | keep |
| 200 | `SceneRendererSettings Settings` | keep |
| 202–207 | `TerrainSystem`, `WaterBodies`, `PrimaryReflectionWater`, `Emitters`, `Ribbons`, `DistortionEmitters` | **fence** |
| 208 | `Scene* EcsScene` | keep |
| 213 | `const SkyDetailDesc* DetailedSky` | **fence** |
| 219–222 | `Coverage`, `CoverageAccumPerSec`, `CoverageMeltPerSec`, `DeltaTime` | **fence** (keep `DeltaTime` if 2D needs it — check at implementation) |
| 227 | `SelectedEntities` | **fence** (picker is 3D-only) |
| 229 | `DrawOpaque` | **fence** |
| 230 | `DrawTransparent` | keep |
| 231 | `DrawOverlay2D` | keep |

Also fence in the header: the `Renderer3D.h` / `EnvironmentMap.h` / `ShadowMap.h` includes, the
`SceneDrawContext` (**:89**) submit verbs, the `m_Environment` / `m_Shadow` / outline members,
and the private decls `PassShadow` **290**, `PassCoverage` **291**, `PassReflection` **292**,
`PassOutline` **296**. Keep `Render` **257**, `RenderToTexture` **271**, `ApplyEnvironment`
**286**, `PassOpaqueHDR` **293**, `PassTransparents` **294**, `PassPostAndComposite` **295**,
`SceneRendererSettings` **136–187**.

**`SceneRenderer.cpp` (761 lines) — definition ranges:**

| Function | Lines | 2D |
|---|---|---|
| anon-ns helpers | 29–174 | audit; fence the 3D-only ones |
| `SceneRenderDesc::SetCamera` | 169–174 | keep |
| `~SceneRenderer` | 180–186 | keep |
| `Init` | 188–201 | keep; fence env/shadow lines |
| `Shutdown` | 203–220 | keep; fence env/shadow lines |
| `SetViewportSize` | 222–230 | keep |
| `ApplyEnvironment` | 235–295 | keep; fence `desc.Lights` writes + owned-EnvironmentMap block |
| `Render` | 301–351 | keep; fence steps 2–3 + shadow/coverage/reflection calls |
| `RenderToTexture` | 354–376 | keep |
| `PassShadow` | 379–438 | **fence whole** |
| `PassCoverage` | 441–495 | **fence whole** |
| `PassReflection` | 498–544 | **fence whole** |
| `PassOpaqueHDR` | 547–595 | keep; fence 3D interior |
| `PassTransparents` | 598–643 | keep; fence 3D interior (water/particles) — sprites flow through here |
| `PassPostAndComposite` | 646–717 | keep; fence sky-resource sampling only. Tonemap/FXAA/bloom/vignette stay |
| `PassOutline` | 720–760 | **fence whole** |

There is no out-of-line `SceneRenderer::SceneRenderer` — the constructor is implicit.

**Zero 2D visual change is expected:** sprites have no picker or outline path today, so removing
`PassOutline` from the 2D build changes nothing for 2D content. The golden A/B in W2 pins it.

### 7.7 `Cosmic.h` — the umbrella (the SF_Telem fix)

SF_Telem's *entire* 3D coupling is `#include <Cosmic.h>` in 10 headers. Fencing the umbrella is
the whole fix — **zero SF_Telem source changes are needed for the split.**

**Fence:** 35 (`Renderer3D.h`), 40 (`EnvironmentMap.h`), 41 (`ShadowMap.h`), 43
(`InstanceSet.h`), 44 (`CoverageCapture.h`), 45 (`terrain/Terrain.h`), 46 (`water/Water.h`), 47
(`water/Presets.h`), 48 (`particles/ParticleSystem.h`), 49 (`particles/Presets.h`), 62
(`graphics/Model.h`), 68 (`assets/MeshImport.h`), 97 (`scene/ScenePicker.h`), 106
(`scene/WorldSystemRecipes.h`). **Add** `#include "scene/Components3D.h"` inside the same fence,
next to line 94.

**Keep unfenced:** 33 `Renderer.h`, 34 `Renderer2D.h`, 36–39 (`RenderCommand`, `RenderPass`,
`BindingPoints`, `PostProcessStack`), **42 `SceneRenderer.h`**, 50–61 + 63–64 (graphics incl.
`Mesh.h`, `Material*`, `Font.h`, `Gizmo.h`), 67 `AssetLibrary.h`, 71–78 (cameras), 81–89 (math),
92–96 (scene core incl. `Components.h`), 102–105, **116–122 (physics)**, 125–127 (input),
130–131 (audio), 134–143 (serial/utils), 146–152 (telemetry), 155–156 (layers), 159–164 (jobs),
168–179 (ImGui + ui).

Note there is **no nav or voxel include in `Cosmic.h`** — do not go looking for one.

---

## 8. CMake and Starforge mechanics

### 8.1 `Cosmic/CMakeLists.txt` (205 lines)

| Line | Change |
|---|---|
| near top | `option(COSMIC_2D_ONLY "Build the 2D-only engine (excludes all 3D subsystems)" OFF)` and `option(COSMIC_WITH_JOLT "Build the Jolt physics backend" ON)` |
| **76** | `add_subdirectory(dependencies/JoltPhysics)` → wrap in `if(COSMIC_WITH_JOLT)` |
| **83** | `add_subdirectory(dependencies/recastnavigation)` → wrap in `if(NOT COSMIC_2D_ONLY)` |
| **92** | `option(COSMIC_WITH_ASSIMP … ON)` block condition → `if(COSMIC_WITH_ASSIMP AND NOT COSMIC_2D_ONLY)` around **92–127** |
| **132–138** | after the GLOB, add `list(FILTER COSMIC_SOURCES EXCLUDE REGEX …)` for the §4 table when `COSMIC_2D_ONLY` |
| **146** | add `target_compile_definitions(Cosmic PUBLIC $<$<BOOL:${COSMIC_2D_ONLY}>:COSMIC_2D_ONLY>)` — **PUBLIC**, so Starforge/projects/tests see it |
| **168** | `PRIVATE … Jolt RecastNavigation` → generator expressions: `$<$<BOOL:${COSMIC_WITH_JOLT}>:Jolt>` and `$<$<NOT:$<BOOL:${COSMIC_2D_ONLY}>>:RecastNavigation>` |
| **172–175** | assimp link block already gated by the same condition as 92 |

The GLOB at 132–138 has **no `CONFIGURE_DEPENDS`** — adding or removing engine source files
requires an explicit reconfigure. This bites in W3/W4/W5 which all add new `.cpp` files.

### 8.2 Root `CMakeLists.txt` (93 lines)

Declare and forward both options. The project scanner is **L67–93**: `file(GLOB
PROJECT_SUBDIRS …)` at **L69**, `foreach` at **L71**, and the gate at **L74**
(`if(IS_DIRECTORY … AND EXISTS …/CMakeLists.txt)`). Add a `COSMIC_SKIP_PROJECTS` cache list
above L69 and extend L74 with `AND NOT "${SUBDIR}" IN_LIST COSMIC_SKIP_PROJECTS`. Under
`COSMIC_2D_ONLY`, default it to `Frontier;Engine3DDemo;ForgeIsle`.

Note the install rule at **L83–90** relies on target-name == directory-name; skipping a project
must also skip its install entry.

### 8.3 NEW `CMakePresets.json`

Committed identically on both branches. Presets `default` (3D) and `2d` (`COSMIC_2D_ONLY=ON`),
both `binaryDir: "${sourceDir}/build"`, `architecture: x64`. **Verify the generator string
against the installed Visual Studio** before committing (`cmake --help` lists available
generators; or read `CMAKE_GENERATOR` out of the existing `build/CMakeCache.txt`).

### 8.4 The build-output collision gotcha

`COSMIC_SDK_DIR` defaults to the **source** directory (root `CMakeLists.txt` **L34**), and every
target's `RUNTIME_OUTPUT_DIRECTORY` is `${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>`. Two binary
directories in one source tree (`build/` and `build-2d/`) would therefore **both write to
`C:/dev/Cosmic/build/Runtime/Debug`** and clobber each other's `Cosmic.dll`.

This is why the 2D configuration gets its own **git worktree** (`../Cosmic-2D`) rather than a
second build folder. Do not try to solve it with two binary dirs.

### 8.5 Starforge

`Projects/Starforge/CMakeLists.txt` — `list(FILTER)` out `panels/VoxelPanel.*`,
`panels/WorldSystemsPanel.*`, `editors/AnimationEditor.*` after the GLOB (**L42**).

Fence inventory:

- **`StarforgeApp.h`** — `m_WorldSystems`, `m_Voxel`, `m_NavBakes` (**251**), the AnimationEditor
  registration. `m_Mode2D` defaults **`true`** under the flag. **`m_Physics` (247) stays.**
- **`StarforgeApp.cpp`** — nav in the play session (physics stays: **660–661**, **696**, **698**,
  **756**, **763**, **786–788**, **799–801** are all unfenced); voxel + world-systems menus and
  panel draws; 3D entity-creation menus; nav bake ticking; the 3D sample scaffolds (ForgeBlocks
  etc. — **ForgePong and FlowDemo remain**); the 2D toggle pinned on. The Project Settings
  "Physics defaults" tab (**4447–4452**) stays.
- **`ViewportController.{h,cpp}`** — `#include "physics/ScenePhysics.h"` (**:8**) stays; the
  collider wireframe gizmos (**745–783**) stay for 3D and gain the 2D Renderer2D variant;
  `GetPhysics()->World().DebugDraw()` (**787–788**) fences to 3D; 3D grid / nav overlays out;
  2D pixel grid + gizmo stay.
- **`panels/InspectorPanel.cpp`** — the 3D sections; "Fit to mesh" for Box/Sphere colliders
  (**465–494**) is 3D-only (it reads mesh bounds).
- **`AssetTypes.cpp`**, **`PreviewRig.cpp`**, **`commands/EditorCommands.cpp`**,
  **`ProfilerPanel`/`SystemPanel`** Renderer3D stat rows.
- **Verified 3D-free, untouched:** `EnvironmentPanel` (so `Ambient2D` stays editable) and
  `HierarchyPanel`.

---

## 9. Test plan — both engines

Three tiers: **shared** (both configs), **2D-only**, **3D-only**.

### 9.1 Gating in `tests/CMakeLists.txt`

The hand-list at **L7–65** splits. Wrap in `if(NOT COSMIC_2D_ONLY)`: `test_physics_terrain`,
`test_voxel_collision`, `test_voxel`, `test_nav_world`, `test_nav_bake`, `test_nav_agents`,
`test_worldsystems`, `test_phase10_world`, `test_meshimport`, `test_animation`, `test_crossfade`,
`test_sockets`, `test_particle_noise`, `test_material_slots`, `test_render_queue`,
`test_primitives`, `test_presets`, `test_frustum`, `test_flycamera`.

**Stay in the 2D suite:** `test_physics_world`, `test_physics_scene`, `test_physics_events`,
`test_physics_character`, `test_physics_determinism` — five of six, thanks to §4.1. One in-file
fence needed: `test_physics_scene.cpp:35` round-trips `TerrainColliderComponent`.

In-file fences also in: `test_template_scripts` (NavCritter cases), `test_scene_components`,
`test_components`, `test_reflect`, `test_scene_serializer`, `test_assetlibrary`,
`test_forgeisle_content` (already self-skipping when `Projects/ForgeIsle` is absent, but the
build gate is cleaner), `test_spatial`.

Estimated 2D suite **≈270–290 cases**; the 3D build keeps all 355 plus everything new.
`test_s5_navigation` is *editor-camera* navigation, not pathfinding — it stays in both.

### 9.2 New 2D headless invariants

| File | Covers |
|---|---|
| `test_sprite_order.cpp` | Via a **pure code-motion** extraction of the painter list from `Scene::OnRenderSprites` into a public `Scene::BuildSpriteDrawList()`. The sort machinery is `Scene.cpp:959–988` (comment 959–962, `struct SpriteItem` 963, gather loops 965–982, `std::sort` 983–988) — **not 952**; lines 952–958 are the registry views plus the "no 2D content ⇒ no GL calls" compat gate. Asserts: ZOrder precedence, YSort key `-Position.y`, tilemap interleave, disabled/inactive-ancestor exclusion, entity-id tie-break determinism. |
| `test_light2d.cpp` | CPU twin of `Light2D.glsl:40` falloff `pow(clamp(1-d,0,1), u_Falloff)`, following the `test_particle_noise.cpp` twin pattern: boundary zeros, monotonic decay, additive accumulation, and the **ambient-multiply identity** — the invariant stated in prose at `Scene.cpp:1154–1155` and guarded at **1156–1157** (`if (lights.empty() && ambient == vec3(1.0f)) return;`). Plus a headless `OnRender2DLights` no-crash. |
| `test_physics_2d.cpp` | Enabled by keeping Jolt: RigidBody + BoxCollider constrained to XY — gravity along −Y, **no Z drift over 600 steps**, 2D trigger enter/exit, bit-identical determinism across two runs (`ThreadCount = 0`). |
| `test_sprite_animation.cpp` | Flipbook advance, loop and ping-pong wrap, negative speed, zero-frame guard, frame-time accumulation across variable dt. |
| `test_tilemap_extra.cpp` | `TilemapComponent::InBounds` (**Components.h:302–316**) edges, resize preserves cells, cull window at camera edges, serializer round-trip of a large int-array tilemap. |
| `test_ui_anchor.cpp` | `UiSystem::ProjectToCanvas` (pure — declared `scene/ui/UiSystem.h:83`, defined `UiSystem.cpp:42`) at frustum edges and behind-camera; `UiRect::Contains` (`scene/ui/UiComponents.h:38`, `Contains` at **:48**) under nested anchors. |
| `test_timeline_state.cpp` | `Projects/Starforge/src/widgets/Timeline.h` driven headless — include mechanism as `test_template_scripts.cpp:11-14`. Advance loop-wrap/clamp, negative Speed, Scrub clamping, `Duration<=0`. |
| `test_scene2d_determinism.cpp` | Two runs of a scripted 2D scene (sprites + flow + 2D physics) produce bit-identical transforms. |

### 9.3 New 3D headless invariants — the partition's own net

| File | Covers | Guards |
|---|---|---|
| `test_render_desc.cpp` | `Scene::BuildRenderDesc` on a fixture scene (lights, terrain, water, emitters, coverage) captured **field by field** into a committed baseline and asserted stable. | **W5.** A `SceneRenderDesc` member silently dropped during the `Scene3D.cpp` code motion is otherwise invisible until someone notices missing water at runtime. |
| `test_components3d_registry.cpp` | All 34 components register exactly once; names and `entt::type_hash` values match a committed baseline; the split registration block has no duplicates and no omissions. | **W4.** The highest-severity failure mode of the `Components.h` split. |
| `test_scene3d_lifecycle.cpp` | `SyncWorldSystems` / `SyncVoxelVolumes` / `SyncNavMeshes` / `SyncPrimitiveMeshes` idempotence and teardown ordering after the file move. | **W5.** |
| `test_physics_backend.cpp` | A fake `IPhysicsBackend` registered from the test, selected via `PhysicsSettings::Backend`, driven through `PhysicsWorld` → `ScenePhysics` → script contact dispatch. | **W3**, and it is the reference example for §6.3. |

Existing 3D coverage is the regression net for the moves and must keep passing unchanged:
`test_render_queue` (cull/sort/auto-instancing), `test_frustum`, `test_primitives`,
`test_worldsystems`, `test_phase10_world`, `test_meshimport`, `test_animation`/`test_crossfade`/
`test_sockets`, `test_material_slots`, the three `test_nav_*`, `test_voxel*`, and the six
`test_physics_*` — including **`test_physics_determinism`'s bit-identical assertion**, which is
what guards the W3 backend refactor.

### 9.4 Shared

`test_crossbuild_scene.cpp` — the 2D build loads a 3D scene, saves, and byte-compares the 3D
component blocks (the `OpaqueComponentsComponent` proof, §7.4). The 3D build runs the mirror
direction. Plus `test_reflect`, `test_scene_serializer`, `test_components`,
`test_scene_components`, `test_hierarchy`, `test_commandstack`, `test_scripthost`,
`test_flowmachine`, `test_story`, `test_camera2d`, `test_ui_rects`, `test_scenemanager`,
`test_telemetry_roundtrip`, `test_framing`, `test_config`, `test_filesystem_mounts`,
`test_audio`, `test_events`, `test_branding` — all in both configs with in-file fences where
needed.

### 9.5 Golden-image harness — both engines

New target **`CosmicRenderTests`**, `option(COSMIC_BUILD_RENDER_TESTS … OFF)`, **local-only,
never in CI** (needs a real GPU). Runs under **both** configurations. Built entirely from engine
verbs, so `tests/check_gl_conformance.ps1` — which scans `tests/` and `Projects/` too — stays
green.

- `tests/render/CMakeLists.txt` — exe links `PRIVATE Cosmic`, `RUNTIME_OUTPUT_DIRECTORY` =
  `${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>` (shaders load CWD-relative).
- `tests/render/render_main.cpp` — doctest main; `Cosmic::Window(640,360,…)` + `Renderer::Init()`
  bootstrap (fence the `Renderer3D::Init/Shutdown` lines in `renderer/Renderer.cpp`);
  `--update-goldens` flag / `COSMIC_UPDATE_GOLDENS=1` env.
- `tests/render/GoldenImage.{h,cpp}` — 320×180 offscreen `FrameBuffer`; capture via
  `FrameBuffer::ReadPixels` (`graphics/FrameBuffer.h:187` — RGBA8, **row-major top-left origin**,
  HDR attachments converted and clamped, **the FBO must be bound**); compare against
  `tests/render/goldens/<name>.png` loaded with `ImageIO::ReadPixels` (`utils/ImageIO.h:32` —
  also **top-left origin**, it explicitly disables stb's flip-on-load). The two compose directly
  with no flip. Write with `ImageIO::WritePNG` (`utils/ImageIO.h:24`). Tolerance: per-channel ≤2,
  differing-pixel budget ≤0.1%; emit `.actual.png` and `.diff.png` on mismatch. Byte-exact
  asserts are reserved for in-process A/B pairs.
- **2D goldens** (both configs): `sprites` (procedural textures; also pins the
  `BuildSpriteDrawList` refactor), `tilemap` (culling at camera edges), `light2d` (lights-on
  golden **plus** the byte-identical A/B — white-ambient-no-lights frame vs pass-skipped frame,
  `memcmp` equal), `ui` (solid-colour panels/buttons; no text in v1), `scene2d` (full 2D-mode
  frame through `SceneRenderer::RenderToTexture`, covering HDR → tonemap → FXAA).
- **3D goldens** (3D config only — these pin W6, the riskiest phase): `mesh_pbr` (lit sphere +
  directional light + shadow), `terrain`, `water` (with planar reflection), `sky_ibl`,
  `particles`, `postchain` (bloom/SSAO/god-rays/FXAA on vs off), `outline` (`ScenePicker`
  selection outline), `instancing` (a ≥4-run auto-instanced draw vs the same scene with
  instancing disabled — must match).
- `tests/render/goldens/` — committed PNGs, generated **once** in W2 on the 3D build, then
  required to match byte-for-byte after every subsequent phase.

### 9.6 Telemetry / serial crash campaign (shared; protects SF_Telem)

**Two confirmed real bugs in `Cosmic/src/telemetry/DataPlayer.cpp`** (`LoadBinaryFile` is
**87–183**; only a v1 path exists — `if (version == 1u)` at **111**, everything else errors at
**181**; **there is no `try`/`catch` anywhere in the file**):

1. **Unvalidated counts → `bad_alloc` → `std::terminate`.** `entityCount` (read **113/115**) is
   never bounds-checked before `std::vector<PlayerEntityData> entities(entityCount)` at **124**
   and `sampleCounts(entityCount, 0u)` at **125**. Same for `chCount` (**134**) used at **138**
   and **148**, and `sampleCounts[e]` (**136**) used at **146**. A corrupt `0xFFFFFFFF` requests
   ~400 GB. (Row indexing at **152–165** is in-bounds — this is an allocation bug, not an OOB.)
2. **Truncation reported as success.** Line **167** reads `if (!file.good() && !file.eof())`. A
   truncated file sets **both** `eofbit` and `failbit`, so `eof()` is true, the condition is
   false, and the function **returns `true` at 178** with silently zero-filled frames.

**Fix:** validate every count against remaining file size plus hard caps (entities ≤ 4096,
channels ≤ 1024), correct the truncation predicate, `CS_CORE_ERROR` + `return false` on
violation. Only the header check at **118–122** exists today; magic is checked at **98–103**.
`test_telemetry_roundtrip.cpp` is the regression net — valid files must load identically.

| File | Covers |
|---|---|
| `test_telemetry_robustness.cpp` | Empty / bad-magic / unknown-version files; truncation sweep over a real `DataRecorder`-produced file; absurd counts; seeded random-byte fuzz (~200 iterations) — Load returns fast and never throws. Plus `DataRecorder`: autosave rolling-folder overwrite, Flush during concurrent Record (4 threads × 10k), Clear keeps registrations, double-Flush idempotence, destructor-during-flush. |
| `test_serial_lifecycle.cpp` | `SerialPort`: bad port fails fast (`State::Failed`), Write-on-closed, idempotent Close, `GetAvailablePorts` no-crash, `BeginOpen` invalid → Failed under timeout, **the abandon race** (`BeginOpen` then immediate `Close` — the historical Bluetooth-freeze machinery), double-`BeginOpen` no-op, destroy-while-Connecting. `SerialLink`: unreachable Connect + `OnUpdate(dt)` loop bounded-time, auto-reconnect after `k_ReconnectInterval`, one-shot `ConsumeJustConnected`, `Shutdown` clears WantConnection. |
| `test_sftelem_protocol.cpp` | `Projects/SF_Telem/src/Telemetry.h` is header-only and engine-free (includes only `<string> <vector> <cstdint> <cstdio> <cmath>` at **38–42**), so it lifts straight into a test TU. `ParseFrame` (**249–285**): `$R/L/W,…*HH` decode incl. poles/gear/slip zero-guards; checksum mismatch, `#` heartbeat, unknown tag, truncated, missing `*`, non-numeric → returns false, no crash. `DriveSample`/`WeaponSample` `ToChannels`/`FromChannels` round-trip. Seeded mutation fuzz. |
| `test_sftelem_hub.cpp` | **Pure refactor first.** `TelemHub::PumpSerial` (`TelemHub.cpp:323–382`) splits at its natural seam: **327–330** is the only I/O (`ConsumeJustConnected` + `Poll`), **331–381** is pure `std::string` → state. Extract the latter as public `void TelemHub::IngestChunk(const std::string&)` and compile `TelemHub.cpp` into `CosmicTests`. Tests: partial-line reassembly across chunk boundaries, good/bad frame stats, per-ESC presence/staleness, the 4 KB accumulator purge (**381**), replay-mode ignores live bytes, Shutdown flushes a dirty recording. **Fallback** if headless `TelemHub` construction proves unsafe (it owns a `Ref<Texture2D>` pinout image at `TelemHub.h:177`): extract the accumulator + line-splitter into a header-only `FrameAssembler.h`, test that directly, and leave `TelemHub.cpp` out of the test target. |
| `test_audio.cpp` (extend) | After `AudioEngine::Init()`, `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)` must **not** return `RPC_E_CHANGED_MODE` — pins the `MA_COINIT_VALUE` STA fix in `audio/MiniaudioImpl.cpp:23` that unbroke every native file dialog. |

Deferred and documented, not built: an injectable byte-transport under `SerialLink` for full
loopback; com0com hardware-in-the-loop scripts.

---

## 10. Documentation work orders (D41–D45) — written in W10

Planned now, written only after the separation is verified working on-GPU.

**New documents**

| ID | Document | Contents |
|---|---|---|
| **D41** | `docs/systems/build-2d-3d-split.md` | What the flag excludes and why; the classification rule for new code ("which side does this file belong on?"); `build.bat` / `build_2d.bat` / presets / worktree layout; **the recorded build-time numbers from W2 and W8**; the branch and carry-over workflow. |
| **D42** | `docs/systems/physics-backends.md` | `IPhysicsBackend`, the registry, `PhysicsSettings::Backend`; the fixed-step contract a backend must honour; `ThreadCount`/determinism expectations; `RayHit::EntityId` round-trip; a worked example lifted from `test_physics_backend.cpp`. |
| **D43** | `docs/reference/physics.md` | The per-call reference for `PhysicsWorld` / `PhysicsTypes` / `PhysicsBody` / `CharacterController` / `ScenePhysics` — currently **missing entirely** from `docs/reference/`. Follows `docs/reference/README.md`'s entry format. |
| **D44** | This document, updated | Status banners, ✅-with-date lines, and a deviation section recording every place implementation diverged from the plan. |

**Updated documents (D45)**

- `docs/plans/00-MASTER-ROADMAP.md` — Phase 29 entry, doc-index row (doc 28), and a
  working-agreement line: *every change must leave both configurations green*.
- `docs/plans/FEATURE-MATRIX.md` — new rows: pure-2D engine build ✅, pluggable physics backend
  ✅, 2D collider debug overlay ✅; a ⏸ row for 2D-native particles.
- `docs/plans/12-documentation-plan.md` — register D41–D45 alongside D5–D40.
- `docs/design/modularity-audit.md` — **close G3** ("Concrete world-system factories:
  replaceable, not coexistable") with the physics backend registry as the first real instance,
  and add a physics row to the §4 swap cookbook.
- `docs/systems/build-plugin-packaging.md` — its D34 section plan gains the two-configuration story.
- `docs/systems/ecs-scene.md`, `docs/systems/rendering-2d.md`, `docs/systems/rendering-3d.md`,
  `docs/systems/README.md`, `docs/reference/README.md` — index and pointer updates for the split
  files (`Components3D.h`, `Scene3D.cpp`, `TypeRegistry3D.cpp`).
- Root `README.md` — §1.5 command reference gains `build_2d.bat` / `build_3d.bat` /
  `build_all_2d.bat`; a short section on the two configurations and which branch is which.

---

## 11. Phase table

| WO | Phase | Content | Size |
|---|---|---|---|
| **W0** | 0 | Git prep — fast-forward `main`, create `feature/engine-split` | S |
| **W1** | 1 | Build flags, presets, skip-list, dependency conditionals, `.bat` set | S |
| **W2** | 2 | **Safety net first** — render harness, 2D **and** 3D goldens, `BuildSpriteDrawList`, the 2D invariant suites, the 3D baseline suites, **3D build-time baseline** | XL |
| **W3** | 3 | Pluggable physics backend | M–L |
| **W4** | 4 | Component partition + reflection split + serializer + umbrella fences | L |
| **W5** | 5 | `Scene.cpp` → `Scene3D.cpp` + the remaining engine fences | L |
| **W6** | 6 | `SceneRenderer` partition — **the 2D config compiles for the first time** | L |
| **W7** | 7 | Starforge gating — the 2D editor boots | M–L |
| **W8** | 8 | Cut `engine-2d`, worktree, **record the build-time numbers** | S–M |
| **W9** | 9 | Telemetry/serial/SF_Telem hardening + fuzz campaign + cross-build test | L |
| **W10** | 10 | Documentation (D41–D45) | M–L |

Every phase leaves **both** flag states green and ends with one commit.

---

## 12. Per-phase work orders

Each section below is self-contained. The **📋 PROMPT** block is what you paste into a fresh
session; the **▶️ YOUR COMMANDS** block is what you run.

### Standing rules every prompt inherits

- Read `docs/plans/28-phase29-engine-split-plan.md` §0 and the named work order before editing.
- **Re-verify every anchor by content, not line number.**
- The 3D build must stay behaviour- and pixel-identical. From W6 on, the 2D build must also be green.
- Never invoke `build.bat` / `build_all.bat` / `build_engine.bat` — they end in `pause` and hang.
  Use the `cmake.exe` recipe in each phase's verification block.
- Don't pipe native-exe stderr through `2>&1` in PowerShell 5.1 — it wraps errors and flips the
  exit code. Use `Tee-Object` + `$LASTEXITCODE`.
- Commit locally at the end of the phase, authored by the repo's git identity, **with no
  `Co-Authored-By: Claude` trailer**. **Never `git push`** — tell the user when to.
- Reconfigure (not just rebuild) whenever source files are added or removed — the engine GLOB has
  no `CONFIGURE_DEPENDS`.

### The verification recipe (referenced by every phase)

```powershell
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

# 3D configuration
& $cmake -S C:\dev\Cosmic -B C:\dev\Cosmic\build -A x64 -DCOSMIC_BUILD_TESTS=ON
& $cmake --build C:\dev\Cosmic\build --config Debug   --parallel
& $cmake --build C:\dev\Cosmic\build --config Release --parallel
& C:\dev\Cosmic\build\Runtime\Debug\CosmicTests.exe --reporters=console --no-intro
powershell -ExecutionPolicy Bypass -File C:\dev\Cosmic\tests\check_gl_conformance.ps1

# 2D configuration (from W6 on; run inside the ../Cosmic-2D worktree once it exists)
& $cmake -S C:\dev\Cosmic-2D -B C:\dev\Cosmic-2D\build -A x64 -DCOSMIC_BUILD_TESTS=ON -DCOSMIC_2D_ONLY=ON
& $cmake --build C:\dev\Cosmic-2D\build --config Debug --parallel
& C:\dev\Cosmic-2D\build\Runtime\Debug\CosmicTests.exe --reporters=console --no-intro
```

---

### W0 — Git prep: `main` becomes the 3D trunk

**Goal.** Make `main` the 3D trunk by fast-forwarding it onto the real work, then open a working
branch so the whole refactor is one `git branch -D` away from being abandoned cleanly.

**Preconditions.** Working tree clean. `main` is a strict ancestor of `phase-7-3d-foundations`
(verified §2 — re-check before merging).

**Steps.** Verify the ancestry, fast-forward `main`, hand the push to the user, then branch.
`phase-7-3d-foundations` is left in place as a historical pointer — do not delete it.

**DoD.** `main` == `30150b3`. `feature/engine-split` exists and is checked out. Working tree
clean. Nothing built or changed.

**📋 PROMPT**

```
Execute work order W0 from docs/plans/28-phase29-engine-split-plan.md in C:\dev\Cosmic.

Read §0 and §12/W0 first. This phase makes NO file edits — it is git only.

1. Confirm the working tree is clean and confirm `main` is still a strict ancestor of
   phase-7-3d-foundations (git merge-base --is-ancestor, and git rev-list --left-right --count).
   If either check fails, STOP and report — do not force anything.
2. git fetch origin
3. git checkout main && git merge --ff-only phase-7-3d-foundations
4. STOP. Tell me to run `git push origin main` myself. Do NOT push.
5. After I confirm the push, git checkout -b feature/engine-split main

Do not delete phase-7-3d-foundations. Report the resulting `git log --oneline -3` and
`git branch -vv`.
```

**▶️ YOUR COMMANDS**

```bash
git push origin main
```

---

### W1 — Build flags, presets, skip-list, `.bat` scripts

**Goal.** Land all the build-system machinery with **zero source edits**, so the 3D build is
provably unaffected and the flag exists for later phases to use. The 2D configuration is **not**
expected to compile yet — that is W6.

**Preconditions.** W0 green; on `feature/engine-split`.

**Edit sites.** §8.1 (`Cosmic/CMakeLists.txt` L76, L83, L92, L132–138, L146, L168), §8.2 (root
`CMakeLists.txt` L69/L74), §8.3 (new `CMakePresets.json`), §5.2 (the `.bat` set).

**Gotchas.**
- The exclusion regex list must be written now but will reference files that do not exist yet
  (`Scene3D.cpp`, `Components3D.h`, `TypeRegistry3D.cpp`). `list(FILTER)` on absent files is
  harmless — write them in anyway.
- `target_compile_definitions` for `COSMIC_2D_ONLY` must be **PUBLIC**, not PRIVATE — Starforge,
  projects and tests all need to see it. Note line 146 currently sets `COSMIC_BUILD_DLL` PRIVATE;
  add a separate PUBLIC call rather than changing that one.
- Verify the `CMakePresets.json` generator string against the installed VS before committing.
- Keep `pause` in the new `.bat` files — they are for the user, not for automation.
- `build.bat` must **not** force `COSMIC_2D_ONLY`; it only reads and echoes it (§5.2).

**DoD.** 3D build Debug+Release zero warnings; 355/355; conformance clean; `cmake --preset
default` configures; `cmake --preset 2d` *configures* (it will not build yet — that is expected
and should be stated in the commit message).

**📋 PROMPT**

```
Execute work order W1 from docs/plans/28-phase29-engine-split-plan.md in C:\dev\Cosmic
(branch feature/engine-split).

Read §0, §5.2, §8.1, §8.2, §8.3 and §12/W1. This phase edits ONLY build files — no .cpp/.h.

Deliverables:
- Cosmic/CMakeLists.txt: add options COSMIC_2D_ONLY (OFF) and COSMIC_WITH_JOLT (ON); gate Jolt
  (L76) on COSMIC_WITH_JOLT; gate recastnavigation (L83) on NOT COSMIC_2D_ONLY; make the assimp
  block (L92-127) additionally require NOT COSMIC_2D_ONLY; add list(FILTER ... EXCLUDE REGEX ...)
  after the GLOB (L132-138) implementing the §4 exclusion table; add a PUBLIC
  COSMIC_2D_ONLY compile definition; convert the Jolt/RecastNavigation link entries (L168) to
  generator expressions.
- Root CMakeLists.txt: declare/forward both options; add COSMIC_SKIP_PROJECTS and extend the
  scanner gate at L74; default the skip list to Frontier;Engine3DDemo;ForgeIsle when 2D.
- NEW CMakePresets.json with presets "default" and "2d". VERIFY the generator string against the
  installed Visual Studio first (read CMAKE_GENERATOR out of build/CMakeCache.txt).
- build.bat / build_all.bat / build_engine.bat: make them MODE-PRESERVING — read
  COSMIC_2D_ONLY:BOOL=ON from build\CMakeCache.txt and echo "[MODE] 2D-only engine" or
  "[MODE] full 3D engine". Do NOT force the value.
- NEW build_2d.bat, build_3d.bat, build_all_2d.bat — explicit mode setters mirroring the existing
  scripts' structure, keeping the trailing pause.

The 2D preset is expected to CONFIGURE but NOT COMPILE at this stage. Say so in the commit message.

Verify with the §12 recipe: 3D Debug+Release zero warnings, CosmicTests 355/355, conformance
clean, and `cmake --preset 2d` configures without error. Commit locally (no Claude trailer, no
push) and report.
```

**▶️ YOUR COMMANDS** — none beyond reviewing the commit.

---

### W2 — The safety net (do this before touching any source)

**Goal.** Build the regression net *before* the refactor, so every later phase has an objective
pass/fail. This is the largest phase and the one that makes the rest safe.

**Preconditions.** W1 green.

**Deliverables.** §9.5 (the `CosmicRenderTests` target, `GoldenImage`, all 2D **and** 3D
goldens), §9.2 (the seven new 2D invariant suites plus the `BuildSpriteDrawList` extraction),
§9.3 (`test_render_desc` and `test_components3d_registry` baselines — written *now*, against the
un-split code, so they capture the pre-refactor truth), and the **3D clean-build time baseline**.

**Gotchas.**
- `BuildSpriteDrawList` is a **pure code-motion extraction** — the sort block is
  `Scene.cpp:959–988`, **not 952**. Lines 952–958 are the registry views plus the "no 2D content
  ⇒ no GL calls" compat gate and must stay in `OnRenderSprites`.
- `FrameBuffer::ReadPixels` requires the FBO to be **bound**, and returns **top-left-origin**
  RGBA8 — the same convention `ImageIO` uses, so no flip is needed anywhere.
- Golden tests must not name a single `gl*` token or `GL_*` enum — `check_gl_conformance.ps1`
  scans `tests/`.
- Generate goldens **once** with `--update-goldens`, eyeball every PNG before committing, then
  never regenerate without an explicit decision.
- `test_components3d_registry`'s baseline must be captured from the *current* single
  `Components.h` so W4 can prove nothing changed.
- Record the clean-build time with `Measure-Command` on a **fresh** `build/` directory.

**DoD.** 3D Debug+Release zero warnings; 355 + new cases all pass; conformance clean;
`CosmicRenderTests` builds and all goldens generate and then re-verify byte-identically on a
second run; the 3D clean-build wall time is recorded in the commit message.

**📋 PROMPT**

```
Execute work order W2 from docs/plans/28-phase29-engine-split-plan.md in C:\dev\Cosmic
(branch feature/engine-split). This is the biggest phase — the regression net that every later
phase depends on. Read §0, §9.2, §9.3, §9.5 and §12/W2 in full.

Deliverables:
1. Golden-image harness: option(COSMIC_BUILD_RENDER_TESTS OFF); tests/render/CMakeLists.txt;
   tests/render/render_main.cpp (Window + Renderer::Init bootstrap, --update-goldens);
   tests/render/GoldenImage.{h,cpp} using FrameBuffer::ReadPixels + ImageIO (BOTH are top-left
   origin RGBA8 — no flip). Tolerance per-channel <=2, differing-pixel budget <=0.1%, emit
   .actual.png/.diff.png on mismatch.
2. 2D goldens: sprites, tilemap, light2d (including the byte-identical A/B: white-ambient-no-lights
   frame vs pass-skipped frame, memcmp equal), ui, scene2d.
3. 3D goldens: mesh_pbr, terrain, water, sky_ibl, particles, postchain, outline, instancing.
4. Extract Scene::BuildSpriteDrawList() from Scene::OnRenderSprites as PURE code motion. The sort
   block is Scene.cpp:959-988 — lines 952-958 (registry views + the "no 2D content => no GL calls"
   compat gate) STAY in OnRenderSprites.
5. New tests: test_sprite_order, test_light2d, test_sprite_animation, test_tilemap_extra,
   test_ui_anchor, test_timeline_state, test_scene2d_determinism, test_render_desc,
   test_components3d_registry. Register them in tests/CMakeLists.txt (hand-list at L7-65).
6. Record the 3D clean-build wall time: delete build/, then Measure-Command around a full
   configure + Release build. Put the number in the commit message.

No 3D pixel or behaviour change is permitted. Nothing outside Scene.cpp's sprite extraction may
move. Verify with the §12 recipe plus a CosmicRenderTests run that passes twice in a row.
Commit locally (no Claude trailer, no push) and report the build-time number and the test count.
```

**▶️ YOUR COMMANDS** — review the generated goldens in `tests/render/goldens/` before the commit
is considered good. They are the reference for the whole refactor.

---

### W3 — Pluggable physics backend

**Goal.** Give physics a swappable backend without changing `PhysicsWorld`'s public API or any
call site — the deliverable capability behind decision 3.

**Preconditions.** W2 green (the physics tests and `test_physics_determinism` are the net).

**Edit sites.** §6.2 in full, plus the `ScenePhysics` fences in §6.4 (the fences can land here or
in W5 — landing them here keeps the physics work in one commit; either is acceptable, state which
you chose).

**Gotchas.**
- `PhysicsWorld` must stay a concrete, by-value-constructible class. Do **not** make it abstract —
  `PlayerLayer.h:75` and `StarforgeApp.h:247` hold it by value.
- Backend registration is an **explicit** `RegisterBuiltinPhysicsBackends()` call from
  `PhysicsWorld::Init`, not a file-scope static registrar.
- The registry map must be a function-local static, not a namespace-scope global.
- `PhysicsWorld.cpp:8`'s `renderer/Renderer3D.h` moves into `JoltBackend.cpp` and is fenced there
  (`DebugDraw`, `:947–984`, `JPH_DEBUG_RENDERER`).
- `test_physics_determinism` asserts **bit-identical** transforms across two runs. If it fails,
  the refactor changed something — do not adjust the test.
- Adding new `.cpp` files means a full reconfigure, not just a rebuild.

**DoD.** All six `test_physics_*` pass unchanged; `test_physics_backend` and `test_physics_2d`
pass; `COSMIC_WITH_JOLT=OFF` configures and builds with only the Null backend; 3D goldens
unchanged; zero warnings both configs of the 3D build.

**📋 PROMPT**

```
Execute work order W3 from docs/plans/28-phase29-engine-split-plan.md in C:\dev\Cosmic
(branch feature/engine-split). Read §0, §6 in full, and §12/W3.

Goal: PhysicsWorld becomes a DISPATCHER over a pluggable backend — the RenderCommand->RendererAPI
idiom — so the user can register their own physics implementation for a single app. PhysicsWorld's
public API must not change by one character; it is held BY VALUE at PlayerLayer.h:75 and
StarforgeApp.h:247, so do NOT make it abstract.

Deliverables:
1. NEW Cosmic/src/physics/PhysicsBackend.h — IPhysicsBackend (pure virtual, mirroring
   PhysicsWorld's public surface 1:1, all params in PhysicsTypes.h vocabulary) and
   PhysicsBackendRegistry (Register/Has/Names/SetDefault/Default/Create) over a FUNCTION-LOCAL
   static map. Plus COSMIC_API void RegisterBuiltinPhysicsBackends().
2. PhysicsTypes.h: PhysicsSettings gains `std::string Backend` (empty => registry default).
3. PhysicsWorld.h: only the private section changes — `struct Impl` / unique_ptr<Impl> becomes
   unique_ptr<IPhysicsBackend> m_Backend. Public block untouched.
4. PhysicsWorld.cpp: every method becomes a one-line forward. Init() calls
   RegisterBuiltinPhysicsBackends() then Create(settings.Backend or the default), logging and
   falling back to "null" on an unknown name.
5. NEW physics/backends/JoltBackend.cpp — move the entire Jolt body here (the <Jolt/...> includes,
   EnsureJoltGlobalInit, the contact listener and query filters, and struct Impl which becomes
   class JoltBackend final : public IPhysicsBackend). PhysicsWorld.cpp:8's renderer/Renderer3D.h
   comes along and gets an #ifndef COSMIC_2D_ONLY fence around DebugDraw's body.
6. NEW physics/backends/NullBackend.cpp — no-op backend, always registered.
7. Apply the §6.4 fences inside ScenePhysics.{h,cpp} (terrain, voxel, mesh-collider, and the
   voxel/VoxelVolume.h include at ScenePhysics.h:27 which exists only for IVec3Hash).
8. NEW tests/test_physics_backend.cpp — a fake IPhysicsBackend registered from the test, selected
   via PhysicsSettings::Backend, driven through PhysicsWorld -> ScenePhysics -> script contact
   dispatch. This doubles as the reference example for the docs.
9. NEW tests/test_physics_2d.cpp — RigidBody + BoxCollider in XY: gravity along -Y, no Z drift
   over 600 steps, 2D trigger enter/exit, determinism across two runs with ThreadCount=0.

test_physics_determinism asserts BIT-IDENTICAL transforms across two runs. If it fails, your
refactor changed behaviour — fix the refactor, never the test.

Also verify a COSMIC_WITH_JOLT=OFF configure+build succeeds with only the Null backend.
Verify with the §12 recipe + CosmicRenderTests (3D goldens must be unchanged). Commit locally
(no Claude trailer, no push) and report.
```

**▶️ YOUR COMMANDS** — none.

---

### W4 — Component partition, reflection split, umbrella fences

**Goal.** Make every component classifiable, split the reflection registration, and fence the
umbrella header — after which SF_Telem's 3D coupling is gone.

**Preconditions.** W3 green.

**Edit sites.** §7.1 (`Components.h` → + `Components3D.h`), §7.2 (`TypeRegistry3D.cpp`), §7.4
(the two serializer casts), §7.7 (`Cosmic.h`), plus `CosmicPCH.h` audit and include fix-ups in
every 3D TU.

**Gotchas.**
- `Components.h` **keeps** `#include "physics/PhysicsTypes.h"` (line 11) — physics is shared.
- The `entt::type_hash` specializations at **1284–1308** are Tag/Transform/SpriteRenderer only —
  they stay. There are no 3D ones to move.
- The `CS_REGISTER_COMPONENT` block at **1241–1274** is at global scope; both halves must remain
  at global scope after the split.
- `Cosmic.h` has **no nav or voxel include** — don't hunt for one. Keep line 42
  (`SceneRenderer.h`) unfenced.
- Add `#include "scene/Components3D.h"` to `Cosmic.h` inside the fence so 3D projects still get
  everything.
- `test_components3d_registry` (from W2) must pass **unchanged** — it is the proof that names and
  `type_hash` values survived.
- **SF_Telem canary:** after this phase, confirm SF_Telem still builds in the 3D config.

**DoD.** 3D Debug+Release zero warnings; full suite passes with `test_components3d_registry`
green; conformance clean; all goldens byte-match W2's baselines; SF_Telem builds.

**📋 PROMPT**

```
Execute work order W4 from docs/plans/28-phase29-engine-split-plan.md in C:\dev\Cosmic
(branch feature/engine-split). Read §0, §7.1, §7.2, §7.4, §7.7 and §12/W4.

Split scene/Components.h (1309 lines, 34 components):
- KEEP 19 in Components.h: the 10 dimension-neutral (ID, Opaque, Relationship, Tag, Transform,
  Camera, Environment, NativeScript, SystemScript, Prefab), the 4 2D (SpriteRenderer,
  SpriteAnimation, Tilemap, Light2D), and the 5 physics-viable (RigidBody, Box/Sphere/Capsule
  colliders, CharacterController). Components.h KEEPS #include "physics/PhysicsTypes.h" (line 11)
  and drops Skeleton.h (8) / AnimationClip.h (9) / ParticleSystem.h (10).
- MOVE 15 into NEW scene/Components3D.h (which #includes Components.h): MeshRenderer,
  PrimitiveMesh, LODGroup, Animator, Socket, DirectionalLight, PointLight, Terrain, Water,
  ParticleEmitter, VoxelVolume (+ the VoxelRenderData fwd decl at 770), MeshCollider,
  TerrainCollider, NavMesh, NavAgent.
- The CS_REGISTER_COMPONENT block at 1241-1274 splits the same way; both halves stay at GLOBAL
  scope. The entt::type_hash specializations at 1284-1308 (Tag/Transform/SpriteRenderer) STAY in
  Components.h — there are no 3D ones.

Also:
- NEW reflect/TypeRegistry3D.cpp exporting RegisterEngine3DTypes(), called from
  RegisterEngineTypes inside an #ifndef COSMIC_2D_ONLY fence. Physics registrations
  (TypeRegistry.cpp:308-360) STAY except MeshCollider (345-348) and TerrainCollider (353).
- SceneSerializer.cpp: fence the two MeshRendererComponent casts at :188 and :270. There are no
  others — verify by grepping all 15 3D type names.
- Cosmic.h: fence lines 35, 40, 41, 43, 44, 45, 46, 47, 48, 49, 62, 68, 97, 106 and ADD
  #include "scene/Components3D.h" inside the fence near line 94. Keep 42 (SceneRenderer.h) and
  116-122 (physics) UNFENCED. There is no nav or voxel include in this file.
- Audit CosmicPCH.h for the same treatment.
- Add #include "scene/Components3D.h" to every 3D TU that needs it (Scene.cpp for now,
  SceneRenderer.cpp, ScenePicker.cpp, SceneNav.cpp, ScenePhysics.cpp, TypeRegistry3D.cpp, the 3D
  Starforge files).

test_components3d_registry (from W2) must pass UNCHANGED — it pins every component name and
type_hash. All W2 goldens must byte-match. Confirm SF_Telem still builds.
Verify with the §12 recipe. Commit locally (no Claude trailer, no push) and report.
```

**▶️ YOUR COMMANDS** — none.

---

### W5 — `Scene.cpp` → `Scene3D.cpp` and the remaining engine fences

**Goal.** Finish the engine-side partition so that only `SceneRenderer` still mixes the two
worlds.

**Preconditions.** W4 green.

**Edit sites.** §7.3 (the move list and `Scene.h` fences), §7.5 (`ScriptableEntity.h`),
`renderer/Renderer.cpp` (`Renderer3D::Init/Shutdown` lines), `layers/PlayerLayer.{h,cpp}`
(nav + `UpdateAnimators` only), and the §6.4 `ScenePhysics` fences if they did not land in W3.

**Gotchas.**
- **`GatherSceneLights` (86–117) is a third helper** in the 44–118 anon-namespace block, called
  by both `OnRender3D` and `BuildRenderDesc`. It moves. The original draft of this plan missed it.
- **`FindEnvironment` (1481–1486) sits between `SubmitOpaqueMeshes` and `BuildRenderDesc` and
  STAYS.** Do not sweep it along with its neighbours.
- **`OnRender`'s closing brace at line 919 carries a trailing comment**
  (`} // Closes void Scene::OnRender()`), so it will not match a bare `^\t}$` anchor. Any
  brace-counting approach mis-targets here.
- The four physics methods (**130–155**) **stay in `Scene.cpp`**. Only nav moves.
- In `Scene.h`, fence **only** the nav block (121–140), `m_NavRuntime` (402), and the moved 3D
  declarations (390, 394). The physics block (95–119) and `m_Physics` (401) stay.
- In `ScriptableEntity.h`, fence includes **34–37** but **not 33** (that one is physics).
- `PlayerLayer.cpp:251` (`UpdateSpriteAnimations`) and `:252` (`UpdateAnimators`) are adjacent —
  fence only 252.
- `test_render_desc` and `test_scene3d_lifecycle` (W2/W3) are the nets. A dropped
  `SceneRenderDesc` field is otherwise invisible.
- Adding `Scene3D.cpp` requires a reconfigure.

**DoD.** 3D Debug+Release zero warnings; full suite green including `test_render_desc`; goldens
byte-match; conformance clean; SF_Telem builds.

**📋 PROMPT**

```
Execute work order W5 from docs/plans/28-phase29-engine-split-plan.md in C:\dev\Cosmic
(branch feature/engine-split). Read §0, §7.3, §7.5 and §12/W5.

Split scene/Scene.cpp (1560 lines) by moving these VERBATIM into NEW scene/Scene3D.cpp:
PrimitiveSignature 48-61, BuildPrimitiveMesh 65-80, GatherSceneLights 86-117 (<- a THIRD helper
in that anon-namespace block; it is called by both OnRender3D and BuildRenderDesc), OnNavStart
158-168, OnNavStep 170-174, OnNavStop 176-183, SyncPrimitiveMeshes 185-238, SyncWorldSystems
240-295, SyncVoxelVolumes 297-419, SyncNavMeshes 421-431, OnRenderWorldFX 433-488, OnRender3D
1162-1214, FindAnimatorFor 1219-1233, UpdateAnimators 1235-1363, SubmitOpaqueMeshes 1366-1479,
BuildRenderDesc 1490-1560. Move their 3D includes with them.

STAYS in Scene.cpp: ctor/dtor, entity CRUD + hierarchy 490-742, OnUpdate 744-779 (fence ONLY the
UpdateAnimators(deltaTime) call at line 749), OnFixedUpdate 781-807, THE FOUR PHYSICS METHODS
130-155, legacy OnRender 809-919, UpdateSpriteAnimations 921-947, OnRenderSprites 949-1130,
OnRender2DLights 1132-1160, FindEnvironment 1481-1486 (<- it sits BETWEEN two moved functions;
do not sweep it along), WorldOf.

GOTCHA: OnRender's closing brace at 919 is `} // Closes void Scene::OnRender()` — it will not
match a bare tab-brace anchor. Find boundaries by content.

Scene.h: fence ONLY the nav block 121-140, m_NavRuntime at 402, and the moved 3D decls at 390 and
394. The physics block 95-119 and m_Physics at 401 stay UNFENCED.

ScriptableEntity.h: fence includes 34-37 (voxel x2, SceneNav, NavWorld) but NOT 33 (ScenePhysics).
Fence Voxels() 297, Animator() 327, Nav() 362 and their proxies. Keep Physics() 172, Character()
191, Flow() 242, Telemetry() 110. Fence NavCritter.h's include and its cases in
test_template_scripts.cpp.

Also fence: the Renderer3D::Init/Shutdown lines in renderer/Renderer.cpp; PlayerLayer's nav calls
(OnNavStart/Step/Stop) and the UpdateAnimators call at PlayerLayer.cpp:252 — but NOT :251
(UpdateSpriteAnimations) and NOT any physics call.

test_render_desc must pass unchanged — it exists precisely to catch a SceneRenderDesc field lost
in this code motion. All goldens must byte-match. Confirm SF_Telem still builds.
Verify with the §12 recipe. Commit locally (no Claude trailer, no push) and report.
```

**▶️ YOUR COMMANDS** — none.

---

### W6 — `SceneRenderer` partition — the 2D build comes alive

**Goal.** Partition the render spine so the 2D configuration compiles and links for the first
time, with the 3D build pixel-identical.

**Preconditions.** W5 green. This is the **riskiest phase**; the 3D golden set from W2 is the
gate.

**Edit sites.** §7.6 in full.

**Gotchas.**
- 2D must keep flowing **through** `SceneRenderer` — do not write a separate 2D compositor. The
  pass contract (`docs/design/frame-lifecycle.md` §5) is preserved exactly.
- `PassTransparents` (598–643) is where **sprites** draw. Fence only its 3D interior
  (water, particles), never the whole function.
- `PassPostAndComposite` (646–717): fence only the sky-resource sampling. Tonemap, FXAA, bloom and
  vignette stay — they are the 2D post chain too.
- `ScenePicker` is excluded in 2D ⇒ `PassOutline` (720–760) fences out entirely. Sprites have no
  picker or outline path today, so this is a no-op for 2D content — the `scene2d` golden proves it.
- There is no out-of-line `SceneRenderer` constructor; don't go looking for one.
- The 3D goldens must byte-match. If `postchain` or `water` drifts, the fence is in the wrong place.
- This is the first phase where the 2D suite runs. Expect to discover missing gates; fix them here.

**DoD.** 3D Debug+Release zero warnings, full suite, all goldens byte-match. **2D preset
configures, builds Debug+Release clean, its suite (~270–290) passes, and the 2D goldens
byte-match.** SF_Telem builds and runs in **both** configurations. Conformance clean in both.

**📋 PROMPT**

```
Execute work order W6 from docs/plans/28-phase29-engine-split-plan.md in C:\dev\Cosmic
(branch feature/engine-split). Read §0, §7.6, §9.1 and §12/W6. This is the riskiest phase — the
3D golden set from W2 is your gate.

Partition renderer/SceneRenderer.{h,cpp} so the 2D configuration compiles for the first time.
2D KEEPS flowing through SceneRenderer (HDR target -> sprites via DrawTransparent -> Light2D
multiply -> post tonemap/FXAA/bloom/vignette -> DrawOverlay2D for UI). Do NOT write a separate 2D
compositor.

SceneRenderer.h — SceneRenderDesc is 193-232. KEEP: 195 (View/Projection/CameraPosition), 196
(SetCamera), 199, 200 (Settings), 208 (EcsScene), 230 (DrawTransparent), 231 (DrawOverlay2D).
FENCE: 198 (Lights), 202-207 (Terrain/Water/reflection/Emitters/Ribbons/Distortion), 213
(DetailedSky), 219-222 (Coverage; check whether DeltaTime is needed by 2D), 227 (SelectedEntities),
229 (DrawOpaque). Also fence the Renderer3D/EnvironmentMap/ShadowMap includes, SceneDrawContext's
submit verbs, the m_Environment/m_Shadow/outline members, and the decls at 290, 291, 292, 296.

SceneRenderer.cpp (761 lines) — fence WHOLE: PassShadow 379-438, PassCoverage 441-495,
PassReflection 498-544, PassOutline 720-760. Fence PARTIALLY: Init 188-201 and Shutdown 203-220
(env/shadow lines), ApplyEnvironment 235-295 (desc.Lights writes + owned-EnvironmentMap block),
Render 301-351 (steps 2-3 and the shadow/coverage/reflection calls), PassOpaqueHDR 547-595 (3D
interior), PassTransparents 598-643 (3D interior ONLY — sprites draw here), PassPostAndComposite
646-717 (sky-resource sampling ONLY — tonemap/FXAA/bloom/vignette stay). Audit the anon-namespace
helpers at 29-174.

Then get the 2D configuration to build and pass: gate tests/CMakeLists.txt per §9.1 (note FIVE of
six test_physics_* files STAY in the 2D suite; test_physics_scene.cpp:35 needs an in-file fence
for its TerrainCollider round-trip), and fix whatever else the 2D link surfaces.

DoD: 3D Debug+Release zero warnings + full suite + ALL goldens byte-match W2's baselines; 2D
preset configures, builds Debug+Release clean, its suite passes, 2D goldens byte-match;
conformance clean in both; SF_Telem builds in BOTH configurations.

If a 3D golden drifts, a fence is in the wrong place — fix the fence, never the golden.
Commit locally (no Claude trailer, no push) and report both suites' counts.
```

**▶️ YOUR COMMANDS** — none.

---

### W7 — Starforge gating: the 2D editor boots

**Goal.** Make the editor usable in the 2D configuration — 3D panels and menus absent, 2D
authoring intact, colliders visible.

**Preconditions.** W6 green.

**Edit sites.** §8.5 in full, plus the new Renderer2D collider overlay from §6.4.

**Gotchas.**
- **`m_Physics` at `StarforgeApp.h:247` stays**, along with every play-session physics call. Only
  nav is fenced in the play session.
- `m_Mode2D` defaults **`true`** under `COSMIC_2D_ONLY`, and the 2D toggle is pinned on.
- ForgePong and FlowDemo scaffolds **remain**; only the 3D samples (ForgeBlocks etc.) are fenced.
- `EnvironmentPanel` and `HierarchyPanel` are verified 3D-free — **do not touch them**.
  `Ambient2D` must stay editable.
- "Fit to mesh" in `InspectorPanel.cpp:465–494` reads mesh bounds, so it is 3D-only even though it
  acts on Box/Sphere colliders that exist in 2D.
- `ViewportController.cpp:8` keeps its `ScenePhysics.h` include; only
  `GetPhysics()->World().DebugDraw()` (787–788) fences.

**DoD.** Both configurations build clean and pass their suites; goldens byte-match; the 3D editor
boots with both modes intact; the 2D editor boots straight into 2D mode with 3D panels/menus
absent, tile painting, Light2D, Flow/Story editors, ForgePong and FlowDemo running in Play, and
the collider overlay visible.

**📋 PROMPT**

```
Execute work order W7 from docs/plans/28-phase29-engine-split-plan.md in C:\dev\Cosmic
(branch feature/engine-split). Read §0, §6.4, §8.5 and §12/W7.

Gate the Starforge editor for the 2D configuration:
- Projects/Starforge/CMakeLists.txt: list(FILTER) out panels/VoxelPanel.*,
  panels/WorldSystemsPanel.*, editors/AnimationEditor.* after the GLOB at L42.
- StarforgeApp.h: fence m_WorldSystems, m_Voxel, m_NavBakes (251), the AnimationEditor
  registration. m_Mode2D defaults TRUE under the flag. m_Physics (247) STAYS.
- StarforgeApp.cpp: fence nav in the play session, the voxel + world-systems menus and panel
  draws, the 3D entity-creation menus, nav bake ticking, and the 3D sample scaffolds. ForgePong
  and FlowDemo REMAIN. All physics call sites (660-661, 696, 698, 756, 763, 786-788, 799-801)
  stay unfenced. Pin the 2D toggle on.
- ViewportController.{h,cpp}: keep the ScenePhysics.h include at :8; fence
  GetPhysics()->World().DebugDraw() at 787-788; fence the 3D grid and nav overlays; keep the 2D
  pixel grid and gizmo. ADD a Renderer2D collider overlay (Box/Sphere/Capsule projected onto XY
  via DrawRect/DrawLine) so 2D physics is visually debuggable.
- Also gate: panels/InspectorPanel.cpp 3D sections (note "Fit to mesh" at 465-494 is 3D-only —
  it reads mesh bounds), AssetTypes.cpp, PreviewRig.cpp, commands/EditorCommands.cpp, and the
  Renderer3D stat rows in ProfilerPanel/SystemPanel.
- DO NOT TOUCH EnvironmentPanel or HierarchyPanel — both are verified 3D-free, and Ambient2D must
  stay editable.

DoD: both configurations build Debug+Release clean and pass their suites; all goldens byte-match;
conformance clean in both. Then report exactly what I need to check by hand on-GPU.
Commit locally (no Claude trailer, no push).
```

**▶️ YOUR COMMANDS** — on-GPU smoke: launch the 3D editor (both modes intact), then the 2D editor
(boots into 2D mode, 3D panels/menus gone, tile painting + Light2D + Flow/Story editors work,
ForgePong and FlowDemo run in Play, collider overlay draws).

---

### W8 — Cut `engine-2d`, set up the worktree, record the numbers

**Goal.** Land the refactor on `main`, create the 2D branch, and prove the build-time claim.

**Preconditions.** W7 green and your on-GPU smoke passed.

**Gotchas.**
- **Do not create a second build directory in the same source tree.** `COSMIC_SDK_DIR` is
  source-relative, so both would write to `C:/dev/Cosmic/build/Runtime/<CONFIG>` and clobber each
  other. The worktree is the mechanism (§8.4).
- Measure clean builds from a **deleted** `build/` directory in each tree, same configuration
  (Release), same machine state.

**DoD.** `main` contains the whole refactor; `engine-2d` exists and is pushed; `../Cosmic-2D` is a
working worktree that configures and builds with `cmake --preset 2d`; plain `build.bat` in that
worktree builds 2D and echoes `[MODE] 2D-only engine`; the clean-build times for both
configurations are recorded.

**📋 PROMPT**

```
Execute work order W8 from docs/plans/28-phase29-engine-split-plan.md in C:\dev\Cosmic.
Read §0, §5, §8.4, §13 and §12/W8.

1. git checkout main && git merge --ff-only feature/engine-split
2. STOP — tell me to run `git push origin main`. Do NOT push.
3. After I confirm: git branch engine-2d main
4. STOP — tell me to run `git push -u origin engine-2d`. Do NOT push.
5. After I confirm: git worktree add ../Cosmic-2D engine-2d
6. Configure the worktree with the 2d preset and build it.
7. MEASURE, with Measure-Command, on a freshly deleted build/ directory in each tree, Release
   configuration: the 3D clean build in C:\dev\Cosmic and the 2D clean build in C:\dev\Cosmic-2D.
   Also measure an incremental build in each after touching Cosmic/src/scene/Components.h.
   Compare against the W2 baseline. Expectation is roughly a 40-55% cut on the clean build,
   dominated by assimp (159 TUs) and recast (26 TUs) not being configured.
8. Verify plain `build.bat` in C:\dev\Cosmic-2D echoes "[MODE] 2D-only engine" and builds 2D
   (inspect the script's logic — do NOT run it yourself, it ends in pause).

Report the four timings honestly. If the reduction is materially below expectation, say so and
propose next levers (COSMIC_WITH_JOLT=OFF is worth another 133 TUs) rather than accepting it
quietly. Do not commit anything to engine-2d in this phase.
```

**▶️ YOUR COMMANDS**

```bash
git push origin main
```

```bash
git push -u origin engine-2d
```

Then, in `C:\dev\Cosmic-2D`, run `build.bat` yourself and confirm it reports 2D mode and is fast.

---

### W9 — Telemetry, serial and SF_Telem hardening + fuzz campaign

**Goal.** Fix the two confirmed `DataPlayer` crash bugs and build the robustness suite around the
telemetry/serial stack that SF_Telem depends on.

**Preconditions.** W8 done. **Land this on `main`**, then carry it across to `engine-2d` — the
code is shared, not 2D-specific.

**Edit sites.** §9.6 in full.

**Gotchas.**
- There is **no `try`/`catch` anywhere in `DataPlayer.cpp`** — an exception is a process kill.
- The truncation predicate at **167** is inverted; fixing it will make previously-"successful"
  truncated loads start failing. That is the correct new behaviour — `test_telemetry_roundtrip`
  guards that *valid* files are unaffected.
- Only a v1 binary path exists. Do not invent v2/v3 handling.
- `TelemHub::PumpSerial`'s seam is exact: **327–330** is I/O, **331–381** is pure state. Nothing
  else in the function needs to change.
- If constructing `TelemHub` headless proves unsafe (it owns a `Ref<Texture2D>`), fall back to the
  header-only `FrameAssembler.h` extraction rather than forcing it.
- `Projects/SF_Telem/src/Telemetry.h` is header-only and engine-free — include it directly in the
  test TU, the way `test_template_scripts.cpp:11-14` does.

**DoD.** All new tests pass, repeatedly (fuzz loops are seeded, so reruns are deterministic);
`test_telemetry_roundtrip` still passes unchanged; both configurations build clean and pass their
suites; SF_Telem builds, launches, connects, records and replays in both configurations.

**📋 PROMPT**

```
Execute work order W9 from docs/plans/28-phase29-engine-split-plan.md in C:\dev\Cosmic
(branch main). Read §0, §9.6 and §12/W9. This lands on main; I will carry it across to engine-2d
afterwards.

Fix two CONFIRMED bugs in Cosmic/src/telemetry/DataPlayer.cpp (LoadBinaryFile is 87-183; only a
v1 path exists at 111; there is NO try/catch anywhere in the file):
1. entityCount (read 113/115), chCount (134) and sampleCounts[e] (136) are used UNVALIDATED at
   124, 125, 138, 146 and 148. A corrupt 0xFFFFFFFF requests hundreds of GB -> bad_alloc ->
   std::terminate. Validate every count against the remaining file size plus hard caps
   (entities <= 4096, channels <= 1024); CS_CORE_ERROR + return false on violation.
2. Line 167 reads `if (!file.good() && !file.eof())`. A truncated file sets BOTH eofbit and
   failbit, so this is false and the function returns true at 178 with silently zero-filled
   frames. Fix the predicate so truncation is a failure.
test_telemetry_roundtrip.cpp is the regression net — valid files must load identically.

New tests:
- tests/test_telemetry_robustness.cpp — empty/bad-magic/unknown-version; truncation sweep over a
  real DataRecorder-produced file; absurd counts; ~200-iteration seeded random-byte fuzz. Plus
  DataRecorder: autosave rolling-folder overwrite, Flush during concurrent Record (4 threads x
  10k), Clear keeps registrations, double-Flush idempotence, destructor-during-flush.
- tests/test_serial_lifecycle.cpp — SerialPort: bad port fails fast, Write-on-closed, idempotent
  Close, GetAvailablePorts no-crash, BeginOpen invalid -> Failed under timeout, the abandon race
  (BeginOpen then immediate Close), double-BeginOpen no-op, destroy-while-Connecting. SerialLink:
  unreachable Connect + OnUpdate loop bounded-time, auto-reconnect after k_ReconnectInterval,
  one-shot ConsumeJustConnected, Shutdown clears WantConnection.
- tests/test_sftelem_protocol.cpp — include Projects/SF_Telem/src/Telemetry.h directly (it is
  header-only and engine-free). ParseFrame (249-285): checksum mismatch, '#' heartbeat, unknown
  tag, truncated, missing '*', non-numeric -> false, no crash. Drive/WeaponSample ToChannels/
  FromChannels round-trip. Seeded mutation fuzz.
- tests/test_sftelem_hub.cpp — first refactor TelemHub::PumpSerial (TelemHub.cpp:323-382): lines
  327-330 are the only I/O, 331-381 are pure string->state. Extract the latter as public
  IngestChunk(const std::string&) and compile TelemHub.cpp into CosmicTests. Tests: partial-line
  reassembly across chunks, good/bad frame stats, per-ESC presence/staleness, the 4KB accumulator
  purge at 381, replay-mode ignores live bytes, Shutdown flushes a dirty recording. FALLBACK if
  headless TelemHub construction is unsafe (it owns a Ref<Texture2D> at TelemHub.h:177): extract
  a header-only FrameAssembler.h instead and leave TelemHub.cpp out of the test target. Say which
  path you took.
- tests/test_crossbuild_scene.cpp — a scene with 3D components round-trips through load+save with
  every 3D block byte-preserved (the OpaqueComponentsComponent proof).
- Extend tests/test_audio.cpp: after AudioEngine::Init(), CoInitializeEx(nullptr,
  COINIT_APARTMENTTHREADED) must NOT return RPC_E_CHANGED_MODE (pins the MA_COINIT_VALUE STA fix).

Verify with the §12 recipe in both configurations; run the fuzz tests several times. Commit
locally (no Claude trailer, no push) and report.
```

**▶️ YOUR COMMANDS**

```bash
git push origin main
```

Then carry the change across to `engine-2d` (§13) and smoke SF_Telem in both trees: launch,
connect to a port, record, replay.

---

### W10 — Documentation

**Goal.** Write the documentation planned in §10, now that the separation is verified working.

**Preconditions.** W0–W9 complete and verified on-GPU.

**Deliverables.** D41–D45, exactly as listed in §10.

**Gotchas.**
- Follow each destination's format contract: `docs/systems/README.md` §"Document format" for
  systems docs, `docs/reference/README.md` §"Entry format" for the reference doc.
- Use the **real recorded numbers** from W2 and W8 in D41 — do not restate the estimate.
- D44 must include an honest deviation section: everywhere the implementation diverged from this
  plan, and why.
- Closing G3 in `modularity-audit.md` means editing an existing analysis document — add the
  resolution, don't rewrite the finding.

**DoD.** All four new docs exist and follow their format contracts; all listed updates land;
`docs/plans/12-documentation-plan.md` registers D41–D45; this document carries ✅ status lines
with dates and a deviation section.

**📋 PROMPT**

```
Execute work order W10 from docs/plans/28-phase29-engine-split-plan.md in C:\dev\Cosmic
(branch main). Read §0, §10 and §12/W10. This phase writes documentation only — no code.

New documents:
- D41 docs/systems/build-2d-3d-split.md — what COSMIC_2D_ONLY excludes and why; the
  classification rule for new code; build.bat / build_2d.bat / presets / worktree layout; THE
  REAL RECORDED BUILD TIMES from W2 and W8 (not the estimate); the branch and carry-over workflow.
- D42 docs/systems/physics-backends.md — IPhysicsBackend, the registry,
  PhysicsSettings::Backend, the fixed-step contract, ThreadCount/determinism expectations,
  RayHit::EntityId round-trip, and a worked example lifted from test_physics_backend.cpp.
- D43 docs/reference/physics.md — the per-call reference for PhysicsWorld / PhysicsTypes /
  PhysicsBody / CharacterController / ScenePhysics. This chapter does not exist today. Follow
  docs/reference/README.md's entry format.
- D44 — update 28-phase29-engine-split-plan.md itself: status banners, per-work-order status
  lines with dates, and an HONEST deviation section covering everywhere implementation diverged
  from the plan and why.

Updates (D45): 00-MASTER-ROADMAP.md (Phase 29 entry, doc 28 index row, and a working-agreement
line that every change must leave BOTH configurations green); FEATURE-MATRIX.md (pure-2D engine
build, pluggable physics backend, 2D collider overlay as new rows; a paused row for 2D-native
particles); 12-documentation-plan.md (register D41-D45); design/modularity-audit.md (CLOSE G3
with the physics backend registry as the first real instance, and add a physics row to the §4
swap cookbook — add the resolution, do not rewrite the finding); systems/build-plugin-packaging.md
(the two-configuration story in its D34 section plan); systems/ecs-scene.md, systems/rendering-2d.md,
systems/rendering-3d.md, systems/README.md, reference/README.md (index/pointer updates for
Components3D.h, Scene3D.cpp, TypeRegistry3D.cpp); root README.md (§1.5 gains build_2d.bat /
build_3d.bat / build_all_2d.bat, plus a short section on the two configurations and which branch
is which).

Follow each destination's format contract. Commit locally (no Claude trailer, no push) and report.
```

**▶️ YOUR COMMANDS**

```bash
git push origin main
```

---

## 13. Git and branch runbook

Assistant runs everything except `git push`. Every push is the user's.

```bash
# W0 — main becomes the 3D trunk
git fetch origin
git merge-base --is-ancestor main phase-7-3d-foundations   # must exit 0
git checkout main
git merge --ff-only phase-7-3d-foundations
#   >>> USER: git push origin main
git checkout -b feature/engine-split main

# W1..W7 — one commit per phase on feature/engine-split

# W8 — land it and cut the 2D branch
git checkout main
git merge --ff-only feature/engine-split
#   >>> USER: git push origin main
git branch engine-2d main
#   >>> USER: git push -u origin engine-2d
git worktree add ../Cosmic-2D engine-2d
cd ../Cosmic-2D && cmake --preset 2d

# W9, W10 — land on main, then carry across
```

**Carrying a change across branches.** Both branches hold byte-identical tracked files, so a
change is literally the same file at the same path. Copy it over, build, test, commit. If you ever
prefer git to do it, `git merge main` from `engine-2d` (or `git merge engine-2d` from `main` for a
2D-first fix) is near-conflict-free by construction — but nothing in this design requires it.

`phase-7-3d-foundations` stays as a historical pointer; do not delete it. The stale
`origin/SF-CrashFixes` and `origin/SF-Improvements` branches are out of scope.

**Abandoning the refactor before W8** is `git branch -D feature/engine-split` — `main` is
untouched apart from the W0 fast-forward, which is pure history alignment.

---

## 14. Risk register

| # | Risk | Phase | Mitigation |
|---|---|---|---|
| 1 | **`SceneRenderer` partition causes 3D pixel regressions.** A fence in the wrong place silently drops a pass or a uniform. | W6 | The 3D golden set exists from W2, *before* the surgery. The guarded code is runtime-dead for 2D scenes. If a golden drifts, the fence is wrong — never update the golden. |
| 2 | **`Components.h` / registration split breaks entt subtly.** A duplicated or omitted `CS_REGISTER_COMPONENT`, or a changed `type_hash`, corrupts serialization in ways that look like data loss much later. | W4 | `test_components3d_registry` pins every name and hash against a baseline captured in W2, plus `test_reflect` / `test_scene_serializer` / `test_components` every phase. |
| 3 | **`Scene.cpp` split silently drops a `SceneRenderDesc` field.** Pure code motion, but a missed assignment is invisible until content goes missing at runtime. | W5 | `test_render_desc` compares the built desc field-by-field against a W2 baseline. |
| 4 | **Physics dispatcher changes behaviour.** | W3 | `test_physics_determinism` asserts bit-identical transforms across two runs; the other five physics suites cover bodies, events, characters and scene binding. The interface mirrors the existing API exactly, so the move is mechanical. |
| 5 | **Editor gating breadth** — many small fences across ~10 Starforge files, easy to miss one. | W7 | The 3D build compiles both paths continuously, so a mis-fence is a compile error rather than a silent behaviour change. |
| 6 | **Build-time win under-delivers.** Jolt staying (133 TUs) offsets some of the assimp win. | W8 | Measured, not assumed, and reported honestly. `COSMIC_WITH_JOLT=OFF` is the next lever and is already supported. |
| 7 | **Branch divergence creeps in** despite identical file sets. | ongoing | Nothing is allowed to be branch-conditional except the build cache and the preset choice. Any file that *must* differ is a design bug — fix it with a flag, not a divergent file. |

---

## 15. Acceptance matrix

| Gate | 3D config (`main`) | 2D config (`engine-2d` / `2d` preset) |
|---|---|---|
| Configure + build Debug **and** Release, zero warnings | every phase | from W6 |
| `CosmicTests.exe --reporters=console --no-intro` | 355 + new cases, all pass | ~270–290, all pass |
| `tests/check_gl_conformance.ps1` | clean | clean |
| `CosmicRenderTests` vs W2 baselines | 2D + 3D goldens byte-match | 2D goldens byte-match |
| Editor boots | both modes intact | 2D mode; 3D panels/menus absent; tile painting, Light2D, Flow/Story editors, ForgePong + FlowDemo in Play, collider overlay visible |
| SF_Telem | builds, launches, connects, records, replays | same |
| Clean-build wall time | 546.4 s → **169.4 s** with global `/MP` | 411.7 s → **123.1 s**; the split itself is **−24.7%**, short of target — see §5.3 |
| `COSMIC_WITH_JOLT=OFF` | configures and builds on the Null backend alone | same |
| Cross-build data safety | `test_crossbuild_scene` passes both directions | same |

---

*Changelog:*
*2026-07-24 — created; W0–W10 planned, nothing implemented.*
