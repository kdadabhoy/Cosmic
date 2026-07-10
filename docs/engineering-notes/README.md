# Cosmic Engine — Engineering Notes

> **Purpose:** durable "why it broke / how this system actually works" writeups. When a bug takes real
> investigation to root-cause — especially platform/driver/DPI/threading issues that only reproduce on
> *some* machines — the understanding goes here so the next person (or the next you) doesn't re-derive it.
>
> This is **not** a roadmap (see [`../plans/00-MASTER-ROADMAP.md`](../plans/00-MASTER-ROADMAP.md)) and
> **not** a subsystem reference (see README Part 2 and [`../archive/`](../archive/)). It's a growing
> collection of focused postmortems and system deep-dives.

## How to add a note

Create `docs/engineering-notes/<short-kebab-title>.md` and add a line to the index below. Keep each note
self-contained and grounded:

- **Symptom** — what was observed, and crucially *where it did / didn't* reproduce (machine, OS, DPI, GPU).
- **Root cause** — the actual mechanism, with `file:line` references to the real source (engine and, where
  relevant, dependencies like GLFW/ImGui).
- **Fix** — what changed and *why that addresses the root cause*, not just the symptom.
- **Verification** — how to confirm it (ideally with concrete numbers/values to capture).
- Note the **"Verified against commit"** so a future reader knows how stale the line references may be.

## Index

| Note | What it covers |
| ---- | -------------- |
| [borderless-window-dpi.md](borderless-window-dpi.md) | HiDPI-only missing custom title bar + mouse-click offset — GLFW's decorated-frame DPI geometry math vs. a visually-stripped frame, and the borderless-model fix. |
| [gl-resource-teardown.md](gl-resource-teardown.md) | `glDelete*` access violation in `opengl32.dll` on close — static GPU-handle lifetime vs. OpenGL context destruction, and the current-context destructor guard. |
| [kiss-telemetry-resync.md](kiss-telemetry-resync.md) | Intermittent "stale weapon" telemetry — delimiter-less KISS frames desyncing permanently after a BT-stall flush, the self-syncing CRC reader that re-locks, and the ESP32 `availableForWrite()` footgun. |
| [starforge-homescreen-hidden.md](starforge-homescreen-hidden.md) | Homescreen (project library + "Voxel Sample" button) invisible with no project open — `NoBringToFrontOnFocus` pins the window to the back of ImGui's z-stack, behind the opaque dockspace host (plus a secondary sizing coupling to the collapsible Viewport node). **Fixed & verified on-GPU 2026-07-10** (correct flags + hide the Viewport panel while no project is open). |
