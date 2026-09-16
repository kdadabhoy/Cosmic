# Cosmic 2D branch migration and verification runbook

Status: planning only. These commands have NOT been run.
Execute mutations only after Kaden authorizes implementation/migration. Retain all existing branches.

This runbook assumes WO-00 has approved the proposed engine-3d snapshot name and codex/2d-stability candidate name. Source preservation is separate from testing the parked 3D engine.

## 1. Revalidate the starting state

From the original checkout:

~~~powershell
Set-Location C:\dev\Cosmic
git status --short
git branch -a
git worktree list
git remote -v
git ls-remote --heads origin
git rev-parse main
git rev-parse origin/main
git rev-list --left-right --count main...origin/SF-Stable
git diff --stat main origin/engine-2d
~~~

Reviewed values:
- main / origin/main / origin/engine-2d: 0e8894b8540029ac57e68540aa9774cf5cf77ebe.
- SF-Stable: fa6ed9fe6910f8a30e5e15561bd2a9b6b8b5d66e.
- main...SF-Stable counts: 75, 0.
- main versus engine-2d tree diff: empty.

If live remote heads or local work differ, refresh the review and approved SHA before proceeding. Fetching later is allowed only within the then-authorized Git scope; use no pruning/deletion. A live remote check is not the same as updating local remote-tracking refs.

The untracked root copy of the original plan belongs to the user. Do not remove it, stash it blindly, stage it incidentally, or overwrite it.

## 2. Preserve current full source before changing supported defaults

After explicit migration authorization, and only if the chosen names do not already exist:

~~~powershell
$snapshotSha = '0e8894b8540029ac57e68540aa9774cf5cf77ebe'
git branch engine-3d $snapshotSha
git rev-parse engine-3d
git rev-parse 'engine-3d^{tree}'
git rev-parse "$snapshotSha^{tree}"
~~~

An existing engine-3d with the same identity is already sufficient; never force-move a different branch of that name.

Recommended immutable provenance, if authorized:

~~~powershell
git tag -a cosmic-pre-2d-2026-09-16 $snapshotSha -m 'Cosmic full-tree snapshot before the 2D-only trunk policy'
~~~

For durable remote preservation, separately authorized normal pushes:

~~~powershell
git push origin refs/heads/engine-3d
git push origin refs/tags/cosmic-pre-2d-2026-09-16
git ls-remote origin refs/heads/engine-3d refs/tags/cosmic-pre-2d-2026-09-16 'refs/tags/cosmic-pre-2d-2026-09-16^{}'
~~~

The annotated tag's own object SHA differs from the commit; verify its peeled ^{} commit. Do not force push or delete any branch. Verify the snapshot is durably recorded before promoting main.

## 3. Isolate outputs using separate source worktrees

Use new, unused directories; inspect existing paths and worktrees first. Never clean an existing directory as a shortcut.

~~~powershell
git worktree add --detach C:\dev\Cosmic-2D-Baseline $snapshotSha
git worktree add -b codex/2d-stability C:\dev\Cosmic-2D-Stability $snapshotSha
~~~

Optional old-SF runtime comparison, when its build/fixture is needed:

~~~powershell
$sfSha = 'fa6ed9fe6910f8a30e5e15561bd2a9b6b8b5d66e'
git worktree add --detach C:\dev\Cosmic-SF-Reference $sfSha
~~~

The baseline worktree builds the current snapshot in 2D mode. It does not develop or qualify 3D. Build SF-Stable using the instructions and capabilities of that older commit; do not copy candidate DLLs into its runtime folder.

Do not put baseline and candidate into two -B directories sharing a source root. The current output directories are anchored under COSMIC_SDK_DIR/build/Runtime and would mix binaries.

## 4. Current verification commands

These commands invoke existing targets/tools. They do not yet provide the proposed lifecycle/soak harness.

Use the supported installed MSVC/CMake toolchain. Resolve cmake.exe and ctest.exe from PATH or the selected Visual Studio installation; record actual versions. The checked-in preset names Visual Studio 18 2026, so do not assume it matches every machine.

Example checked PowerShell invocation:

~~~powershell
$ErrorActionPreference = 'Stop'
function Invoke-Checked {
    param([string]$Exe, [string[]]$Arguments)
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Exe failed with exit code $LASTEXITCODE"
    }
}

$cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
$root = 'C:\dev\Cosmic-2D-Baseline'
$build = Join-Path $root 'build'

Invoke-Checked $cmake @(
    '-S', $root, '-B', $build, '-A', 'x64',
    '-DCOSMIC_2D_ONLY=ON',
    '-DCOSMIC_WITH_JOLT=ON',
    '-DCOSMIC_BUILD_ENGINE_ONLY=OFF',
    '-DCOSMIC_BUILD_TESTS=ON',
    '-DCOSMIC_BUILD_RENDER_TESTS=OFF'
)
Invoke-Checked $cmake @('--build', $build, '--config', 'Debug', '--parallel')
Invoke-Checked $cmake @('--build', $build, '--config', 'Release', '--parallel')

Push-Location (Join-Path $build 'Runtime\Debug')
try {
    Invoke-Checked '.\CosmicTests.exe' @('--list-test-cases')
    Invoke-Checked '.\CosmicTests.exe' @('--reporters=console', '--no-intro')
} finally { Pop-Location }

Push-Location (Join-Path $build 'Runtime\Release')
try {
    Invoke-Checked '.\CosmicTests.exe' @('--list-test-cases')
    Invoke-Checked '.\CosmicTests.exe' @('--reporters=console', '--no-intro')
} finally { Pop-Location }
~~~

Record the effective cache/compile definitions and actual discovered test list. A caller outside this sample must enforce the timeout; the commands above alone do not stop a hung test.

From the source root, run the existing checks and verify exit codes:

~~~powershell
Push-Location $root
try {
    & .\tests\check_gl_conformance.ps1
    if ($LASTEXITCODE -ne 0) { throw 'GL conformance check failed' }
    & .\tests\check_docs_coverage.ps1
    if ($LASTEXITCODE -ne 0) { throw 'Documentation coverage check failed' }
} finally { Pop-Location }
~~~

Run scripts with the supported PowerShell version; capture terminating errors as failures too. Do not change machine execution policy to bypass a configuration failure.

For GPU tests, on an appropriate desktop GPU runner:

~~~powershell
Invoke-Checked $cmake @(
    '-S', $root, '-B', $build, '-A', 'x64',
    '-DCOSMIC_2D_ONLY=ON',
    '-DCOSMIC_BUILD_TESTS=ON',
    '-DCOSMIC_BUILD_RENDER_TESTS=ON'
)
Invoke-Checked $cmake @('--build', $build, '--config', 'Debug', '--target', 'CosmicRenderTests', '--parallel')
Invoke-Checked $cmake @('--build', $build, '--config', 'Release', '--target', 'CosmicRenderTests', '--parallel')
Invoke-Checked (Join-Path $build 'Runtime\Debug\CosmicRenderTests.exe') @('--reporters=console', '--no-intro')
Invoke-Checked (Join-Path $build 'Runtime\Release\CosmicRenderTests.exe') @('--reporters=console', '--no-intro')
~~~

Acceptance must first ensure COSMIC_UPDATE_GOLDENS is unset/false, forbid --update-goldens, and compare tracked golden hashes before/after. The current missing-golden path writes a file and fails: the file's appearance is not approval of a new reference image.

Apply equivalent commands to the candidate root. Do not use the pausing interactive build batch files as unattended runners. Additional harness/profile commands become valid only once WO-04 implements and documents them.

## 5. Make controlled changes on the candidate

Follow WO-03 through WO-12, with small reviewed changes and failure evidence. There is no prescribed cherry-pick: every known donor is already in main.

Keep a ledger:
- Exact files and behavior changed.
- Retained requirement/test IDs.
- Intentional differences from SF-Stable for malformed/unsupported inputs.
- New or fixed tests, failing-before proof and current results.
- Documentation changes and version/ABI/file-format implications.

Do not merge SF-Stable over main or copy its entire serial/telemetry directories. Do not require engine-2d to follow future commits. It remains a preserved historical branch.

## 6. Qualify the final candidate

Run WO-13 after the final candidate changes. Keep results tied to an exact commit/diff, not merely to a branch name. At least the user-reported COM regressions must have real Windows corroboration, and both Windows 10/11 must have recorded qualification.

CI for the candidate/PR must explicitly test the 2D mode. Local-only GPU/serial evidence can gate a release through a reviewed report when no dedicated runner exists; missing evidence cannot be relabeled green.

Do not publish or install onto the user's production SF setup as part of the disposable clean-machine qualification.

## 7. Promote main only when separately authorized

Revalidate local/remote main and working-tree status. If main changed since the pinned base, integrate the new changes into the candidate, review the resulting diff and rerun affected gates; a history change cannot reuse stale candidate evidence silently.

With a committed, approved fast-forward candidate and clean applicable tracked work:

~~~powershell
Set-Location C:\dev\Cosmic
git switch main
git merge --ff-only codex/2d-stability
git rev-parse main
~~~

If ff-only fails, stop and inspect the divergence. Do not reset main or force a merge. Push only when explicitly authorized:

~~~powershell
git push origin refs/heads/main
git ls-remote --heads origin
~~~

Verify the pushed SHA, required 2D CI, preserved branch SHAs, package hashes and final documentation. Release publication remains a separate action.

## 8. Recovery

Before promotion, keep main untouched and repair or pause the candidate. No branch deletion is required.

After promotion, prefer deploying the previous qualified 2D artifact and making a reviewed revert/fix commit on main. Do not rewrite history or move the engine-3d snapshot. If no stable 2D artifact exists yet, keep the known-working SF reference available while investigating; do not claim it qualifies all new engine functionality.

Retain the snapshot, SF-Stable, old branch refs and acceptance artifacts. Worktree cleanup is optional later and must check for user changes and resolved paths before any removal.
