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
renders identically with three documents open (no GL leak). **Status:** ☐

### M2 — Reusable timeline widget *(gap §8.2 part)*
**Files:** NEW Starforge `widgets/Timeline.h/.cpp`.
**Spec:** pure-ImGui widget: time ruler with zoom/pan, transport (play/pause/stop/loop), scrub
head, N labeled tracks, per-track tick marks (keyframes) with hover info, optional snap. NO
editing semantics in v1 (display + scrub only) — key *editing* arrives with C6 (sequencer),
which reuses this widget. **Acceptance:** headless-ish: widget drives a fake clock correctly
(loop wrap, scrub-while-paused); used by M3. **Status:** ☐

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
chain; 0 warnings, no GL leak. **Status:** ☐

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
walk clip with no lag/drift; undo works on socket edits. **Status:** ☐

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
**Status:** ☐

### M6 — Animator crossfade tier *(unlock fired by the flagship; full graph stays parked)*
**Files:** engine A2's `AnimatorComponent` (+`CrossfadeTo(clip, seconds)` on the script proxy,
a second sampled pose + lerp in the pose build), Starforge Inspector (current/next clip + fade
readout).
**Spec:** the FEATURE-MATRIX "blend trees / state machines" row stays **parked** — this is the
minimal tier a playable character needs: script-driven clip switching with a timed crossfade
(idle↔walk↔run). No graph asset, no editor. Revisit the full controller graph only after the
flagship ships (its editor would ride Phase 25's canvas). **Acceptance:** headless: pose during
a 0.3 s fade is the expected lerp at t=0/half/end; on-GPU: walk→run blends without pops; 50
instances still ≥60 fps (A2's bar). **Status:** ☐

## Kickoff prompt

> You are implementing ONE work order from `docs/plans/23-phase24-animation-editors-plan.md`
> in `C:\dev\Cosmic`. Read §0, your item, its cited § in
> `docs/design/example-images-gap-analysis.md`, and doc 19 A2/A4 where referenced (they are
> the runtime/rig specs of record). Pose/sampling math headless-tested; GPU state via
> BindingPoints; non-skinned scenes byte-identical (compat gate); Starforge naming only.
> Roadmap cmake recipe; no git writes. Finish with Acceptance + status banner.
