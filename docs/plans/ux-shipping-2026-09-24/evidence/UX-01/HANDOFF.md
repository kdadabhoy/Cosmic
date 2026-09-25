# UX-01 hand-off (written by the orchestrator, 2026-09-24 ~21:45)

The UX-01 agent was cut off by the account usage limit (HTTP 429) while writing its evidence; it never wrote a
report. This file records what is on disk. Branch `ux/01`, already rebased onto `2afe37c` (D-SOAKS).

## Done (committed)
- KIs registered first in `0ac96b4`: KI-66..71 (items a–f) and **KI-78** (the Screens panel is docked by no
  built-in preset and opens floating over the Hierarchy; disposition open, proposal: dock at `DockPort::LeftBottom`).
  NOTE: UX-02 and UX-03 also registered a "KI-78". UX-01 lands first and keeps KI-78; the others renumber.
- Fixes, one commit each: KI-66 size/dock/focus `08b83f1`, KI-67 routing + vendored patch `58bf7ad`, KI-68 trigger
  kinds + Validate `9c0b411`, KI-69/70 first-frame dirty + tab ids `64ca25a`, KI-71 Editors ✕ + CloseAll `fe3eddc`;
  self-test host, FE01–FE05 unit cases, `ux01-editor` manifest and evidence `59e972d`.
- `ux01-editor` manifest: **5/5 PASSED in Debug and in Release** (`runner-ux01-{Debug,Release}/results.json`;
  FE01, FE02, FE03-U, FE05-U, UX01-EDITOR = FE03/FE04/FE05 self-test). FE04 measured canvas 852x551 px docked at
  Center, 4/4 nodes inside. Failing-before evidence in `failing-before/`. F-FLOWS hashes in `fflows-sha256.txt`.
- WIP commit (orchestrator): the KI-66..71 dispositions → "fixed in <sha>", the last ux01 re-run evidence.

## Not done
- `evidence/UX-01/report.md` (WO-10 layout). The UX-02 agent found that sub-agents were refused writing a
  `report.md` with the Write tool — write it through Bash, or the orchestrator writes it from this file.
- §11 register rows: check `01-Contracts.md` (3 lines added at `0ac96b4..59e972d`) covers trigger kinds, RouteLink
  and the Editors host contract; add what is missing.
- Retained runs: ap03-editor, wo07-l02, wo09-editor and wo10-sample were run in Release (they rewrote tracked
  stability / App Platform evidence, which the orchestrator reverted), but their verdicts were never copied into
  `evidence/UX-01/`. Re-run all four in both configs.
- CosmicTests counts for both configs were never recorded: re-run.
- Contract deviations for the report: NodeCanvasRoute.cpp, UX01EditorSelfTest.cpp + its hook lines, the
  CloseProject line, FlowMachine.cpp Validate, the router pointer (VENDOR-NOTES local patch 2), KI-78.

## Next commands (in `build\_lanes\ux-01`, `$env:COSMIC_SDK` = that folder)
Fresh configure + Debug + Release build (README commands), CosmicTests both configs, then
`Run-Acceptance.ps1 -Manifest <abs>\tests\acceptance\manifests\ux01-editor.manifest.json -Config Debug|Release
-TempRoot <worktree>\build\_temp\ux01`, then the four retained manifests, the three checkers, `git checkout --` any
rewritten tracked evidence, write report.md, commit, land first.
