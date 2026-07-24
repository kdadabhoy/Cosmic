# Phase 28 Plan — Flagship Showcase: **Forge Isle**

> **Created 2026-07-11.** The capstone (user decision 2026-07-11: "the last thing should be
> creating a very cool example application"). **Forge Isle** is a small, polished third-person
> island adventure built entirely with Starforge — a real packaged product whose every beat
> exists to demonstrate an engine capability, continuing the sample naming line
> (ForgePlayground → ForgePong → ForgeBlocks → **Forge Isle**). It is also the acceptance
> vehicle for the editor-vision phases: if a feature can't be exercised here, its phase isn't
> actually done.
>
> **The pitch:** you wash ashore on a volcanic island at dawn. A hermit at a campfire (story
> graph dialogue) sends you to relight three forge beacons: one across a physics ruin, one
> past creatures that patrol a canyon (navmesh AI), one inside a voxel dig site. Day rolls to
> night (physical sky); lighting the last beacon triggers a finale. In the hermit's tent, a
> playable retro table-game — a 2D top-down vignette with darkness + 2D lights — is the
> engine's range statement (and the doc 16 U8 2D sample, promoted).
>
> **Depends on (hard):** Phase 17 remainder (U3/U4/U6/U7/U8 authoring + game view), doc 19
> A1/A2 + Phase 24 (character/animation), Phase 25 Q2–Q4 (story), Phase 26 (nav AI),
> Phase 27 (sky/2D lights/world UI/RTT), Phase 22 K1 (branding). Phase 23 improves the
> *building* of it, not the product. Start Z1 (design/greybox) as soon as Phase 17's remainder
> lands — content work parallelizes with the later phases; each Z-item names what it waits on.

---

## 0. Execution notes

Forge Isle is a **project under `Projects/`/external-project rules** (S1–S3): everything a
user could author goes through Starforge authoring paths (scenes/recipes/scripts/flows/
stories) — engine changes are NOT allowed from this phase (if the game needs a verb, file it
in the owning phase doc first; the roadmap rule). C++ game logic uses the project-DLL script
tiers (E11/H9). Keep every asset license-clean (CC0/self-made; credit file). Performance bar:
≥60 fps at 1080p on the dev machine in every scene, measured with the T17 profiler. No git
writes.

## 1. Work orders

### Z1 — Design doc + greybox island *(waits on: Phase 17 remainder)*
**Files:** NEW `docs/design/forge-isle.md` (one-pager: beat map, feature→moment matrix, scene
list, asset list w/ sources); NEW project `ForgeIsle` (scaffolded external project): greybox
island terrain recipe, blockout ruins/canyon/dig-site/camp, scene-camera walkthrough.
**Spec:** the feature→moment matrix is the contract — every phase 14–27 capability maps to a
specific on-island moment (physics ruin = Jolt stack + character; beacons = flow variables;
tent game = 2D stack; …). **Acceptance:** the matrix has no unmapped capability; greybox walks
end-to-end with the U7 game view; design doc reviewed by the user. **Status:** ◑ STAGED
2026-07-14 — `docs/design/forge-isle.md` written (beat map B0–B7, 3+3 scene/graph list, §4
license-clean asset list w/ the 5-row user-supplied "needed assets" table, §5 feature→moment
matrix covering EVERY Phase 14–27 work order [P]/[E] with phases 19/21 named out-of-matrix
except fired R8, §6 trailer script, §7 engineering constraints from the same-day pre-flight
review, §8 Z-scope map). `Projects/ForgeIsle` scaffolded as a dual-mode external project
(ViperSim CMake pattern: root auto-detect + Starforge Ctrl+B standalone; manifest with
`startup_flow`; `PlayerController` v0 capsule walker w/ mouse-look; CREDITS.md). Greybox
authored: `Island.cscene` (513² physical-sky island terrain recipe + TerrainCollider + ocean
recipe, camp/ruin/canyon/quarry/summit blockouts, 3 dynamic ruin crates, 3 beacon markers,
player + HUD), `Title.cscene` + `Pause.cscene` (zero-code canvases), `flows/Main.cflow`
(Title→Island→push-Pause spine + Q2 quest variables). NEW gated `tests/test_forgeisle_content.cpp`
(3 cases): scenes carry their contract entities, flow validates + scene paths exist + pause is a
push-with-own-scene, and the route is walkable on the EXACT recipe terrain (BuildTerrainSpec →
SampleHeight: 11 site anchors in ±0.8 m windows + 6 route legs above water at ≤3.2 m/4 m grade).
REMAINING for ✅: **the user's design-doc review (this doc's gate) + the on-GPU game-view
walkthrough**.

### Z2 — Playable character *(waits on: A1/A2, Phase 24 M4/M6)*
**Files:** ForgeIsle content: a rigged CC0/self-made character (glTF) + idle/walk/run/jump/
interact clips; project scripts `PlayerController` (CharacterController + M6 crossfades +
camera follow), interaction raycast (`Physics()` queries) with X6 world-anchored prompts;
a socketed lantern (M4) that lights at night.
**Acceptance:** third-person traversal feels responsive (walk/run/jump on terrain, slopes,
the ruin); blends pop-free; lantern tracks the hand socket; recorded clip. **Status:** ☐

### Z3 — Living world *(waits on: Phase 27 X1/X2; polish anytime after Z1)*
**Files:** ForgeIsle scenes: physical-sky day/night cycle (a SystemScript scrubs sun +
`TimeOfDay`), ocean + lake (water recipes), campfire/beacon particles with X3 curl noise,
fireflies at night, ambience via `AudioEngine` loops, voxel dig site volume (V-series) with
place/break gated to a held tool.
**Acceptance:** dawn→noon→dusk→night reads beautifully (screenshots at 4 times of day);
beacons/campfire feel alive; dig site editable in Play and persists the session. **Status:** ☐

### Z4 — Creatures & AI *(waits on: Phase 26)*
**Files:** ForgeIsle: navmesh recipe over the canyon + camp (N2), 4–6 creatures
(NavAgent + SystemScript patrol/investigate/chase states, H9 tier), the hermit wanders the
camp between dialogues.
**Acceptance:** creatures patrol ramps/bridges, converge on the player when spotted, never
interpenetrate or walk water; bake+Play round-trip from a fresh clone with no manual steps.
**Status:** ☐

### Z5 — Story, flow & UI *(waits on: Phase 25, Phase 17 U1/U5/U6)*
**Files:** ForgeIsle: `.cstory` graphs (hermit intro/hints/finale — portraits, VO-less audio
stings, guarded options via Q2 variables: `BeaconsLit`, `MetHermit`, once-flags), `.cflow`
app flow (title → game → pause overlay → finale → credits) driving scenes + UI canvases (U1
buttons/text/images), HUD (beacon counter, interaction prompt, X7 RTT minimap in a corner),
X6 nameplate over the hermit.
**Acceptance:** zero-C++ screens: title/pause/credits run purely from flow + UI entities;
dialogue branches respect variables (lighting beacons changes hermit lines); HUD/minimap live.
**Status:** ☐

### Z6 — The tent game (2D vignette) *(waits on: Phase 27 X5, doc 16 U3/U4; absorbs U8's 2D-sample duty)*
**Files:** ForgeIsle: a `.cflow`-pushed 2D scene — top-down tilemap board (U4), darkness +
lantern 2D lights (X5), sprite player + pieces, hotbar-style UI row, win/lose signals return
to the tent via the flow stack (U5 overlay push/pop).
**Spec:** small (one screen, one loop) — it exists to show the 2D stack shipping inside a 3D
product. Coordinate with doc 16 U8: this vignette satisfies U8's "2D sample plays packaged"
line (note it there when landing).
**Acceptance:** enter/exit the tent game without leaks (flow stack restores the 3D scene);
2D scene is pixel-crisp at integer scales; packaged build includes it. **Status:** ☐

### Z7 — Branding, package, trailer *(waits on: Phase 22 K1; last)*
**Files:** ForgeIsle `icon.png` + branding pass (K1: window/taskbar icon + the project's
identity everywhere), Project Settings (title/size), Packager run (icon/zip/installer),
telemetry-recorded trailer script (`docs/design/forge-isle.md` §trailer — the E21/J9 recorded-
demo pattern), homescreen tile + `docs/plans/12-documentation-plan.md` D40 row.
**Acceptance (the phase DoD):** clean-machine install from the installer → Forge Isle runs
start-to-finale at ≥60 fps/1080p with its own icon/title, user data isolated (S6), `--replay`
works; the recorded trailer is saved; **the Z1 matrix is walked live on stream/recording with
every row demonstrated.** **Status:** ☐

## Kickoff prompt

> You are implementing ONE work order from `docs/plans/27-phase28-flagship-sample-plan.md` in
> `C:\dev\Cosmic`. Read §0, your item, and `docs/design/forge-isle.md` (Z1's design doc — the
> content spec of record once it exists). App-side only: no engine edits from this phase —
> missing verbs get filed in their owning phase doc first. Authoring goes through Starforge
> paths; license-clean assets; ≥60 fps bar; roadmap cmake recipe; no git writes. Finish with
> Acceptance + status banner.
