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
assets open simultaneously without cross-talk. **Status:** ☐

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
after three `setVar`/increments; old `.cflow` files load unchanged. **Status:** ☐

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
a Once option walks every path correctly; signal emissions observed on the bus. **Status:** ☐

### Q4 — Story Graph editor document *(gap §9.3; deps Q1, Q3, M1)*
**Files:** NEW Starforge `editors/StoryEditor.h/.cpp`; `AssetTypes.h` row (`.cstory`).
**Spec:** Q1 canvas with rich nodes (portrait/background thumbnails via T11 slots, text
preview, option pins with condition badges + Once toggles); right-side edit-node panel (title,
speaker, asset slots, text, options list with per-option guard editor); left Q2 variables
panel; toolbar **Play preview** (runs the Q3 runner in-panel with clickable options).
**Acceptance:** author the Q3 acceptance story entirely in the editor, save/reload identical,
preview walks it; undo works on node/option edits. **Status:** ☐

### Q5 — Vignette (+ post params) *(gap §9.4a)*
**Files:** engine `renderer/PostProcessStack` (vignette folded into the tonemap pass like
height fog: amount/radius/feather/color uniforms), `scene/Components.h`
(`EnvironmentComponent` fields, default off), Starforge Environment panel rows (auto-UI).
**Spec:** default-off ⇒ byte-identical output (compat + conformance). Optional stretch:
chromatic aberration behind the same gate — only if trivially co-located. **Acceptance:**
toggle compares identical-vs-vignetted; conformance script green; Engine3DDemo toggle (house
pattern). **Status:** ☐

### Q6 — Post-chain graph view *(gap §9.4b; deps Q1)*
**Files:** NEW Starforge `editors/PostChainEditor.h/.cpp` (an M1 document, or a panel).
**Spec:** renders the FIXED pipeline as read-only-topology nodes — Scene → SSAO → Bloom →
God-Rays → Tonemap(fog/vignette/haze) → FXAA — each node showing its enable checkbox + params
bound to the same reflected `EnvironmentComponent`/`SceneRendererSettings` fields (undo free
via `CommitFieldEdit`). No re-wiring, no custom passes — the 2214-style mental model over the
verified frame shape. **Acceptance:** edits through the graph match Environment-panel edits
exactly (same undo entries); pipeline topology always reflects the real chain. **Status:** ☐

## Kickoff prompt

> You are implementing ONE work order from `docs/plans/24-phase25-graphs-story-plan.md` in
> `C:\dev\Cosmic`. Read §0, your item, its cited § in
> `docs/design/example-images-gap-analysis.md`, and `scene/FlowMachine.h` (the runtime
> pattern of record). Graph runtimes GL-free + headless-tested; `.cflow`/`.cstory` versioned;
> no arbitrary post-graph executor; Starforge naming only. Roadmap cmake recipe; compat gate;
> no git writes. Finish with Acceptance + status banner.
