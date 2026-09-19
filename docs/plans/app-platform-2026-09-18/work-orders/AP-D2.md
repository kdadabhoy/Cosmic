# AP-D2 — Docs content: the app-authoring chapter, chapter updates, reference, roadmap final, DOC02

**Gate:** G4 · **Wave:** 4 (alone) · **Runs:** worktree `ap/d2` · **Base:** `main` after AP-03 has landed ·
**Depends on:** AP-03 · **Acceptance:** DOC01–DOC03 (rerun) · **Model:** Opus 5 · **Effort:** high (Sonnet 5 acceptable for
the chapter prose only, with Opus verifying every code claim) · **Status:** not started

Written from source per the guide's five authoring rules (`docs/guide/README.md:26-44`): the new
chapter that a first-time user follows end to end, the updates to the chapters the new APIs touch, a
reference chapter for the new headers, a real systems explainer, and the DOC02 fresh-checkout
walkthrough executed literally.

## Copy-paste prompt

~~~text
Execute only AP-D2 from the Cosmic App Platform packet (docs/plans/app-platform-2026-09-18/). Read
work-orders/README.md (all, lane rules), docs/guide/README.md (the authoring contract and document
format), 01-Design-Contracts.md sections 1-8 (as the map of what exists — but every claim you write
must be verified against the headers and .cpp files on main, not against the contract), and
03-Acceptance-Catalog.md rows DOC01-DOC03 (../2d-stability-2026-09-16/03-Acceptance-Test-Catalog.md
lines 211-213).

Lane setup: from C:\dev\Cosmic run  git worktree add ..\Cosmic-ap-d2 -b ap/d2 main  (main contains
AP-03), work inside C:\dev\Cosmic-ap-d2, set $env:COSMIC_SDK to it, build the editor once so you can
verify behaviour while writing. You touch docs and README.md only.

Write, in the guide's mandatory format (header block, Quick start, task sections, Common patterns,
Pitfalls, See also):
1. docs/guide/app-authoring.md — the end-to-end chapter: create an app project from the template;
   what the tree contains; screens and the flow; services (CS_SERVICE, AppContext, the frame order,
   what OnAttach/OnSceneChanged/OnSignal are for); the DataBus (channels, history, producers,
   subscriptions, the main-thread rule); the seven bound widgets with the anchor idioms; hosted panels
   (CS_PANEL, ImPlot inside a screen, the placement rules and the limits: always on top, not in edit
   mode); screen scripts and the Data() proxy; commands from buttons/sliders/toggles to services;
   the live loop (exactly what survives a reload and the "state on the bus" pattern); source links;
   packaging and running; a PendulumLab walkthrough with the exact files. Every example compiles
   against main (build them in a scratch project).
2. Updates: game-ui.md (widgets section pointer + the sibling rules), flow-and-story.md (channel guards,
   when, StartAt, the key bridge and the table of key names, the "key:Escape only" text is now history),
   scripting.md (Data(), services vs scripts vs systems: which to use), getting-started.md (the template
   kinds, the picker, samples on disk, the trunk policy, no worktree advice), project-anatomy.md
   (services in the lifecycle tables, the hosts' frame order), building-and-shipping.md (the single
   package layout from AP-P1, writable user data), editor-ui-and-theming.md (Screens/DataBus panels,
   the rect gizmo, source links, the live-loop chip).
3. docs/reference/app-services.md in the reference entry format for data/DataBus.h,
   scripting/AppService.h, scripting/ServiceHost.h, scene/FlowKeyBridge.h and the FlowMachine
   additions; re-point the four manifest rows AP-01 added to it; keep every signature copied verbatim.
4. docs/systems/app-platform.md — a real explainer (not a skeleton): why the bus lives in the host, why
   services are recreated on reload, the frame order and its reasons, how hosted panels coexist with
   the canvas, the design rule; delete the STATUS: SKELETON banner only from this chapter and make the
   coverage checker's strict mode pass for it.
5. docs/plans/00-MASTER-ROADMAP.md v5 final (statuses from the landed reports); docs/README.md and
   README.md doc-map/most-asked list updated; the showcase strip slot left for AP-Q1.
6. DOC02: from a FRESH clone in a temp directory, following only the current docs, configure the 2D
   build, build, run the tests, create an app project from the template, add a screen with a linked
   script, edit its C++ with the editor open and observe the live reload, package it and run the
   package from another directory. Log every command and where the docs were unclear; fix the docs,
   not the log. DOC01: docs-coverage + link checker green; DOC03: one consistent policy (grep for the
   forbidden phrases from AP-D1's sweep again).

Report each chapter per the guide's rule 5 ("say what you found": what the code does that the
contract or older docs got wrong). Evidence in evidence/AP-D2/report.md with the DOC02 log. Land per
L2 (rebase, audits + link checker, commit on ap/d2 as kdadabhoy, no AI trailer, one commit per
chapter group). Do not push. Return: the chapter list, the DOC02 log summary, and any behaviour you
documented that AP-Q1 should re-check.
~~~

## Files to read first (and nothing else)

`work-orders/README.md`; `docs/guide/README.md`; `01-Design-Contracts.md` §1–§8; the DOC rows; then the
engine headers named in item 3 and the chapters named in item 2 (each in full, since you are editing them).

## Owns / May touch

See §10 (AP-D2 row). May touch `docs/reference/README.md` (rows).

## Scope

- **In:** items 1–6.
- **Out:** any source change (report it for AP-Q1); rewriting parked 3D docs; the showcase kit (AP-Q1).

## Deliverables

`app-authoring.md`; the seven chapter updates; `reference/app-services.md`; `systems/app-platform.md`;
roadmap v5 final; README/doc-index updates; `evidence/AP-D2/report.md` with the DOC02 log.

## Done when (DoD)

DOC01–DOC03 green; the DOC02 walkthrough completed from a fresh clone using only the docs; every code
example compiles.

## Rollback

Docs-only: revert the lane's commits.
