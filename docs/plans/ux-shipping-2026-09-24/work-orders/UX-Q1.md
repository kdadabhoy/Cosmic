# UX-Q1 — Qualify and release: suites, DOC02 from the SDK zip, release report, staged push/tag/release

**Gate:** G5 · **Wave:** 5 (alone) · **Runs:** `main` in `C:\dev\Cosmic` · **Base:** `main` after UX-D3 has landed ·
**Depends on:** every UX work order · **Acceptance:** every catalog ID re-run; owns S03, K02 (re-run), the S01 fake-clock
leg and DG02 executed from the zip · **Model:** Opus 5.5 · **Effort:** xhigh · **Status:** not started

Prove the landed `main` at one pinned SHA: the retained suites and every manifest, the guides followed from the
**SDK zip** rather than the checkout, a refreshed showcase, the release report with the requirement → case → evidence
matrix, and the push / tag / GitHub-release commands staged for Kaden, never executed.

**Soaks postponed (Kaden, 2026-09-24):** "do tests and quick things" — the multi-hour soaks (S01-native, Y03, S02,
N02-drift-2h, T05) and their drivers moved to [`../../TESTING-PLAN.md`](../../TESTING-PLAN.md). UX-Q1 runs only the quick
legs and reports the rest as *not run — deferred to TESTING-PLAN.md*, never as a pass.

**Toolchain (Kaden, 2026-09-25, D-TOOLCHAIN):** MSVC stays the only supported toolchain; the bundled llvm-mingw flavour is
planned in [`../../TOOLCHAIN-PLAN.md`](../../TOOLCHAIN-PLAN.md), not built. UX-Q1 qualifies the friction fixes that
decision added (SD05, H1-E, H1-F, DG03) and states the decision in the release report.

## Copy-paste prompt

~~~text
Execute only UX-Q1 from the Cosmic UX & Shipping packet (docs/plans/ux-shipping-2026-09-24/). Read ONLY:
work-orders/README.md (all); 00-Start-Here.md decision D-TOOLCHAIN; 02-Work-Orders.md (landing order, cross-check);
03-Acceptance-Catalog.md (all);
01-Contracts.md §11-§12; the "Contract deviations" section of each evidence/UX-*/report.md (grep for the heading, do not
read whole reports); ../app-platform-2026-09-18/evidence/AP-Q1/release-report.md §9 and §12 (soak commands, staged-
promotion shape); ../2d-stability-2026-09-16/03-Acceptance-Test-Catalog.md rows T05 (:108), N02 (:192), K02 (:208),
S01-S04 (:221-224); ../app-platform-2026-09-18/03-Acceptance-Catalog.md rows Y02, Y03, DOC05 (:63-72).
Work alone on main in C:\dev\Cosmic at a PINNED SHA; key every result to it; nobody else edits this tree meanwhile.
Anchors below are from 0c2edd8: re-check first (README rule 2).

Do, in order:
1. Verify: git log --first-parent shows UX-00 -> 01 -> 02 -> 03 -> 04 -> G0 -> D2 -> D1 -> 05 -> H1 -> D3; git branch
   --no-merged main lists no ux/*; git worktree list: every lane under build\_lanes merged (L3: list, remove none);
   branch engine-3d and tags cosmic-pre-2d-2026-09-16 / cosmic-app-platform-g5-2026-09-20 unchanged (rev-parse before and
   after). Record the pinned SHA and a clean status.
2. Reconcile: apply every recorded deviation to 01-Contracts.md inline as [UX-xx Dn] notes (one commit); finalize §11
   (status + proving ID per row). Known ones to expect: the zip also carries the license-manifest sources, the installed
   editor keeps its data in its exe dir, the added skip names, New-AppRepo's extra parameters, sdk.toml's toolchain /
   msvc_version keys and the VC++ runtime route (UX-04), the compiler-check strings if UX-H1 had to extend them.
3. Retained suites: CosmicTests Debug + Release (new baseline; >= 523 passed, 0 failed); CosmicRenderTests with the 15
   goldens byte-identical (evidence/UX-Q1/golden-hashes.txt); tests\check_gl_conformance.ps1, check_docs_coverage.ps1,
   check_docs_links.ps1, check_api_matrix.ps1 exit 0; every manifest through the runner - ux01-*, ux02-*, ux03-*,
   ux04-sdk, uxg0-*, ux-d1-*, ux05-consumer, ux-h1-*, pr-*, ap*, wo*, selftest - Release, and Debug where a manifest
   allows; absolute -Manifest, -TempRoot C:\dev\Cosmic\build\_temp\uxq1. Retained manifests rewrite tracked App Platform /
   stability evidence: copy results into evidence/UX-Q1/, then git checkout -- those paths and delete n04-*.bin and
   recordings/ from the root; check git status before any git add. ENVIRONMENT_BLOCKED only with the prerequisite named.
4. SDK zip at the pinned SHA: installer\Stage-Sdk.ps1 -Build -Zip in a clean worktree (git worktree add
   build\_lanes\uxq1-sdk --detach <SHA>); its list must equal tests/acceptance/goldens/ux04-sdk-files.txt (a difference is
   a finding, not a golden to update); record size + SHA-256; build the installer if ISCC exists, else name the blocker.
   SD05 at the pinned SHA (D-TOOLCHAIN): UX-04's Test-PackageImports.ps1 over that zip's build\Runtime\Release and over
   the packages step 6 produces (K02's Stage-AppPackage.ps1 package and the editor package from apq1-y02): zero MISSING,
   the msvcp140.dll version leg, sdk.toml toolchain = "msvc". Where a VM exists, SD04's "missing toolchain" report must be
   H1-E's "C++ compiler not found" message.
5. DG02 = DOC02 from the zip: unzip to $env:TEMP\uxq1-doc02\Cosmic SDK, drop COSMIC_SDK from the child environment,
   follow guide 00 (the zip leg) and guide 01 through the DG02 driver UX-D1 landed, pointed at the unzipped
   build\Runtime\Release. If that driver still pins COSMIC_SDK to the checkout (Run-GuideWalkthrough.ps1:36 did at
   0c2edd8), run UX-04's Run-UX04Sdk.ps1 SD01 leg (the same GuideWalkthroughSelfTest) and say so. Pass = the exported
   exe runs from another directory and closes gracefully. DG01: every entry of docs/guides/images/manifest.json exists
   and names its capture method. DG03: evidence/UX-D1/check_guide00_toolchain.py exits 0, plus the leg only you can run:
   guide 00's two winget one-liners are byte-equal to the strings in the landed H1-E source (grep BuildRunner/StarforgeApp
   for them), and the step carries UX-H1's compiler-check picture (else a finding). H1-E / H1-F: ux-h1-toolchain and
   ap04-sample (step 3) green.
6. Quick legs only (the soaks are postponed to docs/plans/TESTING-PLAN.md by Kaden, 2026-09-24: do NOT run
   S01-native, Y03, S02, N02-drift-2h or T05 and do NOT write their drivers or JobObject.psm1). Run sequentially:
   - S01 fake-clock leg: wo06 -Profile pr (D05-two-hour, minutes) per release-report §9. Not the -Profile native leg.
   - S03: ../app-platform-2026-09-18/evidence/AP-Q1/Run-S03Determinism.ps1, x5 both configs. K02 (PendulumLab):
     Run-K02PendulumLab.ps1 there, plus apq1-y02 (Y02). Run-K02PendulumLab.ps1:10's -EditorDist default: take the dist
     folder from the Y02 result JSON, not a hard-coded <repo>\dist (after UX-04 an external project packages under its
     own root).
   - In the matrix, S01-native, Y03, S02, N02-drift-2h and T05 are N "not run — deferred to TESTING-PLAN.md (Kaden
     2026-09-24)"; if anything this campaign changed invalidates a command in TESTING-PLAN.md, fix the command there.
7. Every defect: a KI under "the next free number" in work-orders/README.md (never hardcoded) BEFORE the fix, in
   docs/plans/2d-stability-2026-09-16/contracts/known-issues.md; failing-before / passing-after; its own commit; the
   affected manifest re-run. No failed case survives without a KI and a fix or an explicit Kaden decision.
8. Showcase refresh (DOC05, :72): re-capture those of docs/showcase's 12 images whose subject this campaign changed (01
   homescreen + samples/App default, 02 rect gizmo, 03 Screens panel with Scenes, 04 Inspector flow-usage line, 05
   PendulumLab flow graph routing, 09 Live chip) from the pinned Release build, <= 1 MB each; update the captions in
   docs/showcase/README.md and the README.md top strip.
9. evidence/UX-Q1/release-report.md: pinned SHA; environment (CPU, GPU/driver/GL, Windows build, toolchain - and the
   decision D-TOOLCHAIN in one paragraph: MSVC (Visual Studio Community 2026 or the Build Tools, "Desktop development with
   C++") is the only supported toolchain for engine, editor and user projects; the bundled llvm-mingw flavour is designed
   in docs/plans/TOOLCHAIN-PLAN.md and not built; the VC++ runtime route UX-04 chose); the
   requirement -> case -> evidence matrix for every ID in 03-Acceptance-Catalog.md plus the retained App Platform and
   stability rows; counts P/B/N/F; blocked cases with prerequisites; KI dispositions (every KI opened this campaign);
   the quick-leg results and the deferred soaks (pointer to TESTING-PLAN.md); zip + installer hashes; the deferred list
   (TOOLCHAIN-PLAN.md included); docs/plans/00-MASTER-ROADMAP.md statuses (own commit); and
   the STAGED promotion, unexecuted:
     git -C C:\dev\Cosmic status --short
     git -C C:\dev\Cosmic log --oneline -25
     git -C C:\dev\Cosmic push origin main
     gh workflow run release.yml -f app_name=Starforge -f <UX-04's sdk input>=true   (optional dry run of the sdk job)
     git -C C:\dev\Cosmic tag -a cosmic-sdk-v<ver> <pinned SHA> -m "Cosmic SDK <ver> (UX & Shipping, evidence/UX-Q1)"
     git -C C:\dev\Cosmic push origin cosmic-sdk-v<ver>   (release.yml `sdk`: zip + Starforge-Setup on the GitHub Release)
   with <ver> = COSMIC_VERSION_STRING (the job refuses any other tag); the CI jobs to expect on the push
   (build-and-test, acceptance-pr, consumer = EX05, the API-matrix step) and what stays ENVIRONMENT_BLOCKED there.
Commit locally as kdadabhoy <kdadabhoy28@gmail.com>, no Co-Authored-By / AI trailer. Never push, tag, tag-push, run a
workflow or publish a release. Report <= 40 lines: matrix summary P/B/N/F, quick-leg verdicts (S01-pr, S03, K02), the
D-TOOLCHAIN verdicts (SD05, H1-E, H1-F, DG03), new KIs, zip and installer hashes, the staged commands verbatim.
~~~

## Files to read first (and nothing else)

The nine in the prompt: README; D-TOOLCHAIN; `02-Work-Orders.md`; `03-Acceptance-Catalog.md`; `01-Contracts.md` §11-§12;
the UX reports' deviation sections; AP-Q1 release report §9/§12; the stability rows; the App Platform Y02/Y03/DOC05 rows.

## Owns / May touch

- **Owns:** `evidence/UX-Q1/**`; `docs/showcase/**`; the `README.md` top strip; `01-Contracts.md` §11 and the recorded
  deviations; `docs/plans/00-MASTER-ROADMAP.md` statuses; command fixes in `docs/plans/TESTING-PLAN.md`.
- **May touch:** code only for a registered KI fix, each its own commit, the file named in the report.

## Scope

- **In:** integration check, contract reconciliation, every suite and manifest, the zip at the pinned SHA, DG01/DG02,
  the quick legs (S01-pr, S03, K02), the D-TOOLCHAIN IDs (SD05, H1-E, H1-F, DG03), KI fixes, showcase refresh, release
  report (stating D-TOOLCHAIN), staged promotion.
- **Out:** the multi-hour soaks and their drivers (postponed to `docs/plans/TESTING-PLAN.md`, Kaden 2026-09-24); any toolchain
  other than MSVC (`docs/plans/TOOLCHAIN-PLAN.md`, D-TOOLCHAIN); new features; loosening any bar or updating a golden/pinned list to pass; pushing, tagging, running a
  workflow, publishing a release; bumping the version (Kaden's call before tagging).

## Deliverables

`evidence/UX-Q1/{release-report.md,golden-hashes.txt}`; refreshed `docs/showcase/**` + README strip; the
reconciliation and roadmap commits; KI entries.

## Done when (DoD)

Every mandatory case passed or honestly blocked; DOC02 executed from the zip; the quick legs run; the five soaks
listed as deferred to TESTING-PLAN.md; no failed case without a KI and a fix or a Kaden decision; showcase refreshed;
the push, tag and release commands staged, not run.

## Rollback

Report-only apart from KI fixes and the reconciliation/roadmap commits, each independently revertible.
