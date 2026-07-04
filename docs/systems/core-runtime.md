# Core Runtime — How It Works

> **STATUS: SKELETON** — to be filled by work order **D26** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** `Application` runs the frame loop — a fixed 60 Hz simulation pass, a variable
render pass, an ImGui pass, and a "Safe Zone" where structural changes (layer push/pop, DLL
swaps) happen without iterators in flight.
**Source:** `Cosmic/src/core/Application.*`, `core/Layer.h`, `core/LayerStack.*`, `core/Timestep.h`
**API Reference:** [../reference/core.md](../reference/core.md) · **Guide:** root README §3, §4, §7

## Section plan

1. **Overview** — the heartbeat metaphor; why *two* update rates exist (physics correctness vs smooth visuals). <!-- TODO(D26) -->
2. **Mental model** — diagram **DG-3** (frame-loop sequence) + **DG-10** (time-scale waterfall: rawDelta → global scale → layer scale → `GetLocalTime`). <!-- TODO(D26) -->
3. **Step-by-step** — one frame narrated in plain English, including the accumulator, spiral-of-death clamp (250 ms), pause vs `TimeScale(0)` (scale-preserving `Pause()` table from README §7), minimize behavior, render-while-dragging. <!-- TODO(D26) -->
4. **Technical implementation** — `Application::Run` structure, `s_Instance` ordering constraint (why `Get()` works during `Initialize`), LayerStack insert-index mechanics, the double-tick trap (mine README §33), transition queueing (`TransitionFromLauncherToWorkspace` deferral into the Safe Zone), state diagram **DG-11** (Launcher ⇄ Workspace). <!-- TODO(D26) -->
5. **Design decisions** — why fixed timestep is opt-in-configurable, why layers get pre-scaled deltas but not layer-scale (README §7 "what ts contains"), responsive-drag design (link `../design/responsive-rendering-and-pause.md`). <!-- TODO(D26) -->
6. **Limits & future work** — singleton refactor note (pass `Window&` into `OnAttach` someday). <!-- TODO(D26) -->

**Truth sources:** `Application.cpp` (read it — the loop is short), README §3/§7/§32/§33
(migrating here), design doc `responsive-rendering-and-pause.md`.
