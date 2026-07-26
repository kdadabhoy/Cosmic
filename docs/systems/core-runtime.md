# Core Runtime — How It Works

> **STATUS: SKELETON** — to be filled by work order **D26** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** `Application` runs the frame loop — a fixed 60 Hz simulation pass, a variable
render pass, an ImGui pass, and a "Safe Zone" where structural changes (layer push/pop, DLL
swaps) happen without iterators in flight.
**Source:** `Cosmic/src/core/Application.*`, `core/Layer.h`, `core/LayerStack.*`, `core/Timestep.h`
**API Reference:** [../reference/core.md](../reference/core.md) · **Guide:**
[../guide/project-anatomy.md](../guide/project-anatomy.md) (lifecycle, frame loop, layers, the Safe
Zone) · [../guide/time-and-ticks.md](../guide/time-and-ticks.md) (the time model)

## Section plan

1. **Overview** — the heartbeat metaphor; why *two* update rates exist (physics correctness vs smooth visuals). <!-- TODO(D26) -->
2. **Mental model** — diagram **DG-3** (frame-loop sequence) + **DG-10** (time-scale waterfall: rawDelta → global scale → layer scale → `GetLocalTime`). **Both were already built:** DG-3 by D47 in [`../guide/project-anatomy.md`](../guide/project-anatomy.md#dg-3--the-frame-sequence), DG-10 by D48 in [`../guide/time-and-ticks.md`](../guide/time-and-ticks.md#dg-10--the-time-waterfall) — reuse those sources, don't re-derive them. <!-- TODO(D26) -->
3. **Step-by-step** — one frame narrated in plain English, including the accumulator, spiral-of-death clamp (250 ms — note it clamps the *frame time*, not the accumulator, contrary to README §32), pause vs `TimeScale(0)` (the scale-preserving `Pause()` table is in [`../guide/time-and-ticks.md`](../guide/time-and-ticks.md#pause-versus-timescale0)), minimize behavior, render-while-dragging. <!-- TODO(D26) -->
4. **Technical implementation** — `Application::Run` structure, `s_Instance` ordering constraint (why `Get()` works during `Initialize`), LayerStack insert-index mechanics, the double-tick trap (mine README §33), transition queueing (`TransitionFromLauncherToWorkspace` deferral into the Safe Zone), state diagram **DG-11** (Launcher ⇄ Workspace) — **built by D47** in [`../guide/project-anatomy.md`](../guide/project-anatomy.md#dg-11--application-states), reuse it. <!-- TODO(D26) -->
5. **Design decisions** — why fixed timestep is opt-in-configurable, why stack layers get pre-scaled deltas but not layer-scale while a *plugin* layer gets both (the "what `ts` contains" table in [`../guide/time-and-ticks.md`](../guide/time-and-ticks.md#what-ts-and-dt-actually-contain--and-where-it-differs)), why the fixed `dt` magnitude is unscaled, and the unreachable negative-`dt` branch, responsive-drag design (link `../design/responsive-rendering-and-pause.md`). <!-- TODO(D26) -->
6. **Limits & future work** — singleton refactor note (pass `Window&` into `OnAttach` someday). <!-- TODO(D26) -->

**Truth sources:** `Application.cpp` (read it — the loop is short),
[`../guide/project-anatomy.md`](../guide/project-anatomy.md) (D47 — the client-facing account of the
same machinery, written from source; this explainer supplies the *why*),
[`../guide/time-and-ticks.md`](../guide/time-and-ticks.md) (D48 — the client-facing time model),
README §32/§33 (migrating here — **§32's accumulator code sketch is stale**: the clamp is on the
frame time, the hook order is `UpdateLayerTime` *then* `OnUpdate`, and the fixed rate is
configurable, not a hard `1/60`), design doc `responsive-rendering-and-pause.md`.
