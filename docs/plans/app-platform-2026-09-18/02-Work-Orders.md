# App Platform — work orders

Status: execution index, 2026-09-18. Eleven sessions (AP-05 is two). Prompts live in
[`work-orders/`](work-orders/); this file is the map: waves, dependencies, ownership, landing order, and
the ID cross-check. Model recommendations and the run table are in
[`00-Start-Here.md`](00-Start-Here.md#session-guide).

## Dependency graph

```
AP-00  packet + KI-39 fix ............................ done (planning session)
  │
  ├──────────────────────────────┐
AP-05A deletions (main)          AP-P1 packaging + acceptance-in-CI (worktree ap/p1, from AP-00)
  │                              │   lands after AP-01
AP-05B unfence (main)            │
  │                              │
AP-01  foundation (main) ◄───────┘   (AP-P1 rebases onto AP-01, lands)
  │
  ├── AP-02 widgets (ap/02) ─────┐
  ├── AP-04 templates+sample (ap/04) ├─ land in this order
  └── AP-D1 docs structure (ap/d1) ─┘
        │
      AP-03 editor UX + live loop + source links (ap/03, alone)
        │
      AP-D2 docs content (ap/d2, alone)
        │
      AP-Q1 integrate + qualify + showcase (main, alone)
```

The graph is dependency **and** the permitted concurrency: only the three wave-2 lanes (plus AP-P1
during wave 1) ever run at the same time, and their "Owns" sets are pairwise disjoint
([`01-Design-Contracts.md §10`](01-Design-Contracts.md#10-parallel-lanes-d-lanes--file-ownership-matrix)).

## Landing order

`AP-00 → AP-05A → AP-05B → AP-01 → AP-P1 → AP-02 → AP-04 → AP-D1 → AP-03 → AP-D2 → AP-Q1`

## The work orders

| WO | Gate | Title | Depends on | Acceptance | Prompt |
| --- | --- | --- | --- | --- | --- |
| AP-00 | G0 | Packet, KI-39 CI fix, superseded banners | — | audits exit 0 | [AP-00.md](work-orders/AP-00.md) |
| AP-05 | G1 | 3D purge (Part A deletions; Part B fence removal) | AP-00 | B06 | [AP-05.md](work-orders/AP-05.md) |
| AP-01 | G1 | Foundation: DataBus, services, Data(), flow additions, host wiring, template move | AP-05 | V01, V02, V06 | [AP-01.md](work-orders/AP-01.md) |
| AP-P1 | G2 | Packaging identity, writable user data, acceptance runner in CI | AP-00 (lands after AP-01) | K01, K02, K04, H05 | [AP-P1.md](work-orders/AP-P1.md) |
| AP-02 | G2 | Bound widgets, hosted-panel collection, goldens | AP-P1 landed | V03, V04, V05 | [AP-02.md](work-orders/AP-02.md) |
| AP-04 | G2 | Template kinds, samples on disk, PendulumLab, headless tests | AP-P1 landed | F02, Y01 | [AP-04.md](work-orders/AP-04.md) |
| AP-D1 | G2 | Docs structure: archive, parked-3d, roadmap v5 skeleton, link checker | AP-P1 landed | DOC01, DOC03, DOC04 | [AP-D1.md](work-orders/AP-D1.md) |
| AP-03 | G3 | Editor UX: rect gizmo, Screens/DataBus panels, source links, live loop, picker, self-test | AP-02, AP-04, AP-D1 landed | E01–E08, F01, V05 | [AP-03.md](work-orders/AP-03.md) |
| AP-D2 | G4 | Docs content: app-authoring chapter, updates, reference, roadmap final, DOC02 | AP-03 landed | DOC01–DOC03 | [AP-D2.md](work-orders/AP-D2.md) |
| AP-Q1 | G5 | Integrate, qualify, showcase kit, release report, staged push | all | S01–S04, Y02, Y03, K02, DOC05 | [AP-Q1.md](work-orders/AP-Q1.md) |

Gates: G0 planning · G1 the trunk is 2D-only source and has the foundation · G2 the parallel wave has
landed green · G3 the editor workflow is complete · G4 documented · G5 qualified and staged for Kaden.

## Cross-check: every acceptance ID has exactly one owner

| ID | Owner | ID | Owner | ID | Owner |
| --- | --- | --- | --- | --- | --- |
| B06 | AP-05 | V01 | AP-01 | F01 | AP-03 |
| E01 | AP-03 | V02 | AP-01 | F02 | AP-04 |
| E02 | AP-03 | V03 | AP-02 | Y01 | AP-04 |
| E03 | AP-03 | V04 | AP-02 | Y02 | AP-Q1 (host built by AP-04) |
| E04 | AP-03 | V05 | AP-02 (engine) + AP-03 (editor draw) | Y03 | AP-Q1 |
| E05 | AP-03 (executes AP-02's goldens A/B) | V06 | AP-01 | H05 | AP-P1 |
| E06 | AP-03 | K01, K02, K04 | AP-P1 | DOC01, DOC03, DOC04 | AP-D1 |
| E07 | AP-03 | K03 | AP-P1 (ENV-BLOCKED without a Win10 VM) | DOC02 | AP-D2 |
| E08 | AP-03 | S01–S04 | AP-Q1 | DOC05 | AP-Q1 |

V05 is the only ID with two owners; AP-02 proves the engine half headlessly/GPU, AP-03 the editor half
through the self-test host, and AP-Q1 reports both.

## Per-WO gate / done-when summary

- **AP-05** — both parts committed; B06 oracle greps clean; both configs 0-warn; retained 2D tests and
  goldens unchanged; `check_docs_coverage.ps1` exits 0 without the filter block; a "dead 3D-only classes"
  list is in the report.
- **AP-01** — §1/§2/§5 APIs exist with the documented semantics; both hosts run services in the fixed
  order; `StopScene`/`ReloadModule` destroy services and leave the bus intact; templates moved; V01/V02/V06
  green Debug+Release; new manifest rows; zero behaviour change for a project with no services.
- **AP-P1** — one package layout produced by all three paths (file lists compared); SF_Telem writes only
  under `user://` from a read-only install; K01/K02/K04 evidence; CI runs the `pr` acceptance profile.
- **AP-02** — all seven components reflected, serialized, rendered, interactive per §3; goldens + sentinel
  ROIs; existing goldens untouched.
- **AP-04** — three template kinds + two samples scaffold and open; PendulumLab builds standalone, its
  service passes the analytic oracle, its self-test host exists; every template script compiles in
  `CosmicTests`.
- **AP-D1** — archive + parked tiers exist with banners; manifest rewritten; roadmap v5 skeleton;
  link checker in CI and green for live tiers; DOC01/03/04 evidence.
- **AP-03** — E01–E08 pass through the self-test host on both configs; ImGui stack balanced across
  every action; template picker real; per-frame TOML reparse gone.
- **AP-D2** — the new chapter and updates written from source; DOC02 walkthrough executed literally
  and logged; roadmap v5 final.
- **AP-Q1** — full retained suites + every `ap*` manifest green or honestly blocked; Y02/Y03/S01 run;
  showcase kit; release report with the requirement→case→evidence matrix; staged push commands.
