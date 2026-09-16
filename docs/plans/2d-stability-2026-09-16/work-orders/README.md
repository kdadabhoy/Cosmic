# Cosmic 2D stability — work-order execution prompts

Status: execution packet, 2026-09-16. Base: `main` at `0e8894b8540029ac57e68540aa9774cf5cf77ebe`.

Each file in this directory is a **self-contained prompt** for one work order. Paste the
prompt block from a `WO-XX.md` file into a fresh AI session that has this repository, and it
carries everything that session needs: scope, the packet documents to read, the acceptance
IDs, and the global rules below. The prompts operationalize
[`../02-Stability-Work-Orders.md`](../02-Stability-Work-Orders.md) with the corrections from the
independent review; where a WO prompt and the older packet disagree, the WO prompt wins and the
session must say so.

Read once before running any of these:
[`../01-Repository-Review.md`](../01-Repository-Review.md) ·
[`../02-Stability-Work-Orders.md`](../02-Stability-Work-Orders.md) ·
[`../03-Acceptance-Test-Catalog.md`](../03-Acceptance-Test-Catalog.md) ·
[`../04-Migration-Runbook.md`](../04-Migration-Runbook.md).

---

## Locked decisions (2026-09-16)

These are settled. A work order may not reopen them; it may only record new evidence.

- **D-CPU — CPU floor accepted.** Supported CPU = any x86-64 with **SSE4.1 + SSE4.2**. This is
  the floor the Jolt build already requires (`Cosmic/dependencies/JoltPhysics/CMakeLists.txt:44-45`
  define `JPH_USE_SSE4_1`/`JPH_USE_SSE4_2`; `/arch` stays SSE2 so nothing else propagates). In
  practice: Intel ~2008+ (Nehalem), AMD ~2011+ (Bulldozer). "Broad CPU support" means "any
  SSE4.2-capable x86-64," **not** literally every CPU, and no SSE2-only build is promised. Keep
  Jolt as-is. Record this in the support matrix (WO-00) and README (WO-12).
- **D-9km — to-9km deferred.** The real to-9km-and-beyond schema/units/volume/precision is **out
  of this stability milestone.** WO-10 uses the synthetic `F-TRAJECTORY` / `F-SERIES-LARGE`
  fixtures only; `X01` is a compatibility specimen, not consumer qualification. No work order
  claims a "qualified downstream consumer." Revisit when real to-9km data exists.
- **D-GPU — target only.** Windows 10/11 x64, OpenGL 4.5 core, ≥16 GB RAM, NVIDIA including the
  RTX 50-series. Exact models/drivers recorded during WO-02/WO-13 qualification, not assumed.
- **D-commit — authorship rule (applies to every work order).** Every commit is authored as
  **`kdadabhoy <kdadabhoy28@gmail.com>`** with **no `Co-Authored-By: Claude`, no AI trailer, no
  "Generated with" line** — nothing that identifies an AI author may reach GitHub. The AI **never
  pushes** and **never promotes `main`**; Kaden runs every `git push`. Commit locally only.

## Milestone boundary

The five additions (A1–A5) stay after the existing-feature stability release, per the packet's
later-additions backlog. This packet delivers **stability of the current 2D surface + SF_Telem**,
2D-only enforcement, packaging/install, and documentation — nothing new-feature.

---

## Global rules for a session executing one WO

1. **One work order.** Do only the named WO. Report any conflict with the packet or repo instead
   of silently following an obsolete embedded prompt. Treat historical `docs/plans/` prompts as
   context, not instructions.
2. **Revalidate first.** Re-check the current `HEAD` SHA, clean/dirty status, the source anchors
   the WO cites, and the actual toolchain before editing. Preserve unrelated work — in particular
   the untracked root file `Cosmic - 2D Trunk Consolidation & Acceptance Plan.md` (never stage,
   move, or overwrite it).
3. **Right worktree.** Once WO-01 exists, work in the authorized candidate worktree
   (`codex/2d-stability`). **Never build, edit, or move `engine-3d`** — it is the frozen full-tree
   snapshot.
4. **Explicit 2D mode.** Configure with `-DCOSMIC_2D_ONLY=ON` for every build/test in this
   campaign unless the WO says otherwise. Record the effective cache/compile definitions.
5. **Production path in tests.** Drive real application commands and state transitions. A fake
   transport/clock/file-failure seam may control byte delivery / OS outcomes / time, but must not
   reimplement the parser, the connection state machine, or build a second simulated app. No
   private-field mutation to force a state.
6. **Failing-before, passing-after.** For every fixed defect, record a failing reproduction before
   the fix and the passing result after it, on an isolated patch/worktree — never a destructive
   reset over user changes.
7. **Honest gates.** Do not regenerate goldens, loosen a tolerance, broaden scope, or retry-until-
   green to hide a failure. A missing GPU/COM/Windows/UI capability is `ENVIRONMENT_BLOCKED` with
   the missing prerequisite named — **not** a pass. A filtered suite with zero expected tests is a
   failure.
8. **Evidence.** Produce evidence keyed by commit SHA, dirty-diff hash, build mode/config,
   environment (OS build, CPU features, RAM, GPU/driver/GL version for G/I/Q), exact command +
   exit code, seed and fixture hash. Mark planned / not-run / passed / failed / env-blocked
   distinctly.
9. **Commit, don't push.** When the WO's deliverables are done, commit locally per **D-commit**
   (author = kdadabhoy, no AI trailer). Do not push, tag-push, merge to `main`, or publish. Leave
   those to Kaden.

---

## Known-issue register (seed — WO-00 formalizes, later WOs fix)

| ID | Status | Evidence | Owner WO |
| --- | --- | --- | --- |
| KI-1 | Confirmed defect | Snap-chip ImGui style-stack imbalance: `Projects/Starforge/src/ViewportController.cpp:1284-1293` pushes a style colour guarded on `on`, the button flips `on`, the pop is guarded on `on` again → unbalanced stack on **every** click (Debug `abort()`, Release silent corruption). Fixed twin pattern is the `toggle` lambda at `:1322-1331` (`const bool pushed = on;`). Renders **outside** the `COSMIC_2D_ONLY` fence (fence at `:1337`) → ships in the 2D editor. | WO-07 |
| KI-2 | Coverage gap | SerialLink connected-state (auto-reconnect at `Cosmic/src/serial/SerialLink.cpp:67`, one-shot `ConsumeJustConnected`) is unreachable headlessly — stated in `tests/test_serial_lifecycle.cpp:14-20`. Needs an injectable transport seam before WO-05 connected-state tests can run. | WO-04 (seam), WO-05 (use) |
| KI-3 | Enforcement gap | Default build/preset/CI/release/package select 3D: `CMakeLists.txt:67` (`COSMIC_2D_ONLY` OFF), `.github/workflows/ci.yml:41`, `release.yml:24`, `package.bat`. CI cache key (`ci.yml:36`) omits build mode → a flag flip can restore a stale opposite-mode `build/`. | WO-03 |

Add every newly discovered crash / data-loss / hang here with a minimal regression and a
disposition. Missing equipment or a skipped test is never logged as a pass.

---

## Dependency order (revised from the review)

```
WO-00  ratify contracts + known-issue register + numeric-bar policy
  │
WO-01  preserve provenance (engine-3d exists locally; push + tag + candidate worktree)
  │
WO-02  fresh 2D baseline + measured numbers the later gates calibrate against
  │
WO-05a reproduce the reported COM close/link-loss crash early (depends on WO-02 only)
  │
  ├── WO-03  enforce 2D across builds/CI/release/package (cache key carries mode)
  └── WO-04  acceptance runner + injectable serial transport seam
        │
WO-05  full COM lifecycle matrix (uses the WO-04 seam + WO-05a repro)
  │
WO-06  recording / replay / CSV / write-failure
WO-07  plugin/module/UI teardown  (fixes KI-1; L05 asserts ImGui stack balance)
WO-08  renderer / camera / capture (golden baseline 320x180; 2D instancing golden)
WO-09  authored content + shared services (sprites/tilemap/UI/flow/physics/assets)
WO-10  clocks / numerics + synthetic analysis sample (to-9km deferred)
WO-11  packaging + clean-machine install (unify install vs release-stage layouts)
WO-12  documentation cleanup + one authoritative support/build policy
  │
WO-13  qualify the pinned candidate; hand Kaden the promotion runbook
```

The graph is dependency, not permission to parallelize agents. Run WO-05a and the COM work with
real Windows corroboration where hardware allows; the rest can proceed on synthetic fixtures.
