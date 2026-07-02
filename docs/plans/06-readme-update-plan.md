# README & Docs Update Plan

> **State:** `README.md` is a 4,498-line monolith (Part I client guide §1–29, Part II internals
> §30–43). Spot-checks show it is **largely accurate** (build scripts, template layers, telemetry
> API, `engine://`/`project://` VFS all verified against source) — the problems are a handful of
> stale claims, several missing subsystems, and structure/navigability.
> Each numbered item below is written to be pasted directly to an AI as its own task.

---

## A. Corrections (stale/wrong content)

### A1 — Fix the §34 `RenderPass` constructor claim  *(= BUG-5 in doc 01)* ✅ *(done 2026-07-01; the section is numbered §38 in the current README)*
```
File: README.md, line ~3617 (section "§34 RenderPass Stack — Implementation Details").
The README shows: RenderPass(const OrthographicCamera& camera, std::optional<glm::vec4> viewportBounds = std::nullopt);
and describes behavior for std::nullopt. The REAL constructor (Cosmic/src/renderer/RenderPass.h) takes a
MANDATORY const glm::vec4& viewportBounds — no optional, no default. Open RenderPass.h, copy the exact
signature(s), replace the README's version, and DELETE the paragraph describing std::nullopt semantics.
Cross-check the §14 and §25 usage examples still match (they already pass explicit vec4s).
```

### A2 — Telemetry version sweep ✅ *(done 2026-07-01 — §26/§38/§42 v1-consistent; directory fallback documented)*
```
File: README.md. Search for "v2", "v3", "scene.bin". The binary format is v1 (DataRecorder.h writes
version = 1). Fix any remaining claim that says otherwise, and make §26/§38/§42 consistent with the
header docstrings once WO-2 (doc 02) has landed — including documenting the new directory fallback
("scene.bin if present, else every *.bin in the folder").
```

### A3 — Retire `docs/old/` ✅ *(done 2026-07-01 — still-referenced files moved back to `docs/`, duplicate drafts deleted)*
```
docs/old/ contains Part1/2/3 drafts duplicating README content plus superseded JobSystem/TelemetryAudit
docs. Delete the folder (git history preserves it). Update any links that point into it (search the
repo for "docs/old").
```

### A4 — Mark `docs/engine_analysis.md` as historical ✅ *(done 2026-07-01)*
```
Add a banner at the top of docs/engine_analysis.md: "Historical analysis (2026-05-30). P1/P2 items were
fixed in the 2026-06-24 pass (see IMPROVEMENTS.md); remaining live items were re-audited into
docs/plans/01-bug-audit.md (2026-07-01). Section 6/5.1 (text rendering) is stale — world-space SDF text
now exists (README §27)." Do not edit the body.
```

---

## B. Missing sections to write (each: read the named source first, then write the section in the
README's existing voice — second person, tables for API surfaces, "why" callouts)

### B1 — SerialLink (the biggest gap) ✅ *(done 2026-07-01 — README §20.5, plus a §20.6 Framing bonus)*
```
Sources: Cosmic/src/serial/SerialLink.h/.cpp (+ SerialPort.h for the State enum). README §20 covers
raw SerialPort only. Add "§20.5 SerialLink — Managed Connections": what it adds over SerialPort
(async BeginOpen policy, auto-reconnect, shared connect UI), the API surface, and a usage example
matching how Projects/SF_Telem/src/TelemHub.h uses it. Include the "never call blocking Open on the
render thread for Bluetooth ports" warning (SerialPort.cpp:44-48 comment explains why).
```

### B2 — Theme system ✅ *(done 2026-07-01 — README §28.5)*
```
Sources: Cosmic/src/layers/ImGuiThemes.h, the ThemeManager implementation (search "ThemeManager"),
engine ThemeSelector widget, Theme Studio (see docs/IMGUI_MODERNIZATION_CHANGES.md). Add a client-guide
section: available built-in themes, how a project selects/persists a theme, how to author one
(data-driven format + Theme Studio workflow), Lucide icon usage (IconsLucide.h) and the default
Roboto font setup.
```

### B3 — Window chrome & DPI (promote from engineering-notes) ✅ *(verified 2026-07-01 — README §24 already covered every required point and links the note; no edit needed)*
```
Source: docs/engineering-notes/borderless-window-dpi.md + README §24. Add a short client-facing
subsection to §24: borderless chrome behavior (native snap/resize/animations preserved), what happens
at >100% DPI scale, F11/fullscreen override hooks, and a link to the engineering note for the full
war story. Keep it ~30 lines; the note stays the deep dive.
```

### B4 — DataRecorder autosave & failsafe ✅ *(done 2026-07-01 — README §26 "Autosave (Crash Failsafe)")*
```
Source: Cosmic/src/telemetry/DataRecorder.h (SetAutosave API) and TelemHub usage in SF_Telem
(recordings/<app>/_autosave every 5 s). Document in §26: the API, the interval/threading behavior,
where files land, and recovery-after-crash workflow.
```

### B5 — Homescreen / multi-screen app pattern ✅ *(done 2026-07-01 — README §21.5)*
```
Source: Projects/SF_Telem/src (root layer + screen layers + SimHub-style TelemHub). §21 documents the
template's composite pattern; add "§21.5 Real-world pattern: homescreen + screens" describing the
SF_Telem architecture (tile menu → screens sharing one hub object, replay driving the dashboard) as
the recommended shape for tool-style apps. 40-60 lines + one diagram.
```

### B6 — Packaging & distribution section refresh
```
Source: package.bat + root CMakeLists install rules + docs/plans/07-installer-packaging-plan.md.
Update §40: document package.bat's dist/Cosmic layout, the projects/ scan order
(LauncherLayer.cpp:544 — exeDir/projects then exeDir), COSMIC_DIST effects, and — once doc 07 lands —
the installer + `--project` boot flag. Note explicitly that COSMIC_SDK_DIR matters at BUILD time only;
runtime is relative to the exe (Main.cpp sets CWD).
```

---

## C. Structure (do LAST, after A+B — one mechanical session)

Split the monolith. Target layout:

| File | Contents | Source |
| --- | --- | --- |
| `README.md` (new, ~150 lines) | What Cosmic is, screenshot, feature bullets, quickstart (setup.bat → build_all.bat → launcher), links to the three guides + docs/plans/ | write fresh |
| `docs/client-guide.md` | current §1–§29 (+ new B-sections) | cut/paste |
| `docs/engine-internals.md` | current §30–§43 | cut/paste |

Rules for the split task: pure move — no rewording during the move (A/B already handled content);
fix intra-doc anchors (`#14-renderpass...` links must be updated to point across files where needed);
keep section numbers so existing references ("README §26") stay meaningful; add a one-line redirect
note at the top of each new file. Update links in `docs/plans/*`, `docs/engineering-notes/*`, and
`docs/IMPROVEMENTS.md` afterward (grep for `README.md#`).

**Deliberately NOT recommended:** docs-site generators (mkdocs/docusaurus) — three well-linked
markdown files are the right weight for a solo-dev SDK repo.

---

## Order & effort

| Step | Size | Depends on |
| --- | --- | --- |
| A1–A4 corrections | 1 short session | A2 partially on WO-2 |
| B1, B4 (serial + autosave) | 1 session | — |
| B2 (themes) | 1 session | — |
| B3, B5 (chrome, homescreen) | 1 session | — |
| B6 (packaging) | 30 min | doc 07 implemented |
| C split | 1 mechanical session | A, B done |

Verification after C: click every TOC link in all three files on GitHub; grep for dead
`README.md#4x-` anchors; confirm §-number references in source comments (e.g. Components.h references
"README §35") still resolve to a findable heading.
