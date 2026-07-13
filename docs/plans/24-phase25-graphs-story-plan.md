# Phase 25 Plan — Node Graphs, Flow Variables & Story Tooling

> **Created 2026-07-11.** Editor-vision phase 4 of 7 (spec of record:
> [`../design/example-images-gap-analysis.md`](../design/example-images-gap-analysis.md) §9).
> Doc 16 **U6** stays the home of the `.cflow` node-graph panel and the imgui-node-editor
> vendoring — run U6 FIRST; this phase extracts its canvas into a reusable widget and builds
> the graph-shaped tooling the reference screenshots show: flow **variables**, the **Starforge
> Story Graph** (dialogue trees — Starforge-named per the 2026-07-11 naming rule; no borrowed
> "StoryFlow" branding), and the post-chain graph view (+ the missing vignette pass).
>
> **Depends on:** doc 16 U5 (shipped) + **U6** (vendor + flow panel) for Q1; Phase 24 **M1**
> (document host) for Q1/Q4/Q6; Q2→Q3→Q4 in order. Q5 is independent (any time).

> **STATUS — ✅ code-complete 2026-07-12 (UNcommitted).** All six work orders (Q1–Q6) landed in
> one session; each item's ✅ banner below carries its per-item detail. Graph runtimes are GL-free
> + headless-tested; `.cflow`/`.cstory` are versioned + forward-loadable (v1 `.cflow` loads
> unchanged). The arbitrary post-FX pass-graph executor stays **out of scope** (Q6 is a VIEW of the
> fixed chain — decision #13). Starforge naming only (the story tooling is the "Starforge Story
> Graph", never "StoryFlow"). Build Debug+Release **zero warnings**, `CosmicTests` **323/323**
> (314→323), GL-conformance clean. Remaining = the user's on-GPU acceptance (author + Play a
> branching guarded dialogue zero-code; two flows side by side; vignette A/B; graph-vs-panel undo
> parity) + commit.

---

## 0. Execution notes

Roadmap build recipe; doc 13 §0 engine rules; compat gate. Graph runtimes are GL-free and
headless-tested (the FlowMachine pattern — fake loader, hand-fed signals/time). `.cflow`/
`.cstory` JSON changes are versioned and forward-loadable. The **arbitrary post-FX pass-graph
executor is explicitly out of scope** (decision 2026-07-11, gap §9.4 — the fixed
`PostProcessStack` chain is the contract; Q6 is a graph *view* of it). No git writes.

## 1. Work orders

### Q1 — Reusable node canvas + graph documents *(gap §9.1; deps U6, M1)*
**Files:** NEW Starforge `widgets/NodeCanvas.h/.cpp` (extract/harden U6's panel internals:
node body builder, pin/link model, selection, context menus, mini-map, zoom/pan); the flow
editor becomes an M1 document (`editors/FlowEditor`) built on it.
**Spec:** canvas is graph-agnostic (callers supply node/pin descriptors + change callbacks);
flow editing keeps U6's behavior exactly; multiple flow documents open side by side via M1.
**Acceptance:** the U6 acceptance scenario passes unchanged through the new widget; two flow
assets open simultaneously without cross-talk. **Status:** ✅ 2026-07-12 — `widgets/NodeCanvas`
(already extracted at U6 — a graph-agnostic RAII `imgui-node-editor` wrapper: Begin/End,
`QueryEdits`, node placement, selection, `CenterOnContent`; native zoom/pan) is reused unchanged.
The flow editor became an **M1 document** `editors/FlowEditor` (`IAssetEditor`): the U6 panel's
logic ported verbatim (state nodes + start/overlay markers + red missing-scene/unreachable badges,
per-transition out-pins + "+ link" add-pin, @quit node, side inspector with scene picker + UiButton
signal event-picker + guard fields + onEnter actions, `Validate` problems, document-local
snapshot Undo/Redo, `EditorPos` persistence), minus the flow-picker/New (each document IS one
flow; the Content Browser creates + double-clicks `.cflow` open). Routing: `AssetTypes` `.cflow`
gained `Editor = AssetOpen::FlowEditor`; the Content Browser sets `EditorContext::PendingOpenDocument`
(double-click + "Open in Flow Editor"), the shell dispatches by extension to `m_Editors.Open(…
FlowEditor …)`. Two flows open as independent documents — each owns its own `FlowAsset` + its own
`NodeCanvas` (hence its own `ed::EditorContext`), so there is no cross-talk. The old singleton
`panels/FlowGraphPanel.*` + its shell/View-menu wiring were removed (the "Editors" host toggle now
covers all document types). Debug build 0 warnings, `CosmicTests` 314/314 (editor-only change).
Remaining (user, on-GPU): re-run the U5/U6 authored flow through the document + Play; open two
flows and confirm independent editing.

### Q2 — Flow variables (typed blackboard) *(gap §9.2)*
**Files:** engine `scene/FlowMachine.h/.cpp` (FlowAsset gains
`Variables{name → FlowValue default, group}`; `FlowValue` gains an Enum-of-strings kind;
runtime values on the machine; `FlowGuard` variant comparing a variable; actions gain
`setVar`; script access `Flow().GetVar/SetVar` beside the U2 signal API), `.cflow`
serializer (versioned).
**Spec:** guards/actions may reference variables anywhere a literal works today; missing
variable ⇒ guard false + one Console warning (the FlowGuard convention). Starforge: a
Variables side panel on the Q1 flow document (add/remove/group/defaults; drag a variable onto
a guard). **Acceptance:** headless: a two-state flow gated by `Score >= 3` transitions only
after three `setVar`/increments; old `.cflow` files load unchanged. **Status:** ✅ 2026-07-12 —
engine `scene/FlowMachine`: `FlowValue` gained a **`Enum`** kind (string-stored, compares like
String); NEW `FlowVariable{ Name, Group, FlowValue Default, EnumOptions }`; `FlowAsset::Variables`
(the blackboard); `FlowGuard` gained `Var` (non-empty ⇒ compare a variable, scene-free) and
`FlowAction` gained a `SetVar` type (`Var` + `VarAdd` add-vs-assign). The machine holds a runtime
`m_Vars` map seeded from the variable defaults on Start (cleared on Stop) + `GetVar/SetVar/HasVar`;
`EvalGuard` short-circuits to the variable path (missing var ⇒ false + one warning, the FlowGuard
convention); `RunActions` handles `SetVar`. `.cflow` is **versioned**: the writer emits v2 (+ the
`variables` array / guard `var` / `setVar` blocks) ONLY when a v2 feature is present, so
variable-free flows re-save byte-stable at v1 and old v1 files load unchanged. Script access:
`Scene` gained a non-owning `ActiveFlow()` pointer (the running FlowMachine points its top scene
there in Subscribe/UnsubscribeActiveBus), and `ScriptableEntity` gained a **`Flow()`** proxy
(`GetVar/SetVar` + typed `GetNumber/SetNumber/AddNumber/GetBool/SetBool/GetString/SetString`) beside
`Signals()`. Editor: the FlowEditor gained a **Variables side panel** (add/remove, group, per-type
default editors incl. enum options, each row a `FLOW_VAR` drag source) + guard authoring with a
Field/Variable source toggle (the variable field is a drop target) + `setVar` onEnter actions.
Headless `tests/test_flowmachine.cpp` (**+4**): the `Score >= 3` gate via a `setVar +=1` onEnter
(3 increments), the runtime SetVar/GetVar path + missing-var safety, old-v1-loads-unchanged
(no `variables`/`setVar`, stays v1), and a variables+enum+setVar+var-guard round-trip. `CosmicTests`
**318/318**, Debug 0 warnings. Remaining (user, on-GPU): author a variable + variable-guard in the
Flow Editor, Play it.

### Q3 — Starforge Story Graph runtime (`.cstory`) *(gap §9.3)*
**Files:** engine NEW `scene/StoryGraph.h/.cpp` (asset + runner, GL-free): nodes
`{ Speaker, Text, PortraitPath, BackgroundPath, AudioPath, Options[{Text, Guard(FlowGuard +
Q2 variables), Once, Next}] }`, Start/End, emit-signal actions on enter/exit; runner exposes
"current node + currently-valid options", `Choose(i)`, once-flags persisted per run;
serializer + `.cstory` JSON.
**Spec:** engine ships the *runner*, not presentation — a stock binding script (template) maps
current node → U1 UI entities (text/image/buttons) so a zero-code dialogue works, and any app
can render its own. Signals ride the U2 EventBus; variables ride Q2 (shared store when a story
runs under a flow). **Acceptance:** headless: a 5-node branching story with a guarded option +
a Once option walks every path correctly; signal emissions observed on the bus. **Status:** ✅
2026-07-12 — engine NEW `scene/StoryGraph.h/.cpp` (GL-free): `StoryOption{ Text, Next, Once,
HasGuard, FlowGuard }`, `StoryNode{ Name, Speaker, Text, Portrait/Background/AudioPath, OnEnter/
OnExit signals, Options, EditorPos }`, `StoryGraph{ Version, Start, Nodes, Variables }` + versioned
`.cstory` serializer (`cosmic_story`) + `Validate`. `StoryRunner`: `Start(graph, scene?, sharedVars?)`,
`Current()`, **`ValidOptions()`** (options whose guard passes AND, if `Once`, haven't been chosen —
rebuilt on each node enter), `Choose(validIndex)` (emits OnExit, marks Once, advances to Next /
`@end`), `Get/SetVar`; OnEnter/OnExit emit on the scene EventBus (U2). Guards reuse the shared
**`EvaluateFlowGuard`** (extracted from `FlowMachine::EvalGuard` — variable OR reflected-field), so a
story SHARES a FlowMachine's blackboard when run under a flow (story-only vars top up the shared
store). The stock zero-code binding ships as the template
`assets/templates/src/scripts/StoryUiBinding.h` — maps the current node onto Tag-addressed U1 UI
entities (text/speaker/portrait/background + up to 4 option buttons hidden by validity, `Signal`
rewritten to `story_choose_i`) and `Choose`s on the button signal. Headless `tests/test_story.cpp`
(**+4**): parse, the 5-node walk (guarded Pay + Once Secret + `secret_told`/`paid` bus signals +
`@end`), the Fight branch + round-trip, a story sharing a FlowMachine blackboard; plus the template
compile-smoke. `CosmicTests` **323/323**, Debug 0 warnings, GL-free. Remaining (user, on-GPU): rides
Q4's editor — author + Play the zero-code dialogue.

### Q4 — Story Graph editor document *(gap §9.3; deps Q1, Q3, M1)*
**Files:** NEW Starforge `editors/StoryEditor.h/.cpp`; `AssetTypes.h` row (`.cstory`).
**Spec:** Q1 canvas with rich nodes (portrait/background thumbnails via T11 slots, text
preview, option pins with condition badges + Once toggles); right-side edit-node panel (title,
speaker, asset slots, text, options list with per-option guard editor); left Q2 variables
panel; toolbar **Play preview** (runs the Q3 runner in-panel with clickable options).
**Acceptance:** author the Q3 acceptance story entirely in the editor, save/reload identical,
preview walks it; undo works on node/option edits. **Status:** ✅ 2026-07-12 — NEW Starforge
`editors/StoryEditor.h/.cpp` (`IAssetEditor`, opened via `.cstory` double-click / "Open in Story
Graph Editor" → `PendingOpenDocument`). Built on the Q1 `NodeCanvas`: **rich nodes** (start marker,
speaker, truncated text preview, one output pin per option with `[if]`/`[once]` badges, an
"+ option" add-pin) linked to target-node in-pins or the **@end** node; **edit-node panel** (name
rename w/ Next-retarget, speaker, multi-line text, portrait/background/audio asset slots w/
ASSET_PATH drop, OnEnter/OnExit signal lists, an Options list — text, Next combo, Once toggle,
per-option guard via the shared **`DrawFlowGuardFields`**), a **Q2 variables panel** (the shared
**`DrawFlowVariablesPanel`**), and a toolbar **Play preview** that runs the Q3 `StoryRunner` over a
throwaway scene with clickable valid-option buttons. Document-local JSON-snapshot Undo/Redo +
`Validate` badges. Shared widgets extracted to `widgets/VariablesPanel.h/.cpp` (blackboard + guard
editor) — the Flow editor was refactored onto them (dead `kOps` removed). `AssetTypes` `.cstory` row
+ **New ▸ Story** create (a default Start node → @end). Save/reload-identical + preview-walks rest on
the Q3-tested `StoryGraph` serializer + `StoryRunner`. `CosmicTests` **323/323**, Debug 0 warnings.
Remaining (user, on-GPU): author the branching guarded story in-editor, Play-preview every path,
undo a node/option edit.

### Q5 — Vignette (+ post params) *(gap §9.4a)*
**Files:** engine `renderer/PostProcessStack` (vignette folded into the tonemap pass like
height fog: amount/radius/feather/color uniforms), `scene/Components.h`
(`EnvironmentComponent` fields, default off), Starforge Environment panel rows (auto-UI).
**Spec:** default-off ⇒ byte-identical output (compat + conformance). Optional stretch:
chromatic aberration behind the same gate — only if trivially co-located. **Acceptance:**
toggle compares identical-vs-vignetted; conformance script green; Engine3DDemo toggle (house
pattern). **Status:** ✅ 2026-07-12 — **vignette folded into the tonemap pass** (`Tonemap.glsl`):
a post-tonemap edge-darkening block gated on `u_VignetteAmount > 0.0` (radius/feather smoothstep
toward `u_VignetteColor`). `PostProcessStack` gained `SetVignetteEnabled`/`SetVignetteParams` +
members; `Composite` uploads the uniforms, writing **amount 0 when disabled** so the shader skips
the block ⇒ **byte-identical** shipped output. `EnvironmentComponent` gained `Vignette` (default
OFF) + `VignetteAmount/Radius/Feather/Color` (reflected → the Environment panel auto-UIs them);
`SceneRendererSettings` carries them and `SceneRenderer::BuildRenderDesc`/apply wires env → settings
→ `m_Post.SetVignette*`. Engine3DDemo gained a **Vignette** checkbox + amount slider (the house
pattern). Chromatic aberration NOT added (the "only if trivially co-located" stretch — a vignette
is a screen-space multiply, CA needs per-channel offset sampling; deferred, noted). Debug 0 warnings,
`CosmicTests` **323/323**, **GL-conformance clean** (the static audit; shaders aren't scanned). Byte-
identical-when-off is by construction (amount 0 → shader skip). Remaining (user, on-GPU): the
identical-vs-vignetted screenshot compare + the Engine3DDemo toggle demo.

### Q6 — Post-chain graph view *(gap §9.4b; deps Q1)*
**Files:** NEW Starforge `editors/PostChainEditor.h/.cpp` (an M1 document, or a panel).
**Spec:** renders the FIXED pipeline as read-only-topology nodes — Scene → SSAO → Bloom →
God-Rays → Tonemap(fog/vignette/haze) → FXAA — each node showing its enable checkbox + params
bound to the same reflected `EnvironmentComponent`/`SceneRendererSettings` fields (undo free
via `CommitFieldEdit`). No re-wiring, no custom passes — the 2214-style mental model over the
verified frame shape. **Acceptance:** edits through the graph match Environment-panel edits
exactly (same undo entries); pipeline topology always reflects the real chain. **Status:** ✅
2026-07-12 — NEW Starforge `editors/PostChainEditor.h/.cpp` (a scene-bound PANEL, off by default,
View ▸ Post Chain). Draws the FIXED chain **Scene → SSAO → Bloom → God Rays → Tonemap(fog/vignette)
→ FXAA** on the Q1 `NodeCanvas` as **read-only-topology** nodes (positions set once, links drawn
1→2→…→6, `QueryEdits` drained but **never applied** — no re-wiring, decision #13). Each editable
node draws its `EnvironmentComponent` fields through `PropertyRows::DrawField` + the **exact
Environment-panel path** `Commands::CommitFieldEditFor(ctx, env, "Env " + field, desc->TypeId, field,
before, after)` (same find-or-create Environment entity, same capture-on-activate/commit-on-deactivate
idiom) → an edit here yields the **IDENTICAL undo entry** an Environment-panel edit would. SSAO/Bloom/
Tonemap(Exposure+Fog+Vignette)/FXAA bind real fields; God Rays is an informational node (an engine
pass not authored on `EnvironmentComponent`). No arbitrary pass-graph executor — a VIEW of the
verified chain. Debug 0 warnings, `CosmicTests` **323/323**, GL-conformance clean. Remaining (user,
on-GPU): edit a param via the graph vs the panel and confirm identical undo; confirm the topology
mirrors the chain.

## Kickoff prompt

> You are implementing ONE work order from `docs/plans/24-phase25-graphs-story-plan.md` in
> `C:\dev\Cosmic`. Read §0, your item, its cited § in
> `docs/design/example-images-gap-analysis.md`, and `scene/FlowMachine.h` (the runtime
> pattern of record). Graph runtimes GL-free + headless-tested; `.cflow`/`.cstory` versioned;
> no arbitrary post-graph executor; Starforge naming only. Roadmap cmake recipe; compat gate;
> no git writes. Finish with Acceptance + status banner.
