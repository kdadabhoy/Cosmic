# Starforge — Phase 13 acceptance demo (recording script)

> **This is Phase 13's definition of done.** A single recorded run that exercises
> the whole editor loop end-to-end. Everything below runs on the user's GPU (the
> automated half — reflection, serialization, `.bak` rotation, recipe→spec
> mapping, telemetry routing, boot.cfg — is covered by `CosmicTests`). Plan of
> record: [`docs/plans/archive/11-phase13-starforge-plan.md`](../plans/archive/11-phase13-starforge-plan.md).

The **Forge Playground** sample (offered on first run, or Home ▸ *Open "Forge
Playground" sample*) is the fast path: it already contains terrain + a lake + a
campfire, primitives, a bouncing-ball C++ script, and a saved telemetry take, so
most of the steps below can be *shown* rather than *built*. The script below is
the from-scratch version — pick whichever tells the better story.

## Setup

1. Build **Release** (engine + `Starforge` DLL + your project DLL) with the
   VS-bundled cmake recipe (see `docs/reference` / `00-MASTER-ROADMAP.md`
   "Working agreement"). Outputs land in `build/Runtime/Release/`.
2. Launch `CosmicApp.exe` and pick **Starforge** (or `CosmicApp --project Starforge`).
3. Start the screen recorder.

## The run (≈4–6 min)

1. **New project.** Home ▸ New Project → name it (e.g. `ForgeDemo`) ▸ Create.
   The editor scaffolds `assets/projects/ForgeDemo/` (scripts + `Module.cpp` +
   `CMakeLists.txt`) and opens `Main.cscene`.
2. **Import a model.** File ▸ Import Model… → point at an `.obj` (or an FBX/STL if
   you built with `COSMIC_WITH_ASSIMP`). It copies into `project://models/`, writes
   a `.cmeta` unit-scale preset, and spawns the entity. Frame it with **F**.
3. **Build the scene.**
   - Entity ▸ World ▸ **Terrain** → the World Systems panel opens; tune the recipe
     and press **Regenerate Terrain** (builds off the JobSystem).
   - Entity ▸ World ▸ **Water** → pick the **Lake** (or Ocean) preset.
   - Entity ▸ World ▸ **Particle Emitter** → the default recipe is a campfire ember
     cone; place it and watch the live preview.
   - Entity ▸ Primitive → add a **Cube**/**Sphere**; tune `Size`/`Radius` in the
     Inspector (live, undoable) and move them with the gizmo (**W/E/R**).
   - Entity ▸ Light → a **Directional Light**; open **View ▸ Environment** and
     scrub **Time of Day** to a sunset, enable Fog/Bloom.
4. **Write a script + hot-reload.**
   - In the scaffolded `src/scripts/`, open (or add) a script — e.g.
     `HoverController.h/.cpp` — and give an entity a **Native Script** component
     (Inspector ▸ Add Component ▸ Native Script ▸ pick the class).
   - Edit the C++, then **Build Scripts** (Ctrl+B). Watch the Console stream the
     cmake build and the module hot-reload (`module ok`), with scene state
     preserved.
5. **Play with live telemetry.**
   - Open **View ▸ Telemetry**. Right-click a numeric field in the Inspector (e.g.
     the ball's `height`, or a script field) → **Record for telemetry** (a red
     `REC` tag appears). Or select the entity and *Add from selection*.
   - Press **Play**. The scopes plot live per fixed step (Follow + window). Pause
     and **Step** to advance one deterministic step.
   - Press **Stop**. The take is kept — scrub the playhead, read values, and
     **Export CSV**. (Confirm the CSV row count == fixed-dt sample count.)
   - Show the **Saved takes** browser reloading the take (this survives an editor
     restart — the take lives in `user://starforge/takes/`).
6. **Package.** File ▸ Package… → **Package**. It stages `dist/ForgeDemo/`:
   `ForgeDemo.exe`, `Cosmic.dll`, `ForgeDemo.dll`, the project's assets, and
   `boot.cfg`.
7. **Run the shipped exe on a clean path.** Copy `dist/ForgeDemo/` to a folder with
   **no repo and no `COSMIC_SDK`** (a USB stick / another user profile). Double-click
   `ForgeDemo.exe` → it boots straight into the scene via `boot.cfg`, no Launcher.

## What the recording proves

- Scenes assemble visually (import + primitives + terrain/water/particles + lights
  + environment), edits are reflection-driven and undoable, and saves are
  crash-safe (atomic write + one `.cscene.bak`).
- Logic is real C++ compiled into the project DLL and hot-reloaded in-editor.
- Any reflected numeric value records/plots/exports like a test rig.
- The result ships: a self-contained folder double-clicks into the scene with no
  editor and no SDK present.

> Save the capture next to this doc (or link it) and flip E21 to ✅ once recorded.
