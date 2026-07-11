# Phase 20 Plan — Asset Pipeline Completion & Animation

> **Created 2026-07-04.** Gathers the content-pipeline work left open by Phase 13: the gated
> assimp backend, skeletal animation (doc 05 S14 row), the CAD/modeling parks (STEP, CSG,
> terrain brushes — doc 11 §9 P1/P2/P5), the material/asset UX debt (E10/E17 deviations),
> prefab overrides v2 (E14), and the shipping pak (P6). Items are independent unless marked;
> **A1 is the phase's anchor** (several later items assume assimp is on).
>
> **Depends on:** Phase 14 H6 (file dialogs) for the authoring UX items; nothing else.

---

## 0. Execution notes

Roadmap build recipe; doc 13 §0 engine rules; compat gate. Import/geometry/animation math is
headless-tested; GL uploads main-thread. The `.cmeta` sidecar (E16) is the settings truth for
every importer — extend it, never invent a second sidecar.

## 1. Work orders

### A1 — assimp backend ON *(origin: E16 "seam shipped, backend gated behind COSMIC_WITH_ASSIMP")*
**Files:** VENDOR `Cosmic/dependencies/assimp` (pin release; importers trimmed to
FBX/OBJ/STL/DAE/PLY; exporters/tests OFF), engine CMake flips `COSMIC_WITH_ASSIMP` default ON;
the numbered how-to already sits at the top of `assets/MeshImport.cpp` — follow it.
**Spec:** the backend code exists — this order is vendoring + verification: FBX (cm×0.01) and
STL (mm×0.001) land at correct world size; multi-mesh sources become parent + child
MeshRenderers (E3 hierarchy — the E16 spec line, verify it's implemented under the `#ifdef`,
finish if stubbed); FBX/OBJ materials map to generated `.cmat` files with textures copied
alongside (same — verify/finish). Import dialog drops its "needs the assimp backend" warning.
**Acceptance (the original E16 line):** a Blender FBX + glTF of the same object at identical
world size; a SolidWorks STL (mm) at correct meters; `.cmeta` scale edit + reimport updates
placed entities; CosmicTests' gated assimp cases enabled and green. **Status:** ☐

### A2 — Skeletal animation *(origin: doc 05 S14 row "glTF skins/clips, GPU skinning")*
> **2026-07-11 (v4 roadmap): unlock FIRED** — the Phase 28 flagship (Forge Isle) is the
> character project. A2 stays the RUNTIME spec of record; the editor superstructure (document
> host, Animation Editor, sockets, crossfade tier) is Phase 24 (doc 23 M1–M6), which depends
> on this item. Schedule A1 → A4 → A2.

**Files:** engine `graphics/Model` (glTF skins/clips via cgltf; assimp FBX skins once A1
lands), NEW `graphics/Skeleton.h` + `AnimationClip.h` (pure sampling — headless-tested),
`scene/Components.h` `AnimatorComponent{ ClipPath, Speed, Loop, Playing, NormalizedTime }`
(reflected), skinning matrices → an SSBO (claim the binding in `BindingPoints.h`) consumed by
a `PBRSkinned.glsl` twin (the `SetInstancingShader`-twin registration pattern), Starforge:
clip picker + scrub in the Inspector, play preview in edit mode.
**Spec (v1):** one clip playing per Animator; sampling = keyframe lerp/slerp with a
fixed-rate bake option; **blend trees/state machines parked** (the flow doctrine: revisit
after a character project exists — FEATURE-MATRIX row). Casters: skinned meshes render into
shadow pass with skinning applied (twin shader for `ShadowDepth`).
**Acceptance:** a rigged glTF (e.g. Fox sample) plays correctly vs a reference viewer;
headless clip-sampling tests (t=0/mid/end, loop wrap); 50 instances ≥60 fps; scrub in editor.
**Status:** ☐

### A3 — STEP converter tool *(origin: doc 11 §9 P1)*
**Files:** NEW `tools/step2gltf/` — separate CMake exe on OpenCascade (OCCT), NOT linked into
the engine (the editor shells out, then A1's pipeline ingests the glTF).
**Spec:** `step2gltf in.step out.glb --linear-deflection 0.1 --angular-deflection 15` —
tessellation quality flags, assembly structure → glTF node hierarchy, units from the STEP
header, per-solid materials from OCCT color attributes. Starforge Import dialog recognizes
`.step/.stp` and runs the tool when present (config'd path; friendly "tool not built" message
otherwise). **Unlock (unchanged):** a real STEP-only workflow — build the tool skeleton only
when it fires. **Acceptance:** a reference STEP assembly imports with correct structure,
meters, and ≤0.1 mm deflection at default flags. **Status:** ☐

### A4 — Material & preview UX debt *(origin: E17 deviations)*
> **2026-07-11 (v4 roadmap): unlock FIRED; scope grows to the shared `PreviewRig` service**
> (gap analysis §14.3): batch-thumbnail mode (as specced below) PLUS an **interactive** mode
> (per-document FBO + orbit input) consumed by Phase 23 T7/T11 (browser preview, asset slots)
> and Phase 24 M1/M3 (asset-editor viewports). Same state-restore acceptance covers both
> modes. Size M → M/L; still one work order.

Material edits become undoable (route the panel through `CommitFieldEditFor` on a reflected
MaterialAsset copy — the env panel precedent); the offscreen **preview rig** (tiny FBO +
SceneRenderer-lite: one mesh, key light, IBL) renders a preview sphere in the Material panel
AND generates content-browser thumbnails for meshes/materials (E10's parked half); thumbnails
cache under `<project>/.starforge/thumbs/`. State-restore contract applies (doc 13 §0.5).
**Acceptance:** undo works on material edits; browser shows real thumbnails; no GL-state leak
(scene renders identically after a thumbnail pass — screenshot compare). **Status:** ☐

### A5 — In-place asset reload *(origin: E10 deviation "cache-slot refresh only")*
`AssetLibrary::Reload` re-uploads into the EXISTING GPU object where possible (`Texture2D`
re-upload in place) so held `Ref`s (materials) see edits live; meshes fall back to cache-slot
swap + a scene notify (`Scene::SyncPrimitiveMeshes` already re-resolves null/changed paths —
extend the guard to a reload counter). **Acceptance:** edit a texture in an external editor →
material on screen updates within a second without reassignment. **Status:** ☐

### A6 — Terrain sculpt & splat brushes *(origin: doc 11 §9 P5)*
RTT brush stamps into the packed height/normal texture + splat weights (raise/lower/smooth/
flatten/paint-layer), CPU heightfield kept in sync (SampleHeight parity test stays green —
this is the hard invariant), undo as brush-stroke tiles (E7 coalescing). Starforge: brush
panel (radius/strength/falloff), terrain-collider rebuild hook (doc 14 J7) on stroke end.
**Unlock:** param/recipe terrain stops being enough (a shipped-app polish pass).
**Acceptance:** sculpt a hill + paint a path, undo strokes, save→reload identical, physics
matches the new surface, ≤1 cm SampleHeight parity test green. **Status:** ☐

### A7 — Prefab overrides v2 *(origin: E14 "no per-field override tracking in v1")*
Field-level diff between instance and source (the E14 work order sketched the design):
overridden fields marked in the Inspector (bold + revert arrow), Apply pushes only diffs,
source edits propagate to non-overridden fields of open instances on load. **Unlock:** a
content-heavy project hits the apply/revert-whole-instance wall. **Acceptance:** override one
field of 3 instances → edit source → the other fields update, overrides survive;
apply-single-field works; serializer round-trip. **Status:** ☐

### A8 — CSG booleans *(origin: doc 11 §9 P2)*
Vendor `manifold` (MIT); Union/Subtract/Intersect on primitive/imported solids → baked
`MeshData` + a kept recipe component for re-edit (the E15 params pattern). Starforge:
Entity ▸ CSG menu on a two-solid selection. **Unlock:** in-editor modeling outgrows
primitives. **Acceptance:** subtract a cylinder from a box → watertight mesh (manifold check),
undoable, params re-editable, serializes by recipe. **Status:** ☐

### A9 — Binary asset pak *(origin: doc 11 §9 P6)*
Single-file pak + index for shipped apps (packager gains a "pack assets" toggle);
`FileSystem::Resolve` learns pak-mounted reads (loose files always win in dev). **Unlock:**
shipped-app size/IO matters (measure first — the E19 staging is loose-file today and fine).
**Acceptance:** packaged app runs entirely from the pak; load time ≤ loose files; a modified
loose file overrides the pak in dev mode. **Status:** ☐

## Kickoff prompt

> You are implementing ONE work order from `docs/plans/19-phase20-asset-animation-plan.md` in
> `C:\dev\Cosmic`. Read §0 and your item's origin citation (archived docs carry the original
> spec context — E-item deviations cite doc 11 in `docs/plans/archive/`). The `.cmeta` sidecar
> is the importer-settings truth; `MeshData` is the geometry interchange; headless-test the
> math; compat gate; roadmap cmake recipe; no git writes. Finish with Acceptance + status
> banner.
