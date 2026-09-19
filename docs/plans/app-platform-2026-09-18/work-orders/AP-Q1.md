# AP-Q1 — Integrate, qualify, showcase kit, release report, staged push

**Gate:** G5 · **Wave:** 5 (alone) · **Runs:** `main` in `C:\dev\Cosmic` · **Base:** `main` after AP-D2 has
landed · **Depends on:** everything · **Acceptance:** S01–S04, Y02, Y03, K02 (PendulumLab), DOC05, the
retained suites, every `ap*` manifest · **Model:** Fable 5.1 · **Status:** not started

The former WO-13 for the new surface: prove the landed `main` at a pinned SHA, run the long tests the
lanes could not, tidy what AP-05 listed, produce the showcase kit for the website, write the release
report, and stage — never run — the push.

## Copy-paste prompt

~~~text
Execute only AP-Q1 from the Cosmic App Platform packet (docs/plans/app-platform-2026-09-18/). Read
work-orders/README.md (all), 02-Work-Orders.md (landing order + cross-check), 03-Acceptance-Catalog.md
(all), 01-Design-Contracts.md section 13 and every evidence/AP-*/report.md "Contract deviations"
section, and the stability packet's ../2d-stability-2026-09-16/03-Acceptance-Test-Catalog.md rows
S01-S04. Work alone on main in C:\dev\Cosmic at a PINNED commit; every result is keyed to that SHA.

Do, in order:
1. Verify the landing order in git log (AP-00 -> 05A -> 05B -> 01 -> P1 -> 02 -> 04 -> D1 -> 03 -> D2),
   that every ap/* branch is merged, no worktree has unmerged commits, and that engine-3d and the tag
   cosmic-pre-2d-2026-09-16 are unchanged. Record the pinned SHA and a clean status.
2. Reconcile the contract: apply every recorded deviation to 01-Design-Contracts.md (one commit),
   finalize section 13 with statuses and the proving IDs.
3. Tidy: delete the "now-dead 3D-only classes" AP-05 listed if nothing references them (grep + build);
   each removal its own commit with the B06 oracle re-run.
4. Full retained suites at the pinned SHA: CosmicTests Debug + Release (record counts as the new
   baseline), CosmicRenderTests (all goldens byte-identical, the ap02 goldens included), both audits, the
   link checker, every wo* and ap* manifest through the runner (Debug and Release where the manifest
   allows). ENVIRONMENT_BLOCKED only with the prerequisite named.
5. Long runs: Y02 (editor packages PendulumLab through the real path; run the staged exe from another
   directory with its self-test), Y03 (2-h PendulumLab soak with the screen-switch/reset cadence,
   memory plateau and drift oracles), S01 (SF_Telem 2-h soak exactly as the stability catalog states;
   the app is unchanged except the user:// paths, so this is a regression check), S02 (AnalysisSample
   2-h), S03 (determinism reruns x5 for the deterministic fixtures incl. F-PENDULUM), K02 for
   PendulumLab. Use Windows job objects and the runner deadlines; never a manual kill counted as pass.
6. Every defect found: register KI-57.. BEFORE fixing, failing-before/passing-after, its own commit,
   the affected manifest re-run.
7. Showcase kit (DOC05): docs/showcase/ with the 12 captures listed in the catalog (use the engine's PNG
   capture where a viewport is involved and Windows screenshots for whole windows; 1920x1080 or the
   window size; <= 1 MB each, optimise), captions, a one-line-per-feature list, a 150-word "what
   Cosmic is" blurb for the website, and a README.md top strip (3-4 images + links) in the root README.
8. evidence/AP-Q1/release-report.md: pinned SHA, environment (CPU features, GPU/driver/GL, Windows
   build), the requirement -> case -> evidence matrix for every ID in both catalogs, counts, blocked
   cases with prerequisites, KI dispositions (KI-1..56 unchanged plus the new ones), package hashes,
   the deferred list, and the staged promotion: the exact  git push origin main  (and tag) commands for
   Kaden, unexecuted, plus the CI checks to expect.

Commit locally as kdadabhoy with no AI trailer. Do not push, tag-push or publish. Return: the matrix
summary (passed / blocked / failed), the new KI list, the showcase file list, and the staged commands.
~~~

## Files to read first (and nothing else)

`work-orders/README.md`; `02-Work-Orders.md`; `03-Acceptance-Catalog.md`; `01-Design-Contracts.md` §13;
the `evidence/AP-*/report.md` deviation sections; the stability S-rows.

## Owns / May touch

See §10 (AP-Q1 row).

## Scope

- **In:** integration verification, qualification, long runs, KI fixes, tidy, showcase kit, release
  report, staged push.
- **Out:** new features; loosening any bar; pushing or publishing.

## Deliverables

`evidence/AP-Q1/{release-report.md,golden-hashes.txt,…}`; `docs/showcase/**`; the README strip; the
contract reconciliation commit; KI entries.

## Done when (DoD)

Every mandatory case passed or honestly blocked; no failed case without a registered KI and a fix or
an explicit Kaden decision; the showcase kit exists; the push is staged, not performed.

## Rollback

Report-only apart from KI fixes and tidy commits, each independently revertible.
