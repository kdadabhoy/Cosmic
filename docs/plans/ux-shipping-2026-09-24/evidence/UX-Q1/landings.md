# UX & Shipping — landing log (orchestrator)

One entry per lane merged into `main`. Golden hashes before/after each merge: [`golden-hashes.txt`](golden-hashes.txt).
Environment: the campaign VM (llvmpipe OpenGL 4.5; see `work-orders/README.md` "Environment: the VMware VM").

## UX-V0 — merge `690d642` (2026-09-26)

- Lane `ux/v0` 33aff09..56cff27 (6 commits, kdadabhoy, no trailer), base `353948b` = main (rebase no-op).
- Verification: checkers (gl_conformance incl. the new GLSL pass, docs_coverage, docs_links) exit 0 on the lane;
  `evidence/UX-V0/report.md` names VM01 PASS, VM02 PASS, VM03 PARTIAL (Release `ap03-editor` failed under
  concurrent lane load: MSB6003 project builds, E03 drag check 2 of 3 runs; `wo07-l05` PASS both configs).
- main after merge: Release build 4 min, Debug 3 min, 0 warnings / 0 errors (`--parallel 4`, another lane building).
- CosmicTests on main: Release 523 passed / 0 failed / 14 skipped (623 s), Debug 523 / 0 / 14 (530 s), under load
  from two lanes; no KI-84 timing failure in either run.
- Goldens: 19 files, hashes unchanged.
- Open: the quiet (no other lane) re-run of `ap03-editor` Release on main; the real-GPU CosmicRenderTests check
  (`evidence/UX-V0/HOST-VERIFY.md`).

## UX-01 — merge `9ecc118` (2026-09-26)

- Lane `ux/01` @ `61fa666` (12 commits, kdadabhoy, no trailer), base `690d642`; main had moved only by the docs-only
  landing record `03acf86` (clean `git merge-tree`), so the lane was not re-rebased (keeps the SHAs its KI entries cite).
- Verification: checkers exit 0 on the lane; `evidence/UX-01/report.md` names FE01, FE02, FE03-U, FE05-U, FE03, FE05
  PASS in both configs and **FE04 ENVIRONMENT_BLOCKED** (VM display 1718x920: window capped at 1738x940, canvas
  737x454 < 800x400; the size-independent checks passed); lane CosmicTests 529 / 0 / 14 both configs; retained
  `ap03-editor`, `wo07-l02`, `wo09-editor`, `wo10-sample` PASS both configs.
- main after merge: configure 0; Release build 2 min, Debug 1 min, 0 warnings / 0 errors. Goldens: 19 files, unchanged.
- Main's own CosmicTests re-run: started under heavy load (two lanes' suites running); result to be appended here.
- Registered at this landing: KI-86 (AP03 / guide self-tests truncate their result JSON on FAIL; owner UX-H1); KI-84
  amended with wo06 D03-nightly. README next entry KI-87.
