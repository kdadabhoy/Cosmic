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
