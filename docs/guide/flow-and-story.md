# Screen Flow & Dialogue — Guide

**What this covers:** the two data-driven graph runtimes and the channel that joins them — the
`.cflow` **screen flow** (states, transitions, guards, actions, `FlowMachine`, `Scene::ActiveFlow`,
the `Flow()` script proxy), the typed **flow-variable blackboard** (`FlowValue` /
`FlowVariable` — Bool, Number, String, Enum), the `.cstory` **branching-dialogue** runtime
(`StoryGraph` / `StoryNode` / `StoryOption` / `StoryRunner`, option guards, `Once`, signal
emissions), the stock `StoryUiBinding` script, and the per-scene **`EventBus`** that carries every
signal between UI buttons, flows, stories and scripts.
**Source of truth:** `Cosmic/src/scene/FlowMachine.{h,cpp}`, `scene/StoryGraph.{h,cpp}`,
`scene/EventBus.{h,cpp}`, `scene/Scene.h` (`Events()` / `ActiveFlow()`),
`scripting/ScriptableEntity.h` (`FlowProxy`, `SignalProxy`), `scripting/ScriptHost.cpp`,
`layers/PlayerLayer.cpp`, `Projects/Starforge/src/StarforgeApp.cpp` (`BuildFlowDemo`, flow-driven
Play), `Projects/Starforge/src/ProjectManifest.h`, `Projects/ForgeIsle/flows/Main.cflow`,
`Projects/Starforge/assets/templates/src/scripts/StoryUiBinding.h`, `tests/test_flowmachine.cpp`,
`tests/test_story.cpp`
**API Reference:** none. `scene/FlowMachine.h`, `scene/StoryGraph.h` and `scene/EventBus.h` have
**no row in the reference manifest** — the gap D50 found for `SceneSerializer`/`SceneManager`/
`CommandStack` and D52 found for the whole `scene/ui/` tier. **This chapter is the client-facing
source for all three headers** until D5 closes the manifest.
**How it works:** no explainer covers these runtimes either; the nearest is
[`../systems/ecs-scene.md`](../systems/ecs-scene.md) *(skeleton — D26)*.
**Configuration:** **both.** All three headers are shared, unfenced source — nothing in
`Cosmic/CMakeLists.txt`'s 2D filter list touches them
([`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md)). A 2D game and a 3D game get
identical flow and dialogue surfaces.

> **Three separate things, one channel.** A **flow** decides *which screen you are on*. A **story**
> decides *which line of dialogue you are on*. The **EventBus** is the wire both listen to. You can
> use any one without the other two: a flow with no stories, a story with no flow, or bare signals
> with neither.

Nothing here costs anything until you use it. `Scene::Events()` starts with zero subscribers, and
`EventBus::Emit` on a scene nobody subscribed to walks two empty containers and returns.

---

## Quick start

A two-screen app — menu, game, and a pause overlay — with **no C++ at all**. Author two scenes with
UI buttons that emit signals ([`game-ui.md`](game-ui.md)), drop this file at
`project://flows/Main.cflow`, and point the project manifest at it.

```json
{
  "cosmic_flow": 1,
  "start": "MainMenu",
  "states": [
    { "name": "MainMenu", "scene": "project://scenes/MainMenu.cscene",
      "transitions": [
        { "on": "play_clicked", "to": "Game" },
        { "on": "quit_clicked", "to": "@quit" } ] },

    { "name": "Game", "scene": "project://scenes/Game.cscene",
      "transitions": [
        { "on": "key:Escape", "to": "Pause", "push": true } ] },

    { "name": "Pause", "scene": "project://scenes/Pause.cscene", "overlay": true,
      "transitions": [
        { "on": "resume_clicked", "to": "@pop" },
        { "on": "quit_clicked",   "to": "@quit" } ] }
  ]
}
```

In `project.cproj` (TOML):

```toml
startup_scene = "scenes/MainMenu.cscene"   # fallback if the flow key is removed
startup_flow  = "flows/Main.cflow"         # project-relative, NO scheme prefix
```

That is **FlowDemo**, the engine's zero-code sample. It is not a folder in `Projects/` — like
ForgePong it is a project **generated in code**, by `StarforgeApp::BuildFlowDemo`
(`StarforgeApp.cpp:2852`, homescreen ▸ *Flow Sample*), which is where every snippet below that
mentions FlowDemo comes from.

Four things this is quietly asserting:

- **A button's `Signal` string is the whole API.** `UiSystem::Update` emits it on the scene's
  `EventBus` (`UiSystem.cpp:242`); the running `FlowMachine` has a `ConnectAny` listener on that
  bus, so the string lands in its pending queue and a matching transition fires. No registration,
  no binding table.
- **`Cosmic.h` does not aggregate these headers.** `scene/FlowMachine.h` and `scene/StoryGraph.h`
  are **not** in the umbrella header — any C++ file naming `FlowAsset`, `FlowMachine`, `StoryGraph`
  or `StoryRunner` needs an explicit `#include`. (`scene/Scene.h` does pull in `EventBus.h`, so
  `Signals()`/`Events()` work out of the box.)
- **`key:Escape` is the only keyboard transition the shipped hosts feed.** See
  [Fire a transition](#fire-a-transition-signals-keys-and-timers) — the format is general, the
  bridge is not.
- **Scene paths are VFS paths and are resolved.** `FlowAsset::Load` runs the *file* path through
  `FileSystem::Resolve` (`FlowMachine.cpp:312`), and each state's `"scene"` goes to your loader,
  which resolves it. This is not the `SceneManager` trap D50 found.

---

## The `.cflow` file format

`FlowAsset::LoadFromString` is the only parser (`FlowMachine.cpp:90`) and it is forgiving: every key
is optional, unknown keys are ignored, and a malformed action entry is skipped rather than failing
the load. Only invalid **JSON** returns `false`.

| Key | Where | Type | Meaning |
| --- | --- | --- | --- |
| `cosmic_flow` | root | int | Format version. Defaults to `1` when absent. |
| `start` | root | string | Name of the state to enter on `Start()`. |
| `states` | root | array | The screens. |
| `variables` | root | array | The typed blackboard (v2). See [Keep state in flow variables](#keep-state-in-flow-variables). |
| `name` | state | string | Unique state name. |
| `scene` | state | string | VFS path handed to your `SceneLoader`. **Empty ⇒ reuse the current scene.** |
| `overlay` | state | bool | Author/editor hint. **The runtime never reads it** — `push` on the transition is what stacks. |
| `onEnter` | state | array | Actions run every time the state is entered. |
| `transitions` | state | array | Outgoing edges. |
| `editor` | state | `{ "pos": [x, y] }` | Flow-Editor node layout. Round-trips; runtime-inert. |
| `on` | transition | string | A signal name, `key:<Name>`, or `timer:<seconds>`. |
| `to` | transition | string | A state name, or the builtin `@quit` / `@pop`. |
| `push` | transition | bool | Push a stack frame instead of replacing the stack. |
| `transition` | transition | string | Visual hint (`"None"` / `"Fade"`). **The engine ignores it**; a host may honour it. Omitted on save when `"None"`. |
| `if` | transition | object | A guard. See [Guard a transition](#guard-a-transition). |

`onEnter` entries are one of three shapes, discriminated by their single key:

```json
{ "emit":     "score_reset" }
{ "setVar":   { "var": "Score", "add": true, "value": 1 } }
{ "setField": { "entity": "HUD", "component": "Tag", "field": "Tag", "value": "Round 2" } }
```

### Version 2, and why your file may not become one

`SaveToString` writes `2` **only if the document actually uses a v2 feature** — a `variables`
block, a `setVar` action, or a variable guard (`FlowMachine.cpp:217-224`). A variable-free flow
re-saves at its original version with no `variables` key, so v1 files stay byte-stable through an
editor round trip. Loading is version-blind: the parser reads whatever keys are present.

`SaveToString` pretty-prints with `dump(2)`, the same convention as `.cmat` and `SaveToString` on
scenes.

### Validate before you ship

`FlowAsset::Validate()` returns a list of human-readable problems; **empty means valid**. It checks
a missing or dangling `start`, unnamed states, duplicate state names, transitions with no target,
unknown `@builtin` targets, and transitions to states that do not exist. It does **not** check that
a state's scene file exists on disk — that needs the VFS and is the editor's job (the Flow Editor
badges missing scenes in red).

```cpp
#include "scene/FlowMachine.h"

Cosmic::FlowAsset flow;
std::string err;
if (!Cosmic::FlowAsset::Load(flow, "project://flows/Main.cflow", &err))
    CS_ERROR("flow load failed: {0}", err);          // returns false, logs, err is filled

for (const std::string& problem : flow.Validate())
    CS_WARN("flow: {0}", problem);
```

---

## Run a flow

There are exactly three ways, and they are all the same `FlowMachine` underneath.

**1 — A shipped app.** Set `startup_flow` in the manifest. `PlayerLayer::OnAttach` loads it, and the
flow then **owns scene selection** for the whole session: the start state's scene becomes the active
scene and `startup_scene` is never consulted (`PlayerLayer.cpp:117-144`). If the flow fails to load
or produces no scene, the layer logs an error and falls back to the single-scene path.

**2 — Editor Play.** When the manifest names a `startup_flow`, Starforge's Play button shows a
**Flow** checkbox (`StarforgeApp.cpp:2168`). With it on, Play boots the flow exactly as the shipped
player does — with one editor convenience: the state whose scene path matches the **open** scene
loads from the live in-memory snapshot, so unsaved edits play as you see them
(`StarforgeApp.cpp:627-645`).

**3 — Your own host.** The machine is GL-free, has no engine dependencies beyond `Scene`, and is
headless-testable. You supply the loader:

```cpp
#include "scene/FlowMachine.h"

Cosmic::FlowMachine m_Flow;

// once
m_Flow.SetSceneLoader([this](const std::string& path) -> Cosmic::Ref<Cosmic::Scene>
{
    Cosmic::Ref<Cosmic::Scene> s = Cosmic::Scene::Create();
    if (!Cosmic::SceneSerializer::Load(*s, Cosmic::FileSystem::Resolve(path)))
        return nullptr;                       // null ⇒ keep the current scene, and warn
    return s;
});
Cosmic::FlowAsset asset;
if (Cosmic::FlowAsset::Load(asset, "project://flows/Main.cflow"))
    m_Flow.Start(asset);

// every frame
m_Flow.OnUpdate(ts);
if (m_Flow.QuitRequested()) { /* leave the app / return to the launcher */ }
if (Cosmic::Ref<Cosmic::Scene> s = m_Flow.ActiveScene(); s && s != m_CurrentScene)
{
    m_CurrentScene = s;
    RebindScripts();                          // see below — the machine does none of this
}
```

**The `FlowMachine` swaps scenes and nothing else.** It does not instantiate scripts, build physics
bodies, bind navigation, or tick anything. Every host duplicates the rebind:
`PlayerLayer::RebindScripts` (`PlayerLayer.cpp:181`) is the reference implementation — destroy the
old scene's script instances, `OnPhysicsStop` the old scene, adopt the new one, `Instantiate`,
`OnPhysicsStart`. Same owner-ticked pattern as `SceneManager`, `ScriptHost` and `PhysicsWorld`.

`Start()` calls `Stop()` first, so starting a second flow on the same machine is safe and total.
The destructor calls `Stop()` too — which matters, because `Stop()` is what unsubscribes the
machine's listener from the scene's `EventBus`. Stop the flow **before** the scenes it points at
tear down; `PlayerLayer::OnDetach` does this on its first line (`PlayerLayer.cpp:166`).

---

## Fire a transition: signals, keys and timers

`FeedSignal` **queues**; nothing is applied until the next `OnUpdate`. That is deliberate — it makes
dispatch deterministic and re-entrancy-safe, so a transition can never fire in the middle of a UI
hit-test or a script tick.

`OnUpdate(dt)` does three things in order:

1. adds `dt` to the time-in-current-state counter;
2. drains the pending queue front-to-back, firing **at most one** transition per signal (the first
   whose `on` matches *and* whose guard passes; `TryFireSignal` returns after the first hit);
3. if still running, evaluates timers — **at most one timer transition per update**.

The drain loop is a cascade: an `onEnter` action that emits lands back on the queue and is processed
in the same `OnUpdate`. A guard counter trips at 100 000 iterations, clears the queue and logs
`"FlowMachine: signal cascade guard tripped (possible cycle)"` — that is what an authored
ping-pong between two states looks like.

### Plain signals

Any string. Three producers, all equivalent from the flow's point of view:

| Producer | Call |
| --- | --- |
| A UI button | `UiButtonComponent::Signal` — emitted on release-inside |
| A script | `Signals().Emit("boss_defeated")` |
| The flow itself | an `onEnter` `{ "emit": … }` action |

All three go onto the **scene** bus, which the machine listens to via `ConnectAny`. A signal emitted
on a scene the flow is *not* currently showing never reaches it.

### `key:<Name>`

The transition string is compared literally — nothing in the engine parses `key:` or reads the
keyboard on the flow's behalf. **Both shipped hosts bridge exactly one key:**

```cpp
// PlayerLayer.cpp:237 and StarforgeApp.cpp:768 — the same three lines.
const bool esc = Input::IsKeyPressed(CS_KEY_ESCAPE);
if (esc && !m_PrevEscape) m_Flow.FeedSignal("key:Escape");   // rising edge only
m_PrevEscape = esc;
```

So `key:Escape` works out of the box and **`key:Space`, `key:Tab`, `key:F1` do nothing** in a
shipped app or in editor Play. To add one, bridge it yourself in your own layer — the name is
whatever you type on both sides, and the edge-detect is yours to write. Feeding it without the edge
guard queues a signal every frame the key is held.

Note that the flow is advanced **even while the application is paused**: `PlayerLayer::OnUpdate`
runs the flow block outside its `if (!app.IsPaused())` gate, so a pause overlay authored as a flow
state still responds to Escape.

### `timer:<seconds>`

`m_Elapsed` counts seconds since the current **top** state was entered, and resets on every enter
and on every `@pop`. The seconds value is parsed with `strtof` off the character after `timer:`, so
`"timer:2.5"` is fine and `"timer:"` reads as 0 (fires on the first update). A splash screen:

```json
{ "name": "Splash", "scene": "project://scenes/Splash.cscene",
  "transitions": [ { "on": "timer:2.5", "to": "MainMenu" } ] }
```

Timers are checked after the signal drain, so a signal that arrives on the same frame the timer
expires wins.

---

## Guard a transition

A guard makes an edge conditional. `if` is present ⇒ the transition only fires when the guard is
true. Both flavours share one evaluator — the exported free function
`EvaluateFlowGuard(guard, scene, lookupVar, warn)` — so a flow transition and a story option
behave identically.

**Variable guard** (`var` is non-empty). Compares a blackboard variable; needs no scene:

```json
{ "on": "leave", "to": "Exit", "if": { "var": "Score", "op": ">=", "value": 3 } }
```

**Field guard** (everything else). Finds the *first* entity whose `TagComponent::Tag` matches
`entity`, looks up `component` in the reflection registry, reads `field`, and compares:

```json
{ "on": "continue", "to": "Game",
  "if": { "entity": "SaveSlot", "component": "Camera", "field": "Primary",
          "op": "==", "value": true } }
```

`component` is the **reflected** name, not the C++ type — `"Camera"`, `"Tag"`, `"Transform"`, not
`"CameraComponent"`. Field names are likewise the registered ones (`Tag.Tag`,
`Transform.Position`).

Operators: `==`, `!=`, `<`, `>`, `<=`, `>=`. Ordering operators are **numeric only** — comparing
strings or bools with `<` returns false rather than doing something clever. Numeric equality uses a
`1e-6` tolerance. An `Enum` value compares exactly like a `String` (the option text).

**Every failure is `false`, never an error.** A missing variable, a missing entity, an unknown
component, an entity without that component, an unknown field, a type mismatch — all evaluate to
false and the transition simply does not fire. Flow guards log one `CS_CORE_WARN` per distinct
reason; the dedup set is never cleared, so you get exactly one line per problem per machine.

Two shapes to know about:

- **Field guards do not understand `Enum`-kind values.** `CompareValue` (`FlowMachine.cpp:393`)
  handles `Bool`, `Number` and `String` and falls through to `false` for `Kind::Enum`. This never
  bites a file-authored guard — the JSON parser only ever produces Bool/Number/String — but a guard
  built in C++ with `FlowValue::MakeEnum` against a *field* silently never matches. Use
  `MakeString` for field guards.
- A field guard needs an **active scene**. A scene-less state (one with an empty `"scene"` and
  nothing beneath it on the stack) makes every field guard false.

---

## Keep state in flow variables

The blackboard is a typed, flat map that lives on the running `FlowMachine`. Declare variables on
the asset; the machine seeds them from their defaults on `Start()`.

```json
"variables": [
  { "name": "MetHermit",  "group": "Quest", "type": "bool",   "default": false },
  { "name": "BeaconsLit", "group": "Quest", "type": "number", "default": 0.0 },
  { "name": "Mood",       "type": "enum", "default": "Calm", "options": ["Calm", "Angry"] }
]
```

*(Those first two are verbatim from `Projects/ForgeIsle/flows/Main.cflow` — the only `.cflow` that
ships as a real file in the tree.)*

| `type` | `FlowValue::Kind` | Stored in | Notes |
| --- | --- | --- | --- |
| `bool` | `Bool` | `Bool` | The default when `type` is missing or unrecognised |
| `number` | `Number` | `Number` | `double`; there is no integer type |
| `string` | `String` | `String` | |
| `enum` | `Enum` | `String` | `options` lists the legal strings; defaults to the first option when `default` is absent. Compares as a string. |

`group` is a UI label the editor's Variables panel groups by; the runtime ignores it.

### Read and write from a script

`Scene::ActiveFlow()` is how a script reaches the machine. A running `FlowMachine` points its **top
scene** at itself on every enter (`SubscribeActiveBus` → `SetActiveFlow(this)`), and clears the
pointer when it moves on or stops. The `Flow()` proxy on `ScriptableEntity` wraps that with typed
helpers:

```cpp
class Beacon : public Cosmic::ScriptableEntity
{
protected:
    void OnStart() override
    {
        Flow().AddNumber("BeaconsLit", 1.0);               // read-modify-write
        if (Flow().GetNumber("BeaconsLit") >= 3.0)
            Flow().SetBool("MetHermit", true);
        Signals().Emit("beacon_lit");                      // let the flow/UI react too
    }
};
```

The full proxy: `GetVar`/`SetVar` (raw `FlowValue`), `GetNumber`/`SetNumber`/`AddNumber`,
`GetBool`/`SetBool`, `GetString`/`SetString`. **With no flow running every getter returns a
default-constructed value and every setter is a no-op** — `Flow().GetNumber("x")` on a scene with
no flow is `0.0`, not a crash. That means a script cannot tell "no flow" from "variable is zero";
if you need to know, check `GetScene().ActiveFlow() != nullptr`.

A `SystemScript` has no `Flow()` proxy (it is scene-bound, not entity-bound). Reach the machine
directly: `GetScene().ActiveFlow()`.

### Three rules that catch people

- **`SetVar` creates on write.** Setting a name that was never declared adds it, so scripts can keep
  ad-hoc scratch state. The flip side: a typo makes a *new* variable instead of an error, and the
  guard reading the correctly-spelled name stays false.
- **`GetVar` on an unknown name returns `FlowValue::MakeBool(false)`** — a `Bool`, whatever type you
  expected. `GetNumber` on it is `0.0`, `GetString` is `""`.
- **The blackboard is per-run.** `Stop()` clears it, and `@quit` calls `Stop()`. Nothing persists
  variables across runs; if you want a save file, write one yourself.

### `setVar` as a state action

```json
"onEnter": [ { "setVar": { "var": "Score", "add": true, "value": 1 } } ]
```

`"add": true` does `current.Number + value.Number` and stores the result as a **Number**, whatever
the variable's declared type was. `"add": false` (or omitted) assigns `value` verbatim. Because
`onEnter` runs on *every* entry, a self-looping state is the idiomatic counter — that is exactly
what `tests/test_flowmachine.cpp:230` asserts.

---

## Do something on entering a screen

Beyond `emit` and `setVar`, `setField` writes a reflected field on a tagged entity in the active
scene:

```json
{ "setField": { "entity": "HUD", "component": "Tag", "field": "Tag", "value": "Round 2" } }
```

The value is coerced to the field's registered kind: `Bool` for bools; `Number` truncated to
`int32_t` / `uint32_t` / `uint64_t` (an `EntityRef`) / `float` / an enum's integer; `String` for
`String` and `AssetPath`. **Vectors, colours and quaternions are not supported** — a `setField` at a
`Vec3` field logs `"flow setField: field '…' has an unsupported kind"` and does nothing. Every other
failure (no tagged entity, unknown component, missing component, unknown field) logs a warning and
continues; unlike guards, these are **not** deduped, so a `setField` on a state you re-enter often
will fill the log.

Order matters inside `Enter`: the scene is resolved, the bus subscription is (re)made, and *then*
`onEnter` runs. So an `emit` action lands on the new scene's bus, is seen by the machine's own
listener, and is re-queued — a state can emit a signal that immediately transitions it onward.

---

## Push an overlay (pause menus)

The machine keeps a **stack of frames**, each `{ state name, active scene }`. `CurrentState()`,
`ActiveScene()` and the timer all read the top.

- `"push": true` on a transition **adds** a frame.
- `"to": "@pop"` removes the top frame — and **warns and does nothing when only one frame remains**
  (you cannot pop the base state).
- A transition **without** `push` clears the whole stack and starts a new one-frame stack. Leaving a
  pause overlay by transitioning to `MainMenu` therefore discards the game frame under it; only
  `@pop` returns to what was there.
- `"to": "@quit"` sets `QuitRequested()` and stops the machine outright.

A pushed state with an **empty** `"scene"` keeps the scene beneath it active — the "same-scene UI
canvas toggle" overlay, where the pause menu is a canvas already in the game scene. A pushed state
*with* a scene loads its own; FlowDemo's `Pause` state does that, and ForgeIsle's does too.

**v1 renders the top scene only.** The host draws whatever `ActiveScene()` returns, so a pause
screen with its own scene hides the game behind it rather than dimming it. Additive rendering of
the under-scene is a recorded follow-up, not shipped. If you want the game visible behind a pause
menu, author the overlay as a canvas in the game scene and give the pushed state an empty `scene`.

`StackDepth()` tells you where you are; `CurrentState()` returns `""` when the stack is empty.

---

## Signals: the EventBus

One `EventBus` per `Scene`, reached with `Scene::Events()`. Signals are plain strings carrying an
optional source `Entity`.

```cpp
// Subscribe to one name; keep the handle.
EventBus::Handle h = scene->Events().Connect("boss_defeated",
    [](Cosmic::Entity source) { /* … */ });

// Subscribe to everything (this is what FlowMachine and ScriptHost use).
EventBus::Handle any = scene->Events().ConnectAny(
    [](const std::string& sig, Cosmic::Entity source) { /* … */ });

scene->Events().Emit("boss_defeated", Cosmic::Entity());   // source may be invalid
scene->Events().Disconnect(h);                             // no-op on an unknown handle
```

From a script, prefer the proxies — `Signals().Emit(...)`, `Signals().Connect(...)`,
`Signals().Disconnect(h)` — which stamp the emitting entity as the source automatically. For a
catch-all, override `OnSignal`: `ScriptHost` wires a single `ConnectAny` at `Instantiate` and fans
every signal out to every live script's `OnSignal` (`ScriptHost.cpp:140`).

The dispatch contract, straight out of `EventBus::Emit`:

- **Named handlers fire first, in subscription order; then every any-handler.** There is no priority
  or ordering control beyond that.
- **Both lists are snapshotted before dispatch, and each entry is liveness-checked before it runs.**
  So disconnecting inside a handler reliably prevents that listener from firing, and connecting
  inside a handler does **not** deliver the in-flight signal to the new listener.
- **Handle `0` is invalid.** `Connect`/`ConnectAny` return `0` if you hand them an empty
  `std::function`.
- Everything is synchronous, same-frame, main-thread. `Emit` returns after every handler has run.

Two facts about scope that cause more confusion than anything else:

- **The bus belongs to the scene, not the app.** A scene swap gives you a brand-new, empty bus.
  Every handle you were holding is dead, and every subscription must be re-made — which is why
  `ScriptHost::Instantiate` re-subscribes and the `FlowMachine` re-subscribes on every enter.
- **A `SystemScript` never receives `OnSignal`.** `ScriptHost::DispatchSignal` walks
  `NativeScriptComponent` instances only. A system that wants signals must call
  `GetScene().Events().Connect(...)` itself in `OnCreate` and disconnect in `OnDestroy`.

`ListenerCount(signal)` and `TotalListeners()` exist for diagnostics; `Clear()` drops everything.

---

## Branching dialogue with `.cstory`

The same shape as a flow, one level down: nodes instead of states, options instead of transitions,
and a `StoryRunner` you drive by choosing rather than by feeding signals. **The engine ships the
runner, not the presentation** — nothing in `StoryGraph.cpp` touches GL or ImGui.

```json
{
  "cosmic_story": 1,
  "start": "Intro",
  "variables": [ { "name": "Gold", "type": "number", "default": 5 } ],
  "nodes": [
    { "name": "Intro", "speaker": "Guard", "text": "Halt.",
      "portrait": "project://art/guard.png",
      "options": [
        { "text": "Pay 10 gold",    "next": "Paid",   "if": { "var": "Gold", "op": ">=", "value": 10 } },
        { "text": "Fight",          "next": "Fight" },
        { "text": "Ask the secret", "next": "Secret", "once": true } ] },

    { "name": "Paid",   "onEnter": ["paid"],   "options": [ { "text": "Go", "next": "End" } ] },
    { "name": "Fight",  "onEnter": ["fought"], "options": [ { "text": "Go", "next": "End" } ] },
    { "name": "Secret", "onEnter": ["secret_told"],
                        "options": [ { "text": "Back", "next": "Intro" } ] },
    { "name": "End",    "text": "Done.",       "options": [ { "text": "Finish", "next": "@end" } ] }
  ]
}
```

| Key | Where | Meaning |
| --- | --- | --- |
| `cosmic_story` | root | Format version. **Written back verbatim** — unlike `.cflow`, nothing bumps it. |
| `start` | root | First node. |
| `variables` | root | Same blackboard schema as `.cflow`, same four types. |
| `speaker`, `text` | node | The two strings you almost always draw. |
| `portrait`, `background`, `audio` | node | Asset **paths the runtime never loads** — it hands you the strings and your presentation layer decides. |
| `onEnter`, `onExit` | node | Arrays of signal names emitted on the scene bus. |
| `options` | node | The choices. |
| `text` | option | The button label. |
| `next` | option | Target node name. `""` or `"@end"` ends the story. |
| `once` | option | Hide this option after it has been chosen — **per run**. |
| `if` | option | A `FlowGuard`, exactly as in `.cflow`. |
| `editor` | node | `{ "pos": [x, y] }` Story-Editor layout; runtime-inert. |

Driving it:

```cpp
#include "scene/StoryGraph.h"

Cosmic::StoryGraph graph;
if (!Cosmic::StoryGraph::Load(graph, "project://stories/Intro.cstory"))
    return;

Cosmic::StoryRunner runner;
runner.Start(graph, &scene);          // scene: receives signals + is the field-guard source

const Cosmic::StoryNode* node = runner.Current();          // null once ended
for (int i = 0; i < (int)runner.ValidOptions().size(); ++i)
{
    const Cosmic::StoryOption& opt = node->Options[runner.ValidOptions()[i]];
    DrawButton(opt.Text, /*on click*/ [&, i] { runner.Choose(i); });
}
```

**`Choose` takes an index into `ValidOptions()`, not into `Options`.** `ValidOptions()` is a vector
of indices *into* `node->Options` — the ones whose guard passes and which have not been consumed by
`Once`. Out-of-range indices are ignored silently. Getting this wrong picks the wrong branch rather
than crashing, which makes it hard to spot; the pattern above (index the valid list, pass the loop
counter) is the safe shape.

`Choose` emits the current node's `onExit`, marks a `Once` option consumed, and enters `next`.
Entering a node emits its `onEnter` and rebuilds `ValidOptions()`. An unknown `next` logs
`"StoryRunner: unknown node '…' — ending"` and ends the story.

`StoryGraph::Validate()` mirrors the flow's: missing/dangling start, unnamed nodes, duplicate names,
and options pointing at unknown nodes (`""` and `"@end"` are exempt).

### Three behaviours worth internalising

- **`ValidOptions()` is rebuilt only on node entry.** Changing a variable while sitting on a node
  does **not** re-evaluate its guards. `tests/test_story.cpp:80` is built on this: it raises `Gold`
  while on `Intro`, and the previously-hidden option appears only after leaving and coming back.
  If your UI needs live re-evaluation, re-enter the node.
- **Story option guards are completely silent.** `RebuildValid` calls `EvaluateFlowGuard` without a
  `warn` callback, so a typo'd variable or component name produces **no log line at all** — the
  option just never appears. Flow guards warn; story guards do not. When an option is
  mysteriously missing, that is the first thing to check.
- **`Once` is per run, not per save.** The consumed set is keyed `"<node>#<optionIndex>"` and is
  cleared by `Stop()` and by `Start()`. Reordering a node's options in the editor also changes those
  keys, so a `Once` consumed *before* the reorder maps to a different option after it — irrelevant
  within a run, relevant if you ever persist the set yourself.

---

## Put dialogue on screen with `StoryUiBinding`

Every generated project ships `src/scripts/StoryUiBinding.h`, the stock zero-code presentation
layer. Attach it to any entity in a scene that also holds a UI canvas, point `StoryPath` at a
`.cstory`, and author UI entities with these tags:

| Tag (default) | Component the binding writes | What it shows |
| --- | --- | --- |
| `StoryText` | `UiTextComponent::Text` | The current node's `Text` |
| `StorySpeaker` | `UiTextComponent::Text` | The current node's `Speaker` |
| `StoryPortrait` | `UiImageComponent::TexturePath` | The node's `PortraitPath` |
| `StoryBackground` | `UiImageComponent::TexturePath` | The node's `BackgroundPath` |
| `StoryOption0` … `StoryOption3` | `TagComponent::Active`, `UiButtonComponent::Signal`, `UiTextComponent::Text` | One button per valid option |

Each option entity is shown or hidden with `TagComponent::Active` (hiding the whole subtree), has
its `Signal` rewritten to `"story_choose_<i>"`, and gets the option text. The binding's `OnSignal`
parses that index back out and calls `Choose`. `MaxOptions` is a compile-time `4`; a node with more
valid options than there are authored button entities simply shows the first four.

**Author the label on the button entity itself.** `SetText` writes `UiTextComponent` on the entity
it is handed, so a `StoryOption0` entity needs image + button + text all on one entity — the
`MakeUiButton` convention FlowDemo and ForgePong both use (`StarforgeApp.cpp:2791`). A label
parented *under* the button is never written.

Four caveats before you rely on it:

1. **As shipped it does not compile, and that is why nobody has noticed.** The file uses
   `UiTextComponent`, `UiImageComponent` and `UiButtonComponent` but does **not** include
   `scene/ui/UiComponents.h`, which `Cosmic.h` does not aggregate. It gets away with it because the
   template `Module.cpp` neither includes nor `CS_SCRIPT`-registers it — it ships in every generated
   project and is never built. Add the include and a `CS_SCRIPT(StoryUiBinding)` block before use.
2. **Its own header comment says "each frame" — it is not per-frame.** There is no `OnUpdate`
   override; `Refresh()` runs on `OnStart` and on each `story_choose_*` signal. Mutating the story's
   variables from elsewhere will not update the visible options until the next choice.
3. **It shares the flow blackboard automatically**: `m_Runner.Start(graph, &GetScene(),
   GetScene().ActiveFlow())` — null when no flow is running, which is exactly the "own store"
   case.
4. **It is a sample, not engine API.** Copy it into your project and edit it; the engine will never
   change it under you.

---

## Share variables between a flow and a story

`StoryRunner::Start`'s third parameter takes a `FlowMachine*`. Pass one and the runner stops keeping
its own map: every `GetVar`/`SetVar` and every option guard reads and writes the **flow's**
blackboard, so a dialogue choice can set a quest flag that a screen transition later reads.

Seeding is a **top-up, and the flow wins**: on `Start`, each of the story's declared variables is
written into the flow store *only if the flow does not already have that name*. A `Gold` declared in
both files keeps the flow's current value, not the story's default.

```cpp
// In a ScriptableEntity that lives in a flow-driven scene:
m_Runner.Start(graph, &GetScene(), GetScene().ActiveFlow());   // null ⇒ private store
```

Pass `nullptr` (or nothing) and the runner owns a private store seeded from the graph's own
`variables` — the right choice for a self-contained dialogue.

---

## Author both in Starforge

Both file types are first-class assets in the Content Browser: **Create ▸ Flow** and
**Create ▸ Story** write a minimal valid document (a flow with one `Start` state; a story with one
`Start` node whose single option goes to `@end`), and double-clicking opens the matching editor
document (`AssetTypes.cpp:39-40, 161-187`).

- **Flow Editor** — states are nodes (scene stem, start marker, red badges for missing scenes and
  unreachable states), transitions are links from per-transition pins, and a side inspector edits
  the selected state or transition. Its event picker is fed by scanning the project's scenes for
  `UiButtonComponent::Signal` strings, so authored button names appear in a dropdown rather than
  being retyped. `@quit`/`@pop` are pickable targets. Node positions persist into `EditorPos`.
  Save runs `Validate()` and lists the problems.
- **Story Editor** — nodes carry speaker/text/asset slots, option pins show `[if]` and `[once]`
  badges, and links run from an option pin to the target node (or an `@end` node). A toolbar **Play
  preview** runs a real `StoryRunner` in-panel with clickable option buttons.
- Both keep a **document-local JSON undo stack** (their own Undo/Redo buttons). Flow and story edits
  are file-scoped and deliberately do **not** share the scene `CommandStack`, so Ctrl+Z in the
  viewport will not undo a graph edit.
- Both carry the shared typed-variables side panel.

---

## Common patterns

**Menu → game → pause, zero code.** The [Quick start](#quick-start). Buttons emit; the flow routes;
`push`/`@pop` handles pause. This is the pattern most projects want and it needs no C++ at all.

**A gate that opens after N events.** Declare a `number` variable, increment it from a script with
`Flow().AddNumber(...)` (or an `onEnter` `setVar` with `"add": true`), and guard the transition with
`>=`. `tests/test_flowmachine.cpp:230` is this pattern end to end.

**A story that unlocks a screen.** Run the story with the flow as its shared store; a
`setVar`-equivalent (`runner.SetVar(...)`) or a `Once` branch writes a flag, and a later flow
transition guards on it. ForgeIsle's `MetHermit` / `BeaconsLit` / `TentWins` variables are shaped
for exactly this.

**A script reacting to the same button the flow consumes.** Emitting is a broadcast, not a hand-off:
the flow's `ConnectAny`, every script's `OnSignal`, and any explicit `Connect` all see it. Play a
click sound in a script while the flow changes screens on the same signal.

**A scene-less state for pure logic.** A state with no `"scene"` reuses whatever is beneath it, so
you can insert a decision node between two screens that only runs guards and `setVar` actions.
Remember field guards will read the *under* scene.

**Headless testing.** Both runtimes are GL-free. Hand `FlowMachine` a lambda loader that returns
`Scene::Create()` and drive `FeedSignal` + `OnUpdate(dt)` by hand;
`tests/test_flowmachine.cpp` and `tests/test_story.cpp` are the worked examples.

---

## Pitfalls

**"My button does nothing."** Four links in the chain, and any one breaks it silently: the host must
call `UiSystem::Update` (buttons are only live in **Play** inside the editor); the button's `Signal`
string must match the transition's `on` **exactly** (case-sensitive, no trimming); the flow must be
running (`IsRunning()`); and the button must be in the flow's **current top scene**.

**"The transition fires a frame late."** By design. `FeedSignal` queues and `OnUpdate` applies. If
your host calls `OnUpdate` before the UI update, add a frame; `PlayerLayer` updates the UI *first*
precisely so button signals are on the bus before the flow drains them.

**"`key:Space` doesn't work."** Nothing reads the keyboard for the flow. Only `key:Escape` is
bridged, in `PlayerLayer.cpp:238` and `StarforgeApp.cpp:770`. Bridge your own with a rising-edge
guard.

**"My guard is always false and there's no error."** That is the design — every guard failure mode
evaluates to false. Flow guards warn **once** per distinct reason, so check the log early in the
session; **story option guards never warn at all**. Verify the reflected name (`"Camera"`, not
`"CameraComponent"`) and remember a variable guard on an undeclared name is false.

**"An option disappeared and I don't know why."** Either its guard is failing (silently) or it is
`Once` and already consumed. `ValidOptions()` rebuilds only on node entry.

**"`Choose(2)` picked the wrong line."** The argument indexes `ValidOptions()`, not `Options`. With
one option hidden by a guard, valid index 1 is option 2.

**"Leaving the pause menu lost my game."** Only `@pop` unwinds the stack. Any other transition
target clears it and starts fresh — including a transition back to the state you pushed from.

**"`@pop` warns and nothing happens."** You are on the base frame. `@pop` requires
`StackDepth() > 1`.

**"The pause menu hides the game instead of dimming it."** v1 renders the top scene only. Give the
pushed state an empty `"scene"` and author the menu as a canvas inside the game scene.

**"My variables reset."** They are per-run. `Stop()` clears the map, and `@quit` calls `Stop()`.

**"`Flow().SetNumber` doesn't stick."** No flow is running, so the proxy is a no-op — or a *different*
flow is now driving the scene. Check `GetScene().ActiveFlow()`.

**"A script's `Signals().Connect` handle stopped working after a screen change."** The bus belongs to
the scene. A flow scene swap destroys the old bus and its subscriptions. Re-subscribe in `OnStart`
of the new scene's scripts (which `ScriptHost::Instantiate` triggers for you).

**"My `SystemScript` never gets `OnSignal`."** It has no such callback. Subscribe explicitly through
`GetScene().Events().Connect(...)`.

**"`setField` did nothing."** Vector, colour and quaternion fields are unsupported and log an
"unsupported kind" warning. Everything else that fails (missing tag, wrong component name) also logs
and continues — check the Console.

**"The log says a cascade guard tripped."** Two states are transitioning into each other on signals
they emit in `onEnter`. The queue was cleared to break the loop; fix the graph.

**"Scene loads but nothing renders / scripts don't run after a flow transition."** The machine only
swaps `ActiveScene()`. Your host must detect the change and rebind — see
`PlayerLayer::RebindScripts`.

**"`FlowAsset::Load` returned true but nothing happened."** Loading is forgiving: unknown keys and
malformed action entries are skipped, and only bad JSON fails. Run `Validate()` — a missing `start`
state loads fine and then refuses to run, logging
`"FlowMachine::Start: start state '…' not found"`.

**"Crash on shutdown after a flow was running."** The machine holds a raw `Scene*` for its bus
subscription. Call `Stop()` before the scenes go away; the destructor does it, but only if the
machine outlives nothing it points at.

---

## See also

- [`game-ui.md`](game-ui.md) — the UI entities that emit these signals: canvas, `RectTransform`,
  buttons and the hit-test
- [`scripting.md`](scripting.md) — `ScriptableEntity`, `OnSignal`, and all eight proxies including
  `Flow()` and `Signals()`
- [`scenes-and-serialization.md`](scenes-and-serialization.md) — `.cscene`, `SceneManager`, and the
  rebind that a scene swap requires
- [`entities-and-components.md`](entities-and-components.md) — `TagComponent`, `Active`, and the
  reflected names guards address
- [`project-anatomy.md`](project-anatomy.md) — `PlayerLayer`, the manifest, and the owner-ticked
  pattern these runtimes follow
- [`sprites-and-tilemaps.md`](sprites-and-tilemaps.md) — ForgePong, the other zero-code sample
- [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md) — why these three headers are
  in both configurations
