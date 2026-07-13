# Phase 24 Plan — Skeletal Animation Editors & Multi-Material Meshes

> **Created 2026-07-11.** Editor-vision phase 3 of 7 (spec of record:
> [`../design/example-images-gap-analysis.md`](../design/example-images-gap-analysis.md) §8 +
> §5.5). Doc 19 **A2 remains the runtime spec** (skins/clips/GPU skinning/`AnimatorComponent`) —
> its unlock FIRED 2026-07-11 (the Phase 28 flagship is the character project). This phase
> builds the editor superstructure the reference screenshots show: a document-style **Starforge
> Animation Editor** (skeleton tree, bone-overlay preview, sockets, timeline), the reusable
> widgets underneath it, and the multi-material mesh upgrade.
>
> **Depends on:** doc 19 **A1** (assimp ON) and **A2** (skeletal runtime) for M3/M4/M6; doc 19
> **A4/PreviewRig** for the editor viewport; M1→M3/M4 consumers; M2 is independent (C6 sequencer
> reuses it later). M5 pairs with A1 but is otherwise independent.

> **STATUS — ✅ code-complete 2026-07-12 (UNcommitted).** All six work orders (M1–M6) landed in
> one session; each item's ✅ banner below carries its per-item detail. Engine surface stayed
> generic + compat-gated (single-material + non-skinned scenes byte-identical). Build Debug+Release
> **zero warnings**, `CosmicTests` **314/314** (301→314), GL-conformance clean. Runtime stays doc 19
> A2; the full controller graph stays **parked** (M6 restates it). Remaining = the user's on-GPU
> acceptance list (rigged character opens in the Animation Editor + scrub, socket a prop, script
> crossfade walk→run, 2-material model both slots, 50-instance ≥ 60 fps) + commit.

---

## 0. Execution notes

Roadmap build recipe; doc 13 §0 engine rules; compat gate (no shipped app has skins — every
render-path change must keep non-skinned scenes byte-identical); headless-test all sampling/
pose math (the A2 pattern); skinning GPU state via `BindingPoints.h` claims. Asset types
register in Phase 23's T5 table (skeleton/clip badges). No git writes.

## 1. Work orders

### M1 — Asset-editor host framework *(gap §8.1)*
**Files:** NEW Starforge `editors/AssetEditorHost.h/.cpp` + `editors/IAssetEditor.h`
(`Path()/Title()/Dirty()/Save()/OnUpdate/OnImGuiRender`); `StarforgeApp.cpp` (dock a tabbed
"Editors" document area); `AssetTypes.h` gains an `open-in-editor` action column.
**Spec:** multi-document tab bar; each editor may own an interactive offscreen viewport (the
A4 `PreviewRig` in interactive mode: per-document FBO + orbit input + state-restore contract);
dirty-dot on tabs, close-with-save prompt, one editor instance per asset path (re-focus on
re-open). First consumer is M3; Phase 25 (graphs/story) and later editors ride the same host.
**Acceptance:** open/close/re-open documents with correct focus + save prompts; scene viewport
renders identically with three documents open (no GL leak). **Status:** ✅ 2026-07-12 — NEW
`editors/IAssetEditor.h` (`Path/Title/Icon/Dirty/Save/OnUpdate/OnImGuiRender`) + `editors/
AssetEditorHost.h/.cpp`: the dockable **"Editors"** window draws a tab bar of open documents with
per-tab dirty dots (`ImGuiTabItemFlags_UnsavedDocument`), `SetSelected` re-focus, and a
Save/Discard/Cancel modal when a dirty tab's ✕ is clicked; `Open(path, factory, showFlag)` enforces
one instance per path (re-opening re-focuses). Wired into the shell: `m_Editors` member,
`OnUpdate` ticks every open doc, auto-shown while `AnyOpen()`, a "Animation Editor" View-menu
toggle + a new **"Animation"** layout preset docking Editors + Content Browser. `AssetTypes` gained
the **open-in-editor action column** (`AssetOpen::AnimationEditor`); rigged formats (glTF/GLB/FBX/DAE)
route a double-click / "Open in Animation Editor" context item to `EditorContext::PendingOpenAnimEditor`,
which the shell turns into an `AnimationEditor` document. The no-GL-leak guarantee is the A4 §0.5
state-restore contract (each editor's interactive `PreviewRig` re-binds the FBO + resets render
state every pass). Debug build 0 warnings, `CosmicTests` 301/301. Remaining (user, on-GPU): open
three docs, confirm the scene viewport is byte-identical + focus/save-prompt behaviour.

### M2 — Reusable timeline widget *(gap §8.2 part)*
**Files:** NEW Starforge `widgets/Timeline.h/.cpp`.
**Spec:** pure-ImGui widget: time ruler with zoom/pan, transport (play/pause/stop/loop), scrub
head, N labeled tracks, per-track tick marks (keyframes) with hover info, optional snap. NO
editing semantics in v1 (display + scrub only) — key *editing* arrives with C6 (sequencer),
which reuses this widget. **Acceptance:** headless-ish: widget drives a fake clock correctly
(loop wrap, scrub-while-paused); used by M3. **Status:** ✅ 2026-07-12 — NEW `widgets/Timeline.h/.cpp`.
The transport/clock is a **pure struct** `TimelineState` (GL-free, ImGui-free math): `Advance(dt)`
loop-wraps via `fmod` into `[0,Duration)` (negative Speed wraps too) or clamps+stops when non-loop;
`Scrub(t)` clamps (the paused re-pose / head drag); `Normalized`/`SetNormalized`/`Stop`/`Pause`/`Play`.
`Timeline::Draw(id, state, tracks, opts)` renders a zoom/pan time ruler (wheel zooms around the
cursor, middle/right-drag pans), a transport row (play/pause/stop/loop + time readout), a draggable
scrub head (left-drag anywhere over the lane, optional key/grid snap), and N labelled tracks with
per-key diamond ticks + hover tooltips. **DISPLAY + SCRUB ONLY** — no key editing (C6 adds that and
reuses this widget verbatim). The pure transport math is verified by trace (wrap 1.8+0.5·s@dur2→0.3;
negative wrap; non-loop clamp→stop; paused Advance is a no-op) and exercised live by M3. 0 warnings.

### M3 — Starforge Animation Editor document *(gap §8.2; deps A1, A2, M1, M2, A4)*
**Files:** NEW Starforge `editors/AnimationEditor.h/.cpp`.
**Spec:** opens a skinned model asset (or an entity's `AnimatorComponent` context): left
skeleton tree (joint hierarchy from A2's `Skeleton`), center interactive preview (PreviewRig:
model + key light + IBL) with **bone overlay** (joint points + parent lines via
`Renderer3D::DrawLine`) and click-to-select-joint (nearest projected joint) + gizmo on the
selected joint (inspect-only in v1 — no authoring of joint transforms), right details (joint
name/index/bind pose; M4's socket section), bottom M2 timeline listing the model's clips with
key ticks, Play/Pause/Loop + scrub driving A2's sampling. **Acceptance:** the A2 acceptance rig
(e.g. the glTF Fox) opens, plays, scrubs; selecting `hand.l`-style joints highlights the bone
chain; 0 warnings, no GL leak. **Status:** ✅ 2026-07-12 — NEW `editors/AnimationEditor.h/.cpp`
(`IAssetEditor`). Loads the merged skinned `Mesh` (`AssetLibrary::GetMesh`) + its clip set
(`GetAnimationClipNames`/`GetAnimationClip("path#clip")`); **left** skeleton tree from
`Skeleton::Joints` (children adjacency, click-select, `hand.l`-style names), **center** interactive
preview via a per-document `PreviewRig::RenderSkeletal` (NEW): draws the skinned mesh at the sampled
pose (`Clip::Sample`→`ComputePalette`) then a **bone overlay ON TOP** (depth-off line flush) —
joint crosses + parent lines from baked-space `ImportCorrection·global` joint transforms, the
selected joint's inbound bone highlighted + an inspect-only axis tripod (no joint authoring);
click-to-select projects joints via `PreviewRig::ProjectPoint` (nearest within 12 px); orbit/zoom/
reset match the Material Editor. **Right** details = joint name/index/parent/bind-local/posed
position (+ the M4 socket section). **Bottom** = clip combo + the M2 timeline built from the active
clip's channels (one track per animated joint, union of pos/rot/scl key times), transport + scrub
writing `TimelineState::Time` into `Clip::Sample`. Static/skinless models show a "no skeleton" note;
no-clip rigs preview the bind pose. Debug 0 warnings, `CosmicTests` 301/301. Remaining (user,
on-GPU): open the Fox, play Survey/Walk/Run, scrub, select `hand`-chain joints → bone highlight.

### M4 — Joint sockets / attachments *(gap §8.3; deps A2)*
**Files:** engine `scene/Components.h` (NEW reflected
`SocketComponent{ std::string Joint; glm::vec3 Position; glm::quat Rotation; glm::vec3 Scale }`),
`scene/Scene.cpp` (`GetWorldTransform` composes ancestor-world × current joint pose × socket
offset when an ancestor animates — A2's pose palette is the source), serializer row; Starforge:
socket add/remove UI in M3's details pane (pick joint from the tree, gizmo-edit the offset),
Inspector auto-UI covers the fields.
**Spec:** an entity with a `SocketComponent` whose parent chain has an Animator follows the
joint every frame (render, physics attach points, scripts all see the composed transform).
Compat: entities without the component untouched. **Acceptance:** headless: composed transform
matches hand-computed pose at t=0/mid; on-GPU: a sword entity socketed to `hand.r` tracks a
walk clip with no lag/drift; undo works on socket edits. **Status:** ✅ 2026-07-12 — engine
`scene/Components.h` gained the reflected **`SocketComponent{ std::string Joint; glm::vec3 Position;
glm::quat Rotation; glm::vec3 Scale }`** (registered "Socket"/"Rendering") + `AnimatorComponent`
runtime `ScratchGlobals`/`JointModelMatrices`. `Scene::UpdateAnimators` now publishes
`JointModelMatrices[j] = ImportCorrection · global_j` (baked-space joint FRAMES — no inverse-bind,
so a child placed there sits ON the joint) every frame from the clip pose, or from the **bind pose
when no clip is resolved** (sockets track a clip-less rig too); the DRAW path still only reads
`Palette`, which stays empty without a clip → the pre-M4 static bind-pose draw is byte-identical.
`Scene::WorldOf` gained a socket branch: an entity with a `SocketComponent` walks its ancestor
chain for the nearest `AnimatorComponent` whose skeleton has the named joint and returns
`WorldOf(animator) · JointModelMatrices[joint] · (T·R·S offset)`; unresolved (no animating
ancestor / unknown joint) falls through to the ordinary parent-relative transform (compat). The
serializer + Inspector are FREE via reflection (all fields are ordinary kinds — no special-casing;
add via Add Component ▸ Socket, edit Position/Rotation/Scale with per-field undo). Headless
`tests/test_sockets.cpp` (**+6**): composition at t=0/mid/end vs hand-computed world positions, a
rotation+scale offset, the bind-pose fallback, no-animator + unknown-joint fall-through (compat),
and a save/round-trip. `CosmicTests` **307/307**, Debug 0 warnings. *Deviation:* the socket
authoring UI is the reflected Inspector (add/remove/edit-with-undo) + M3's "copy joint name" helper
rather than a bespoke add-button/offset-gizmo in the Animation Editor — the reflection dividend
covers add/remove/edit undoably, and no scene entity is bound to the asset-document preview.
Remaining (user, on-GPU): socket a sword to a Fox `hand`-joint in a scene → Play → it tracks.

### M5 — Material slots (multi-material meshes) *(gap §5.5; pairs with A1)*
**Files:** engine `graphics/Mesh.h/.cpp` (submesh ranges + material indices — `Model`/
`MeshImport` already know per-part materials at load; keep them through `MeshData`),
`scene/Components.h` (`MeshRendererComponent` gains `std::vector<std::string> MaterialPaths`;
legacy single `MaterialPath` stays authoritative when the vector is empty),
`renderer/Renderer3D` (`DrawMesh` submits per-submesh queue entries so sort/cull/instancing
still apply), `scene/SceneSerializer` (read/write both forms), `Scene::SyncPrimitiveMeshes`
(resolve slot paths); Starforge Inspector renders a "Materials" list using T11 asset slots.
**Spec:** compat is the hard requirement — empty vector ⇒ today's behavior byte-identical
(conformance + screenshot compare); multi-mesh *hierarchy* import (E16) remains valid, this
adds the single-entity multi-slot alternative for skinned/complex models. **Acceptance:** an
imported two-material model renders both slots on ONE entity; per-slot override via Inspector
is undoable; old scenes byte-identical; S12 stats show per-submesh entries batching correctly.
**Status:** ✅ 2026-07-12 — engine `graphics/Mesh` gained a **`Submesh{ IndexOffset, IndexCount,
MaterialIndex }`** table (`MeshData::Submeshes` → `Mesh::m_Submeshes`; `GetSubmeshes`/`HasSubmeshes`/
`GetMaterialSlotCount`); `MeshImport::Import`'s merge loop populates one range per part with its
`MaterialIndex` (dropped when < 2 parts, so single-material meshes carry NO table → byte-identical).
`RendererAPI::DrawIndexed` grew a defaulted `indexOffset` (GL `glDrawElements` byte offset; 0 ⇒
identical); `Renderer3D::DrawMesh` grew defaulted `indexOffset`/`indexCount`, `MeshDrawCmd` carries
the range, `ExecuteSingle` draws `IndexCount ? IndexCount : GetIndexCount()` from `IndexOffset` (0/0
⇒ the pre-M5 whole-mesh call), and ranged draws are marked non-instancable. `MeshRendererComponent`
gained `std::vector<std::string> MaterialPaths` + resolved `MaterialAssets` (NOT reflected — the
serializer special-cases the `"MaterialPaths"` array, written only when non-empty; `SyncPrimitiveMeshes`
resolves it). `Scene::SubmitOpaqueMeshes` submits one lit queue entry per submesh (via NEW
`SceneDrawContext::DrawMeshRange`, Main/Reflection only) when `MaterialAssets` is set; **depth/shadow
passes draw one whole-mesh caster** (materials don't affect depth → byte-identical to a single-material
caster); a slot with no material falls back to the legacy material/colour for that range. Skinned
meshes stay on the single-material skinned path (multi-material skinned deferred). Starforge: a
`Commands::SetMaterialSlot` (undoable; captures the vector; trailing-empty trim keeps all-empty absent)
+ an Inspector **"Materials"** list (drag a `.cmat` per slot, ✕ to clear) shown when the mesh has ≥ 2
slots. Headless `tests/test_material_slots.cpp` (**+3**): the compat gate (empty ⇒ NO `MaterialPaths`
key), the non-empty round-trip (incl. an empty middle slot), the `MeshData` submesh table; import
`MaterialIndex` flow already covered by test_meshimport. `CosmicTests` **310/310**, Debug 0 warnings.
Byte-identical is by construction (empty vector ⇒ unchanged code path + `DrawIndexed(count,0)` ⇒ same
GL call). Remaining (user, on-GPU): drop a 2-material model on a MeshRenderer's MeshPath, assign two
`.cmat`s → both slots render on ONE entity; undo a slot; screenshot-compare an old single-material scene.

### M6 — Animator crossfade tier *(unlock fired by the flagship; full graph stays parked)*
**Files:** engine A2's `AnimatorComponent` (+`CrossfadeTo(clip, seconds)` on the script proxy,
a second sampled pose + lerp in the pose build), Starforge Inspector (current/next clip + fade
readout).
**Spec:** the FEATURE-MATRIX "blend trees / state machines" row stays **parked** — this is the
minimal tier a playable character needs: script-driven clip switching with a timed crossfade
(idle↔walk↔run). No graph asset, no editor. Revisit the full controller graph only after the
flagship ships (its editor would ride Phase 25's canvas). **Acceptance:** headless: pose during
a 0.3 s fade is the expected lerp at t=0/half/end; on-GPU: walk→run blends without pops; 50
instances still ≥60 fps (A2's bar). **Status:** ✅ 2026-07-12 — engine `AnimationClip` gained a
pure static **`BlendLocals(a, b, w, out)`** (per-joint TRS decompose → translation/scale mix +
shortest-arc slerp → recompose; alias-safe). `AnimatorComponent` gained crossfade runtime state
(`NextClipPath`/`NextClipRef`/`NextTimeSeconds`/`FadeDuration`/`FadeElapsed`/`ScratchLocalsB`) + a
header **`CrossfadeTo(clip, seconds)`** (seconds ≤ 0 or same-clip = hard switch / cancel; only sets
intent). `Scene::UpdateAnimators` resolves the target (guarded, like ClipPath), advances BOTH heads
+ the fade while playing, **pose-blends the two sampled LOCALS** at `FadeElapsed/FadeDuration` (the
M4 joint frames + sockets then follow the blended pose), and **PROMOTES** the next clip to current
when the fade completes. Script surface: NEW `ScriptableEntity::AnimatorProxy` — `Animator().
CrossfadeTo/Play/SetPlaying/IsCrossfading/CurrentClip` on this entity's Animator. Starforge Inspector
shows a **"Crossfading → <clip>" progress bar** during a fade (read-only — no graph authoring). The
FEATURE-MATRIX "blend trees / state machines" row STAYS PARKED (this is the minimal script-driven
tier). Headless `tests/test_crossfade.cpp` (**+4**): `BlendLocals` translation lerp + rotation slerp,
the end-to-end 0.3 s fade (pose at t=0/half/end + promotion), hard-switch/cancel semantics, and a
paused animator not advancing the fade. `CosmicTests` **314/314**, Debug 0 warnings. Remaining (user,
on-GPU): a script `CrossfadeTo` walk→run blends without pops; 50 instances ≥ 60 fps.

## Kickoff prompt

> You are implementing ONE work order from `docs/plans/23-phase24-animation-editors-plan.md`
> in `C:\dev\Cosmic`. Read §0, your item, its cited § in
> `docs/design/example-images-gap-analysis.md`, and doc 19 A2/A4 where referenced (they are
> the runtime/rig specs of record). Pose/sampling math headless-tested; GPU state via
> BindingPoints; non-skinned scenes byte-identical (compat gate); Starforge naming only.
> Roadmap cmake recipe; no git writes. Finish with Acceptance + status banner.
