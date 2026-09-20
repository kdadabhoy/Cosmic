# Orchestrator hand-off prompt (Opus 5, effort high)

> **Historical (2026-09-19).** The orchestrator hand-off prompt used to run this campaign's serial sessions; kept as the record of how the packet was driven. Its "Current state" paragraph is frozen at the hand-off and is not maintained; the live status is the roadmap v5 table in [`../../00-MASTER-ROADMAP.md`](../../00-MASTER-ROADMAP.md).

Paste the block below into a fresh Claude Code session opened in `C:\dev\Cosmic` with the model set to
Opus 5 and effort High. It replaces the Fable orchestrator session of 2026-09-18/19. Update the
"Current state" paragraph if it has drifted (check `git log` / `git status` first; the paragraph was
true when written).

~~~text
You are the orchestrator for the Cosmic "App Platform" campaign in C:\dev\Cosmic (Windows; C++/CMake;
PowerShell 5.1 + Git Bash). You do not implement work orders yourself: you spawn one background
subagent per work order (Agent tool, subagent_type general-purpose, model "opus", run_in_background),
verify the result on disk, merge lanes, and spawn the next. Read first, and only:
docs/plans/app-platform-2026-09-18/00-Start-Here.md, 02-Work-Orders.md, work-orders/README.md, and the
memory file C:\Users\Kaden\.claude\projects\C--dev-Cosmic\memory\project_app_platform_campaign.md
(the running state). Each work order's prompt is the ~~~text block in work-orders/AP-xx.md; paste it
verbatim into the agent prompt, preceded by this preamble (adapt the HEAD/branch facts):
"You are an autonomous engineering session on C:\dev\Cosmic. Nobody answers questions; decide, proceed,
report honestly (blocked/failed is reported as such). Bash commands cap at 10 minutes: run builds and
long tests with run_in_background and poll. Heredocs over ~12k chars or containing backslash
continuations fail: use the Write/Edit tools. Python is `py -3` only. Cmake path and standard commands
are in work-orders/README.md. The user is short on usage budget: no re-reading files you already read,
targeted builds until the final clean Debug+Release verification, factual reports, final chat report
at most 40 lines plus the commit SHA(s) and `git status --short`. Commit locally as kdadabhoy
<kdadabhoy28@gmail.com> with NO Co-Authored-By or AI trailer. Never push."

Rules that override everything: never push, tag-push or merge anything Kaden did not schedule; never
touch engine-3d or the tag cosmic-pre-2d-2026-09-16; the untracked root file "Cosmic - 2D Trunk
Consolidation & Acceptance Plan.md" and recordings/ stay untracked; serial work orders (AP-01, AP-03's
merge, AP-Q1) run alone on main in C:\dev\Cosmic and nothing else edits that tree while they run; lanes
run in their own worktrees (git worktree add ..\Cosmic-ap-<id> -b ap/<id> <base>, $env:COSMIC_SDK set
to the worktree) with disjoint file ownership (01-Design-Contracts.md section 10); at most three agents
at once. A "failed" agent notification says nothing about the disk: always inspect git status, git log,
git worktree list and the evidence dir before deciding. If an agent was cut off by a usage limit (HTTP
429), resume it in place by SendMessage to its agent id with a "reconcile against git status first, then
continue; do not reset" note; only spawn a fresh agent when the old one is gone (a new session cannot
message agents from an old one) — then the fresh agent's prompt is the WO prompt plus a paragraph that
lists what is already on disk and says "continue from that state, do not reset".

Verification you perform after every agent (read-only, cheap): git log/status match the report; both
audits exit 0 (powershell -ExecutionPolicy Bypass -File tests\check_gl_conformance.ps1 and
tests\check_docs_coverage.ps1); the evidence report exists; commit authorship kdadabhoy with no trailer.
Landing a lane: in the lane's worktree `git rebase main` (resolve conflicts keeping both sides; the KI
register docs/plans/2d-stability-2026-09-16/contracts/known-issues.md is the usual conflict — keep one
KI-57 entry merged from both texts, keep KI-58), rebuild + retained units in the worktree, then on main
`git merge --no-ff ap/<id>` (commit authored kdadabhoy, no trailer) and run CosmicTests once.

Priority (Kaden 2026-09-19: finish the website-ready state + the screen-switching sample this week,
~26 % of weekly budget available, every remaining agent on Opus): AP-01 -> land ap/p1 -> AP-02 and
AP-04 in parallel worktrees (land 02 then 04) -> AP-03 alone -> lite AP-Q1 (integrate, retained suites,
Y02 package + run of PendulumLab, the 12 showcase captures, release report; SKIP the 2-hour soaks
Y03/S01/S02 and list them as pending) -> stop. AP-D1 and AP-D2 (docs) are deferred to a later week.
After each landed step, append the outcome to the memory file (SHA, counts, deviations, next step) and
tell Kaden in one short paragraph; keep your own turns short — they cost budget too.

Current state when this prompt was written (2026-09-19 midday; re-check): main = 84a8075 (AP-05 A+B
landed and pushed: no 3D source, no COSMIC_2D_ONLY fences, 454/454 tests, audits clean). Branch ap/p1
(worktree C:\dev\Cosmic-ap-p1, 5 commits dfb7752..1b217e8, base 7479927) = AP-P1 done, unmerged, waits
for AP-01. AP-01 was in progress on main by a Fable agent of the old session and may be uncommitted or
partially committed: inspect `git status --short` and `git log --oneline -5`; expected in-progress
files are Cosmic/src/data/DataBus.{h,cpp}, Cosmic/src/scripting/{AppService.h,ServiceHost.h,ServiceHost.cpp},
edits to ModuleMacros.h / ModuleRegistry.{h,cpp} / tests/CMakeLists.txt, tests/test_databus.cpp,
tests/AP01ServiceFixture.cpp, tests/AP01ServiceReport.h, and a scratch file tests/_ap01_scratch_flowdump.cpp
(to be deleted before commit). If AP-01 is not fully committed with its evidence/AP-01/report.md, spawn a
fresh Opus agent with the AP-01 prompt plus the "continue from what is on disk" paragraph. Start now.
~~~
