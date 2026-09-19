# Cosmic 2D App Platform — planning packet

Prepared 2026-09-18 against `main` at `8da533c0aee19d0706616c4c2b55cadb7bc53629` (origin/main == main).
Successor of [`../2d-stability-2026-09-16/`](../2d-stability-2026-09-16/00-Start-Here.md), whose
WO-00..WO-10 are done and whose WO-11/12/13 are absorbed here (AP-P1, AP-D1+AP-D2, AP-Q1).

**Goal.** Make Cosmic the engine Kaden reaches for when a project needs visuals — telemetry ground
stations, tuning/command panels, simulations, later games — with the app's logic in his own C++ and
the *screens* authored in Starforge: arrange readouts, plots, images and buttons; wire screen switches
in the flow graph; see values live; edit the C++ with the editor open and have the logic update; click
any widget or panel to open the C++ file that drives it; package to an exe. Then not touch the engine
for a long while, and have plenty to show on the website.

## Read in this order

1. **This file** — decisions, waves, the session guide.
2. [`01-Design-Contracts.md`](01-Design-Contracts.md) — the authoring model and every name/field/
   signature the parallel lanes must agree on. A work order reads only the sections its prompt names.
3. [`02-Work-Orders.md`](02-Work-Orders.md) — the eleven sessions: waves, dependency graph,
   file-ownership matrix, landing order, cross-check table.
4. [`03-Acceptance-Catalog.md`](03-Acceptance-Catalog.md) — the acceptance IDs, fixtures, manifests.
5. [`work-orders/README.md`](work-orders/README.md) — the global rules every session obeys, then the
   one `work-orders/AP-xx.md` whose prompt you are running.

Nothing in this packet is executed by reading it. Engine behaviour changes only when a work order runs.

## Locked decisions (Kaden, 2026-09-18) — a work order may record evidence, never reopen these

- **D-UI** — screens are the entity canvas (`scene/ui/*`) with new data-bound widgets **plus** an ImGui
  "hosted panel" slot the app draws with ImGui/ImPlot. Not ImGui dock layouts.
- **D-LANES** — parallel work runs in one git worktree per lane with a declared file set; serial work
  runs alone on `main`. Safer beats faster: at most three sessions at once, concurrent lanes never share
  a file, every lane rebases and re-runs the retained suites before it lands.
- **D-SAMPLE** — `Projects/SF_Telem` is its own app and is **not** migrated or edited beyond the two
  writable-path lines AP-P1 needs. The showcase sample is **PendulumLab**: not ground control, an
  intuitive "logic in C++, visuals in the editor" demonstration.
- **D-DOCS** — archive everything completed or superseded; park *all* 3D documentation under
  `docs/parked-3d/`; one 2D policy everywhere.
- **D-PURGE** — **nothing 3D on `main`.** The fenced/filtered 3D source, its vendored deps, tests,
  goldens, editor TUs and template scripts are deleted; the `COSMIC_2D_ONLY` fences are removed. The full
  tree stays on `engine-3d` (`0e8894b`) + tag `cosmic-pre-2d-2026-09-16`. This supersedes the original
  consolidation brief's "gate, don't delete".
- **D-LIVE** — editing C++ while Starforge is open updates the running logic: save → auto-build → module
  swap → Play auto-resumes on the same flow state with the DataBus preserved. The swap costs one build;
  in-memory service state not on the bus is lost (documented pattern).
- **D-LINKS** — every logic-bearing thing (screen script, service, hosted panel, bound widget, button
  signal) offers **Open source** and **Reveal in Explorer** in the editor.
- **D-TOKENS** — one fresh session per work order, prompt pasted from `work-orders/AP-xx.md`, ≤ 6 files
  read before editing, ≤ 40-line report, logs to `evidence/`.
- **D-SHOWCASE** — AP-Q1 produces `docs/showcase/` (captures, feature list, blurb, README strip).
- Carried over unchanged from the stability packet: **D-CPU** (SSE4.2 floor), **D-GPU** (Win10/11 x64,
  GL 4.5, NVIDIA incl. RTX 50), **D-9km** (to-9km deferred), **D-commit** (author `kdadabhoy
  <kdadabhoy28@gmail.com>`, **no AI trailer, the AI never pushes**).

## Waves (11 sessions; never more than three at once)

```
Wave 0  AP-00   packet + KI-39 CI fix                          done in the planning session, on main
Wave 1  AP-05A  3D purge part A (deletions)                     main, alone
        AP-05B  3D purge part B (fence removal)                 main, alone, after AP-05A
        AP-01   engine foundation                               main, alone, after AP-05B
        AP-P1   packaging + acceptance-in-CI                    worktree ap/p1 from the AP-00 commit;
                                                                may run alongside AP-05/AP-01; lands after AP-01
Wave 2  AP-02   widgets                                         worktree ap/02 ┐ branched from post-AP-P1 main;
        AP-04   templates + PendulumLab                         worktree ap/04 ├ pairwise disjoint files;
        AP-D1   docs structure + link checker                   worktree ap/d1 ┘ land in this order
Wave 3  AP-03   editor UX + live loop + source links            worktree ap/03, alone
Wave 4  AP-D2   docs content                                    worktree ap/d2, alone
Wave 5  AP-Q1   integrate, qualify, showcase kit, release report main, alone
```

Landing order into `main`: AP-00 → AP-05A → AP-05B → AP-01 → AP-P1 → AP-02 → AP-04 → AP-D1 → AP-03 →
AP-D2 → AP-Q1.

## Session guide

| # | WO | Prompt | Runs where | Concurrent with | Model | Effort |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | AP-00 | `work-orders/AP-00.md` | `main` | — | Fable 5.1 — **done** | max |
| 2 | AP-05A | `work-orders/AP-05.md` §Part A | `main`, alone | AP-P1 | Fable 5.1 (`claude-fable-5-1`) | high |
| 3 | AP-05B | `work-orders/AP-05.md` §Part B | `main`, alone | AP-P1 | Fable 5.1 | max |
| 4 | AP-01 | `work-orders/AP-01.md` | `main`, alone | AP-P1 | Fable 5.1 | max |
| 5 | AP-P1 | `work-orders/AP-P1.md` | worktree `ap/p1` | AP-05/AP-01 | Opus 5 (`claude-opus-5`) | high |
| 6 | AP-02 | `work-orders/AP-02.md` | worktree `ap/02` | AP-04, AP-D1 | Fable 5.1 | xhigh |
| 7 | AP-04 | `work-orders/AP-04.md` | worktree `ap/04` | AP-02, AP-D1 | Opus 5 | high |
| 8 | AP-D1 | `work-orders/AP-D1.md` | worktree `ap/d1` | AP-02, AP-04 | Opus 5 | high |
| 9 | AP-03 | `work-orders/AP-03.md` | worktree `ap/03`, alone | — | Fable 5.1 | max |
| 10 | AP-D2 | `work-orders/AP-D2.md` | worktree `ap/d2`, alone | — | Opus 5 (Sonnet 5 only for chapter prose) | high |
| 11 | AP-Q1 | `work-orders/AP-Q1.md` | `main`, alone | — | Fable 5.1 | xhigh |

Effort is the Claude Code effort setting (the session effort control or `/effort`): **max** for engine and editor
architecture, **xhigh** for rendering and final qualification, **high** for scripted moves, content and
packaging. Never below high on this campaign.

How to run one: open a fresh Claude Code session on `C:\dev\Cosmic` (or on the lane's worktree, see
`work-orders/README.md` rule L1), paste the `~~~text` block from the WO file, nothing else. The prompt
tells the session which files to read. When the session reports done, run the "Land" checklist at the
end of the prompt (or hand it to the next serial session). Kaden pushes `main` when he wants CI to run.

## What is deliberately deferred (not in any work order)

Link/transport abstraction for CAN / I2C / UDP with a COBS command codec (`serial/Framing.h` and
`SerialLink::Write` are ready but have no production consumer); `Config::Save`; "run services while
editing"; flow `Fade` transitions; additive under-scene rendering for overlays; `UiText.Wrap`; GIF/MP4
export and the other A1–A5 additions from the original brief. Each is listed in the roadmap v5 that
AP-D1 writes, with the contract section it would extend.
