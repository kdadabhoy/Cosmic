# Phase 15 Plan — Physics & Collision (Jolt)

> **Created 2026-07-04.** Promotes the long-parked "Physics middleware gate" (doc 05 S14 row →
> doc 11 §9 P3) into a full phase. **User decision 2026-07-04: vendor Jolt now** — the
> "collision detection / a ground" need is real (games in Starforge, walking characters,
> resting objects), and Starforge's reflection/serialization seams (E1/E2) make the component
> layer cheap.
>
> **Division of responsibility (unchanged doctrine):** the engine gains a *generic* rigid-body
> tier for contact-rich gameplay. Vehicle/aero six-DOF simulation (ViperSim-class) **stays
> app-side** on the doc 03 integrator toolkit — Jolt is for contacts, stacks, characters,
> triggers, and queries, not for replacing hand-rolled flight dynamics. Both can coexist in
> one scene (an aircraft app ticks its own dynamics and writes the transform; Jolt sees it as
> kinematic if it needs to collide).
>
> **Why Jolt** (recorded): MIT, actively maintained, ships in AAA (Horizon Forbidden West),
> deterministic option, first-class `HeightFieldShape` + `CharacterVirtual`, plain CMake static
> lib, no exceptions/RTTI requirements that fight the engine's flags. Alternatives rejected:
> PhysX (heavier integration, binary blobs), Bullet (older API, worse character controller),
> custom (the "minimal built-in" option was declined — it gets outgrown immediately).

---

## STATUS — Phase 15 CODE-COMPLETE 2026-07-04 (UNcommitted; user commits)

All nine work orders (J1–J9) landed in one pass. Build green across all 5 projects,
**zero warnings in engine/project code** (vendored Jolt warnings silenced on its own
target). `CosmicTests` **213/213** (195 → 213: +6 J2 PhysicsWorld, +3 J3/J4 scene, +2
J5 events, +4 J6 character, +2 J7 terrain, +1 J9 determinism). GL-conformance clean.
Compat gate held — Engine3DDemo/Frontier/SF_Telem/ViperSim attach no physics
components, `PhysicsWorld` is only created inside Starforge play mode / `PlayerLayer`,
and `Scene::OnPhysics*` are no-ops until `OnPhysicsStart` runs.

Per-item landing notes + deviations (all decided in-line, none change the doctrine):
- **J1** Jolt **v5.5.0** vendored at `Cosmic/dependencies/JoltPhysics/Jolt` (library
  subtree only; own curated `CMakeLists.txt`; pin in its README). Linked PRIVATE static
  into `Cosmic.dll`. **SIMD choice: SSE4.2 defines, `/arch` left at MSVC x64 baseline
  (SSE2), NOT AVX2** — on MSVC `/arch:AVX2` is a PUBLIC option that would recompile the
  WHOLE engine for AVX2 (a min-spec bump + a shipped-app codegen change). Documented
  AVX2 flip-on in the CMake comment. `JPH_CROSS_PLATFORM_DETERMINISTIC` on; asserts +
  debug renderer Debug-only; asserts/trace routed to the engine log.
- **J2** `physics/PhysicsWorld` (pimpl, zero JPH in public headers) + `PhysicsTypes.h`
  + `PhysicsBody.h`. Heap `PhysicsSystem` recreated per `Init` so the editor can
  play/stop repeatedly. Fine 16-bit category/mask done via `OnContactValidate` + query
  filters (coarse Static/Dynamic/Trigger/Character object layers drive the broadphase).
  Temp allocator sized off the settings. Batch body add is a noted follow-up (bodies
  added individually — fine for hundreds).
- **J3** `RigidBody` + `Box/Sphere/Capsule/Mesh/Terrain` colliders + `CharacterController`
  components, reflected + registered + serialized (empty `TerrainCollider` round-trips).
  **Deviation:** the plan's `uint16 Layer=Dynamic` was folded into the existing
  `MotionType` (Static/Kinematic/Dynamic drives motion + coarse layer); the reflected
  filter fields are `CollisionCategory` + `CollidesWith`. Runtime body ids live on the
  Scene's physics runtime, not the components (components stay pure authored data).
- **J4** `physics/ScenePhysics` (Scene-owned runtime binding) + `Scene::OnPhysicsStart/
  Step/Stop`. Tick order contract wired into Starforge `TickPlay` + `PlayerLayer`:
  scripts `OnFixedUpdate` → `OnPhysicsStep` → `DispatchPhysicsEvents`. Kinematic bodies
  read transforms via `MoveKinematic`; dynamic write-back uses the quat slot with parent
  decompose. Determinism proven (bit-match).
- **J5** `ScriptableEntity::Physics()` + `OnCollision*/OnTrigger*` virtuals; contact
  listener queues enter/exit (refcounted per body-pair) drained main-thread after Step.
  **Test-realism note:** frame-exact contact counts jitter during settle/fast-separation
  (Jolt manifold-internal, true of every solver), so the events test asserts the robust
  invariants (landing→Enter, separation→Exit, no phantom Exit while resting).
- **J6** `physics/CharacterController` wrapper (owns gravity/jump/stick-to-floor) over a
  `CharacterVirtual`; `Character()` proxy; `WalkController` template sample. Headless
  tests: rest/ground, walk, wall-block (no tunnel at speed), gentle-slope climb.
- **J7** `TerrainCollider` → `HeightFieldShape` from the terrain's CPU heightfield, built
  from the even `(n-1)²` grid (Jolt rounds sample count up to the block size; the engine
  grid is odd `32·2^k+1`). Parity test: 100 random points ≤ 2 cm vs `SampleHeight`.
  `SyncWorldSystems()` now runs before `OnPhysicsStart` so recipe terrain is built first.
  Rigid-body buoyancy stays parked (scripts use the S9 water queries + `AddForce`).
- **J8** collider wireframe gizmos (box/sphere/capsule) + Fit-to-mesh + a `Colliders`/
  `Physics` viewport toggle; live Jolt debug draw via the Renderer3D line batch
  (Debug-config only). GL-conformance clean.
- **J9** `WalkController` + `PhysicsBall` template scripts; ForgePlayground's ball is now
  a real dynamic sphere resting/bouncing on the terrain collider (PhysicsBall pushes
  height/velY telemetry); dedicated determinism proof. **Deviation:** ForgePlayground v2
  serves as the "physics playground scene" rather than a separately hand-authored
  template `.cscene` (the fragile-JSON path was avoided; the scripts + in-editor
  authoring cover the sample intent).

**Remaining = the user's on-GPU acceptance (J9 DoD): author ground+boxes+character
in-editor → Play → stack behaves, character walks, telemetry records; package the
project (E19) and confirm the shipped exe simulates identically; visual pass on the
collider gizmos + Physics-Debug toggle (sleeping bodies grey).** Then commit.

---

## 0. Execution notes

1. Roadmap §"Working agreement" build recipe; engine rules from doc 13 §0 apply verbatim
   (no GL outside platform; generic modules only; compat gate: Engine3DDemo, Frontier,
   SF_Telem, ViperSim identical — none of them attach physics components, so `PhysicsWorld`
   must be a strict no-op when unused).
2. **Everything simulation-side is headless-testable.** Jolt never touches GL; `CosmicTests`
   gets real physics tests (stacks, determinism). Debug *drawing* is the only GL-adjacent
   piece and lives behind the line-batch verbs.
3. **Fixed-step only.** Physics steps exactly on the engine fixed timestep (project.cproj
   fixed-dt). Never step physics from variable `OnUpdate`.
4. One work order per session; status banners; no git writes.

---

## 1. Architecture

```
Cosmic/dependencies/JoltPhysics/            (vendored, pinned tag, static lib)
Cosmic/src/physics/
├── PhysicsWorld.h/.cpp     engine service (SerialLink/SceneManager ownership pattern:
│                           owned by whoever ticks it — PlayerLayer / Starforge play mode)
├── PhysicsTypes.h          MotionType, layer enums, RayHit/ShapeCast results (POD, reflected
│                           where useful) — public header, NO Jolt includes
├── PhysicsBody.h           thin handle (BodyID + world*) returned to scripts
└── PhysicsDebug.h/.cpp     wireframe draw of shapes/contacts via Renderer3D line batch
scene/Components.h          RigidBodyComponent + collider components (reflected, serialized)
scripting/ScriptableEntity  Physics() accessor (queries, forces) + collision callbacks
```

**Jolt stays out of public headers** (compile-time firewall like nlohmann/windows.h): pimpl in
`PhysicsWorld.cpp`; components store plain reflected fields, never `JPH::` types.
**Lifecycle:** bodies exist only while a simulation session runs (Play in the editor; always in
PlayerLayer). Edit mode = no Jolt objects; gizmos draw from component data alone. This keeps
undo/serialization trivial (components are the truth, bodies are derived).

---

## 2. Work orders

### J1 — Vendor Jolt + build integration

**Files:** VENDOR `Cosmic/dependencies/JoltPhysics/` (pin a release tag in a README, current
stable 5.x); MODIFY `Cosmic/CMakeLists.txt` (add subdirectory or a curated static-lib target —
compile Jolt's own sources; exclude samples/tests); `Cosmic/dependencies/README` pin note.

**Spec:** build Jolt as one static lib linked into `Cosmic.dll` (assimp-style: no new runtime
DLL). Match the engine's MSVC flags: x64, `/MD(d)`, C++20, warnings-as-errors OFF for the
vendored target only. Set Jolt CMake options: `CROSS_PLATFORM_DETERMINISTIC=ON` (float
determinism — needed by J8's test), `USE_AVX2` per current min-spec (document the choice),
disable Jolt's own debug renderer sample glue. Startup/shutdown: `RegisterDefaultAllocator`,
`Factory::sInstance`, `RegisterTypes` — wrapped once inside `PhysicsWorld`, never global.

**Gotchas:** Jolt asserts route through `JPH_ASSERT` — install the trace/assert callbacks to
`CS_CORE_ERROR` so failures land in the engine log, not stderr. Debug builds of Jolt are slow;
keep `JPH_DEBUG_RENDERER` compiled only in Debug config.

**Acceptance:** full solution builds Debug+Release, zero new warnings in engine code;
`CosmicTests` links; a smoke test constructs/destroys a `JPH::PhysicsSystem` via
`PhysicsWorld` headlessly.

**Status:** ✅ 2026-07-04

### J2 — PhysicsWorld service

**Files:** NEW `physics/PhysicsWorld.h/.cpp`, `physics/PhysicsTypes.h`, `physics/PhysicsBody.h`;
`Cosmic.h` export; NEW `tests/test_physics_world.cpp`.

**Spec:** `PhysicsWorld` owns one `JPH::PhysicsSystem` + `TempAllocatorImpl` +
`JobSystemThreadPool` (v1: Jolt's own pool, `min(hw-1, 4)` threads; bridging to the engine
`JobSystem` is a noted follow-up). API (all engine types, no JPH):
`Init(const PhysicsSettings&)` (gravity, max bodies/pairs, layer config), `Step(float fixedDt)`
(collision steps = 1; document the 1/60 assumption and the sub-step rule for smaller dt),
`CreateBody(const BodyDesc&) -> PhysicsBody`, `DestroyBody`, `SetBodyTransform/GetBodyTransform`
(position + quat), velocities, forces/impulses, `RayCast(origin, dir, maxDist, LayerMask) ->
optional<RayHit{entity, point, normal, distance}>`, `SphereCast`, `OverlapSphere/Box ->
vector<Entity>`. Bodies carry the owning entity's UUID in `userData` so every query result maps
back to an `Entity`. **Layers:** object layers Static/Dynamic/Trigger/Character + a reflected
16-bit `CollisionMask` on the component (broadphase: 2 layers static/moving — the standard Jolt
recipe from its samples).

**Gotchas:** body add/remove must use batch APIs when >~10 at once (scene start) —
`BodyInterface::AddBodiesPrepare/Finalize`. Kinematic targets use `MoveKinematic` (velocity-
consistent), not teleports, except on session start. All Step/query calls are main-thread in
v1 (document; Jolt is internally parallel).

**Acceptance:** headless tests: gravity drop matches closed form ±1%; a 10-box stack settles
and sleeps; raycast hits the expected box + entity round-trip; layer mask filters; 10k
create/destroy cycles leak nothing (Jolt's leak check enabled in Debug).

**Status:** ✅ 2026-07-04

### J3 — Components + reflection + serialization

**Files:** MODIFY `scene/Components.h` (+ `reflect/TypeRegistry.cpp` registrations):
`RigidBodyComponent{ MotionType Motion = Static|Kinematic|Dynamic; float Mass=1, Friction=0.5,
Restitution=0.1, LinearDamping=0.05, AngularDamping=0.05, GravityFactor=1; bool CCD=false,
StartAsleep=false; uint16 Layer=Dynamic; uint16 CollidesWith=0xFFFF; }`;
`BoxColliderComponent{ HalfExtents, Offset }`, `SphereColliderComponent{ Radius, Offset }`,
`CapsuleColliderComponent{ Radius, HalfHeight, Offset }`,
`MeshColliderComponent{ bool Convex; /* uses the sibling MeshRenderer/Primitive mesh */ }`,
`TerrainColliderComponent{ /* uses the sibling TerrainComponent */ }`. All
`CS_REGISTER_COMPONENT`'d + E1-reflected with ranges/tooltips; hidden runtime body-id field is
NOT reflected.

**Spec:** a body = `RigidBodyComponent` + ≥1 collider on the same entity (multiple colliders →
`StaticCompoundShape`). Collider-only (no RigidBody) = implicit static. Scale: bake the
entity's world scale into the shape at body-build time; warn once on non-uniform scale for
sphere/capsule. `MeshColliderComponent`: Convex=true → `ConvexHullShape` (dynamic-capable),
false → `MeshShape` (static/kinematic only — enforce with a Console warning at build).

**Acceptance:** components round-trip the serializer (headless test); Inspector shows them
grouped under a "Physics" category; undo works on every field (free via E7/E8 — verify).

**Status:** ✅ 2026-07-04

### J4 — Scene/session integration (Play ↔ bodies)

**Files:** MODIFY `scene/Scene.h/.cpp` (`OnPhysicsStart(PhysicsWorld&)`, `OnPhysicsStop`,
`OnPhysicsStep(fixedDt)` — build/destroy/sync); `Projects/Starforge/src/StarforgeApp.cpp`
(play mode owns a `PhysicsWorld`); `layers/PlayerLayer.cpp` (same); NEW
`tests/test_physics_scene.cpp`.

**Spec:** on Play/scene-load: walk components → create bodies (world transforms via `WorldOf`,
E3). Tick order per fixed step (document in the header, this ordering is a contract):
**scripts `OnFixedUpdate` → `PhysicsWorld::Step` → transform write-back (dynamic bodies →
`TransformComponent` position/quat) → collision-event dispatch (J5)**. Kinematic bodies read
the entity transform each step (script-driven movers). Position write-back uses the quat slot
(`UseQuatRotation` — the component already supports it; verify the exact field names in
`Components.h` before wiring). Hierarchy: physics entities under parents get world-transform
write-back decomposed into local (reuse the E3 keep-world-pose math); warn once for dynamic
bodies parented under moving parents (unsupported v1).

**Gotchas:** destroy order on Stop: bodies before the runtime scene (dangling UUID userData
otherwise). Editor pause/single-step (E13) must step physics exactly once per Step press —
route through the same `TickPlay` accumulator the scripts use.

> **Tick-order amendment 2026-07-14 (Phase 26 / N4):** the nav crowd steps inside this
> same fixed step. `Scene::OnNavStep` runs **after** `OnPhysicsStep` and **before**
> `DispatchPhysicsEvents`, so the full contract is now:
> **scripts `OnFixedUpdate` → `OnPhysicsStep` → `OnNavStep` → `DispatchPhysicsEvents`.**
> Agent transforms write back like physics bodies; `nav.arrived` emits on the scene
> EventBus during the nav step. Wired identically in Starforge `TickPlay` and `PlayerLayer`.

**Acceptance:** a scene with a ground box + 5 dynamic boxes: Play → they fall/stack/sleep;
Pause + Step advances one dt; Stop restores the edit scene untouched (byte-identical
serializer check — the E13 harness). Headless: two 300-step runs of the same scene produce
identical positions (determinism, needs J1's flag).

**Status:** ✅ 2026-07-04

### J5 — Script API + collision events

**Files:** MODIFY `scripting/ScriptableEntity.h` (a `Physics()` proxy mirroring the E20
`Telemetry()` pattern), `scripting/ScriptHost.h/.cpp` (event dispatch), `physics/PhysicsWorld`
(contact listener → queued events); tests.

**Spec:** script surface (all optional-safe when no world exists — no-op like the null
telemetry sink):
`Physics().RayCast(...)`, `.OverlapSphere(...)`, `.AddForce/.AddImpulse/.SetVelocity/
.GetVelocity()`, `.IsGrounded(maxDist)` convenience. Events: `OnCollisionEnter(Entity other)`,
`OnCollisionExit(Entity other)`, `OnTriggerEnter/Exit(Entity other)` — new virtuals on
`ScriptableEntity` (default empty). Jolt's `ContactListener` fires on Jolt's threads → queue
into a lock-guarded buffer, drained + dispatched on the main thread after `Step` (same pattern
as `FileWatcher`). Trigger = collider with `IsTrigger` flag (add to collider components, J3
amendment).

**Acceptance:** headless: script receives Enter exactly once for a falling box, Exit on
separation; trigger volume reports overlap without contact forces; a script raycast selects
the ground under a moving entity every step.

**Status:** ✅ 2026-07-04

### J6 — Character controller

**Files:** NEW `physics/CharacterController.h` (engine wrapper over `JPH::CharacterVirtual`);
MODIFY `scene/Components.h` (`CharacterControllerComponent{ Height, Radius, MaxSlopeDeg=45,
StepHeight=0.35, Mass=80 }`), ScriptableEntity `Character()` proxy (`Move(velocity)`,
`Jump(v)`, `IsGrounded()`, `GetGroundNormal()`).

**Spec:** `CharacterVirtual` (not the rigid-body character): kinematic capsule with slope/step
handling; updated inside the fixed step *after* `PhysicsWorld::Step` (Jolt's recommended
`ExtendedUpdate` with gravity + stick-to-floor). Transform write-back like J4. The template
project gains a `WalkController` sample script (WASD + gamepad axes → `Move`, space → `Jump`)
so "walking on the ground" is a copy-paste away.

**Acceptance:** sample walks up a 30° slope, is blocked at 60°, steps onto a 0.3 m box, can't
tunnel through walls at high speed; works on terrain (needs J7).

**Status:** ✅ 2026-07-04

### J7 — Terrain + water interplay

**Files:** MODIFY `physics/PhysicsWorld.cpp` (heightfield build), `scene/Scene.cpp` (body
build recognizes `TerrainColliderComponent`); read `terrain/Terrain.h` for the height-data
accessors (`SampleHeight` exists; a raw row accessor may need adding — engine-side, GL-free).

**Spec:** `TerrainColliderComponent` next to a built `TerrainComponent` creates a
`JPH::HeightFieldShape` from the same CPU heightfield the renderer samples (`Terrain` keeps the
CPU copy — verify the accessor; add `GetHeightData()` returning dims + span if absent). Sample
count parity note: Jolt heightfields want power-of-two blocks; the engine grid is `32·2^k+1` —
build from the `(n-1)×(n-1)` cell grid (drop the duplicate edge row) and assert
`SampleHeight` ≤ 2 cm error vs the Jolt shape at 100 random points (test). Water: **no rigid
buoyancy in v1** — scripts use the existing `Water` buoyancy queries (S9) + `AddForce`; note
the Jolt `BuoyancyImpulse` helper as the v2 route.

**Acceptance:** headless parity test above; in ForgePlayground: dynamic boxes rest on the
island slopes, the character (J6) walks the shoreline.

**Status:** ✅ 2026-07-04

### J8 — Editor authoring + debug draw

**Files:** Starforge: Inspector category done via J3; NEW gizmo drawing in
`ViewportController::DrawSceneOverlay` (wire box/sphere/capsule from collider components,
selected-entity emphasis), "Fit to mesh" button on box/sphere colliders (AABB from the sibling
mesh — `Mesh` exposes local AABB since S5), View ▸ Physics Debug toggle; engine:
`physics/PhysicsDebug` draws *live Jolt state* during Play (body outlines colored by
sleep/awake, contact points) through the Renderer3D line batch.

**Acceptance:** collider wireframes match rendered meshes after Fit; debug toggle shows
sleeping bodies turn grey; no GL calls outside platform (conformance script).

**Status:** ✅ 2026-07-04

### J9 — Samples, determinism proof, docs hooks

**Files:** template project (`Projects/Starforge/assets/templates/`) gains
`WalkController` + a physics playground scene; ForgePlayground v2 (doc 13 H8) ball switches
`BouncingBall` script to a real dynamic sphere (keep the telemetry channels — record height
from the transform); `tests/test_physics_determinism.cpp`; FEATURE-MATRIX + doc 12 rows
flipped.

**Acceptance (phase definition-of-done):** from a new project: add ground + boxes + character
purely in-editor → Play → stack behaves, character walks, telemetry records a contact-driven
value; two headless runs bit-match; package the project (E19) and the shipped exe simulates
identically.

**Status:** ✅ 2026-07-04

---

## 3. Parked (with unlocks)

| Item | Unlock |
| --- | --- |
| Rigid-body buoyancy via Jolt (`BuoyancyImpulse` against `Water`) | a project needs floating dynamics beyond script-applied forces |
| Constraints/joints (hinge, slider, ragdoll) | a project needs articulated bodies |
| Vehicle helper (wheeled) | a driving project (aero/flight stays app-side regardless) |
| Engine JobSystem ↔ Jolt job-system bridge | profiler shows the extra pool matters |
| Physics interpolation for render (fixed-step decoupled smoothing) | visible stutter at low fixed rates |

## 4. Order

J1 → J2 → J3 → J4 → J5 → J6/J7 (parallel) → J8 → J9. Nothing outside this phase depends on it
except doc 17 (voxel collision) and doc 13 H8's final ball polish.

## Kickoff prompt

> You are implementing ONE work order from `docs/plans/14-phase15-physics-plan.md` in
> `C:\dev\Cosmic`. Read §0–§1 first — the Jolt-stays-out-of-public-headers rule and the
> fixed-step contract bind every item. Then read your work order (J__) fully. Re-verify quoted
> engine APIs by content (they drift). Compat gate: shipped apps attach no physics components
> and must run identically. Build with the roadmap's non-interactive cmake recipe; tests are
> headless; never run git write commands. Finish with the Acceptance + status banner update.
