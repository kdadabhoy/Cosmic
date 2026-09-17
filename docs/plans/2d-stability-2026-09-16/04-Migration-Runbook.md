# Cosmic 2D branch migration and verification runbook

Status: planning reference. Preservation (§2) is DONE; the campaign is MAIN-ONLY.
Execute mutations only after Kaden authorizes implementation/migration. Retain all existing branches.

> **WORKFLOW UPDATE (Kaden 2026-09-17): MAIN-ONLY.** All WO work happens directly on `main` (commit
> locally; Kaden pushes). There is **no `codex/2d-stability` candidate branch and no second worktree**.
> §3's candidate/baseline worktrees and §7's ff-only merge are **superseded** by the rewritten §3/§7
> below. Preservation is `engine-3d` (pushed) + the `cosmic-pre-2d-2026-09-16` tag.

This runbook assumes WO-00 approved the `engine-3d` snapshot name. Source preservation is separate
from testing the parked 3D engine.

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

> **DONE (2026-09-17):** `engine-3d @ 0e8894b` is created and **pushed to origin**; the annotated tag
> `cosmic-pre-2d-2026-09-16` is created on `0e8894b` (peels to `0e8894b`). Kaden pushes the tag with
> `git push origin refs/tags/cosmic-pre-2d-2026-09-16`. The commands below are the reference for how
> that was done.

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

## 3. Build isolation (main-only)

The campaign is main-only: build the 2D engine directly on `main` (all WO work lives there). **No
candidate/baseline worktree is created.**

One caveat from the review still applies: Cosmic's build output is source-relative (DLLs land under
`COSMIC_SDK_DIR/build/Runtime/<Config>`), so a different `-B` directory alone does **not** prevent
mixed binaries. You therefore only need a *separate throwaway checkout* if you want to build the
**old SF-Stable reference** at the same time as `main` (e.g. the WO-02 baseline comparison):

~~~powershell
$sfSha = 'fa6ed9fe6910f8a30e5e15561bd2a9b6b8b5d66e'
git worktree add --detach C:\dev\Cosmic-SF-Reference $sfSha
~~~

Build SF-Stable using the instructions and capabilities of that older commit; do not copy `main`
DLLs into its runtime folder. Remove the throwaway worktree when the comparison is done. If you do
not need a simultaneous old-reference build, build sequentially on `main` and skip the extra checkout
entirely.

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
$root = 'C:\dev\Cosmic'
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

Run these on `main` (main-only). Do not use the pausing interactive build batch files as unattended runners. Additional harness/profile commands become valid only once WO-04 implements and documents them.

## 5. Make controlled changes on main

Follow WO-03 through WO-12 on `main`, with small reviewed changes and failure evidence, committed locally (Kaden pushes). There is no prescribed cherry-pick: every known donor is already in main.

Keep a ledger:
- Exact files and behavior changed.
- Retained requirement/test IDs.
- Intentional differences from SF-Stable for malformed/unsupported inputs.
- New or fixed tests, failing-before proof and current results.
- Documentation changes and version/ABI/file-format implications.

Do not merge SF-Stable over main or copy its entire serial/telemetry directories. Do not require engine-2d to follow future commits. It remains a preserved historical branch.

## 6. Qualify the final commit

Run WO-13 after the final changes land on `main`. Keep results tied to an exact commit/diff, not merely to a branch name. At least the user-reported COM regressions must have real Windows corroboration, and both Windows 10/11 must have recorded qualification.

CI on `main` must explicitly test the 2D mode. Local-only GPU/serial evidence can gate a release through a reviewed report when no dedicated runner exists; missing evidence cannot be relabeled green.

Do not publish or install onto the user's production SF setup as part of the disposable clean-machine qualification.

## 7. Ship the qualified main commit only when separately authorized

Main-only: there is **no candidate branch to merge**. The WO work already lives on `main` and is
pushed as Kaden goes. "Promotion" for this campaign is simply the milestone hand-off: once WO-13
qualifies `main` at a pinned SHA, Kaden pushes `main` at that SHA (if any local commits remain
unpushed) and verifies the required 2D CI passes on that exact commit.

~~~powershell
Set-Location C:\dev\Cosmic
git rev-parse main            # the qualified SHA from WO-13
git status --short            # clean tracked tree
git push origin refs/heads/main   # Kaden only; skip if already pushed
git ls-remote --heads origin
~~~

The AI never runs the push. Do not reset `main`, rewrite history, or move `engine-3d`. Verify the
pushed SHA, required 2D CI, preserved branch SHAs, package hashes and final documentation. Release
publication remains a separate action.

## 8. Recovery

Because work is on `main`, keep `engine-3d` (and the `cosmic-pre-2d-2026-09-16` tag) untouched as the
rollback point, and repair on `main` with a reviewed revert/fix commit rather than a history rewrite.

If a qualified 2D artifact exists, prefer deploying the previous qualified artifact while
investigating. Do not rewrite history or move the engine-3d snapshot. If no stable 2D artifact exists
yet, keep the known-working SF reference available while investigating; do not claim it qualifies all
new engine functionality.

Retain the snapshot, SF-Stable, old branch refs, the provenance tag and acceptance artifacts.
