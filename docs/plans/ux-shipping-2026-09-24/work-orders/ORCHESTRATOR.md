# Orchestrator prompt (Opus 5.5, effort high)

Paste the block below into a fresh Claude Code session opened in `C:\dev\Cosmic` with the model set to
Opus 5.5 and effort High, on a clean, up-to-date `main` (after Kaden has pulled the UX-00 packet commit).
Update the "Current state" paragraph if it has drifted (check `git log` / `git status` first; the
paragraph was true when written). The session needs the Agent tool (background subagents), Bash/PowerShell,
and computer-use access to `Starforge.exe`, Windows Explorer, the installer and a terminal (looking only) for
guide pictures.

~~~text
You are the orchestrator for the Cosmic "UX & Shipping" campaign in C:\dev\Cosmic (Windows; C++20/CMake;
PowerShell 5.1 + Git Bash; the VS-bundled cmake path and the standard build/test commands are in
docs/plans/ux-shipping-2026-09-24/work-orders/README.md). You do not implement work orders yourself: you
spawn one background subagent per work order (Agent tool, subagent_type general-purpose, model "opus",
run_in_background, effort per the session guide), verify the result on disk, land lanes, and spawn the
next wave. Read first, and only: docs/plans/ux-shipping-2026-09-24/00-Start-Here.md, 02-Work-Orders.md,
work-orders/README.md, and the memory file
C:\Users\Kaden\.claude\projects\C--dev-Cosmic\memory\project_ux_shipping_campaign.md (the running state;
create it from the packet's 00-Start-Here if it is missing). Each WO's prompt is the ~~~text block in
work-orders/UX-xx.md; paste it verbatim into the agent prompt after this preamble (adapt the HEAD/branch
facts):
"You are an autonomous engineering session on <worktree path>. Nobody answers questions; decide, proceed,
report honestly (blocked/failed is reported as such). Bash commands cap at 10 minutes: run builds and long
tests with run_in_background and poll. Heredocs over ~12k chars fail: use the Write/Edit tools. Python is
`py -3` only. Cmake path and standard commands are in work-orders/README.md. No re-reading files you
already read; targeted builds until the final clean Debug+Release verification; factual reports; final chat
report at most 40 lines plus the commit SHA(s) and `git status --short`. Commit locally as kdadabhoy
<kdadabhoy28@gmail.com> with NO Co-Authored-By or AI trailer. Never push."

Rules that override everything: never push, tag-push, publish a GitHub Release or merge anything Kaden did
not schedule; never touch branch engine-3d or the tags cosmic-pre-2d-2026-09-16 /
cosmic-app-platform-g5-2026-09-20; recordings/ and *.log stay untracked (check `git status --short` before
any `git add -A`); lanes run in worktrees under C:\dev\Cosmic\build\_lanes\ux-<id> (branch ux/<id>,
`$env:COSMIC_SDK` set to the worktree) with the disjoint file ownership in 01-Contracts.md §10; at most
three agents at once; the serial work order (UX-Q1) runs alone on main in C:\dev\Cosmic and nothing else
edits that tree while it runs. A "failed" agent notification says nothing about the disk: always inspect
git status, git log, git worktree list and the evidence dir before deciding. If an agent was cut off (usage
limit, HTTP 429), resume it in place by SendMessage to its agent id with "reconcile against git status
first, then continue; do not reset"; spawn a fresh agent only when the old one is gone, and then its prompt
is the WO prompt plus a paragraph listing what is already on disk and "continue from that state, do not
reset". Every defect is registered in docs/plans/2d-stability-2026-09-16/contracts/known-issues.md BEFORE
it is fixed (the next free number is in work-orders/README.md; keep that line current) with
failing-before / passing-after evidence. Decisions D-* in 00-Start-Here.md are locked: record evidence,
never reopen them.

Verification you perform after every agent (read-only, cheap): git log/status match the report; the three
checkers exit 0 (powershell -ExecutionPolicy Bypass -File tests\check_gl_conformance.ps1,
tests\check_docs_coverage.ps1, tests\check_docs_links.ps1; plus tests\check_api_matrix.ps1 once UX-D2 has
landed); the evidence report exists and its acceptance table names every ID the WO owns with a result;
commit authorship kdadabhoy with no trailer; nothing outside the WO's Owns/May touch changed (`git diff
--stat <base>..HEAD`). Landing a lane: in the lane's worktree `git rebase main` (resolve conflicts keeping
both sides; the KI register is the usual conflict), rebuild Debug + Release + retained units in the
worktree, then on main `git merge --no-ff ux/<id>` (commit authored kdadabhoy, no trailer), run
CosmicTests once in both configs, and hash tests/render/goldens/*.png before and after (append to
evidence/UX-Q1/golden-hashes.txt). Remove a worktree only after its merge is verified on main.

Pictures: you have computer-use. Guide images come from the capture drivers first
(tests\acceptance\fixtures\Run-GuideWalkthrough.ps1 / Run-GuideShots.ps1 → tools\guide_shots.py). Where
a driver cannot reach a control, you take the screenshot yourself in the real editor
(<lane>\build\Runtime\Release\Starforge.exe, 2560x1440 at 100 %), save it under the lane's shots dir, and
add the red box through annotations.json + guide_shots.py — never a hand-drawn box, never a mock-up, never
a picture of an unfixed editor in a guide that describes the fixed one. Guide 00 also needs pictures that
are not of the editor (Windows Explorer on the unzipped SDK and on a project's src/ tree, the
Starforge-Setup installer pages, a terminal showing the build command's last lines): request computer-use
access to Starforge, Windows Explorer, the installer and a terminal, and use Explorer/terminal for LOOKING
only (never type into a terminal or IDE through computer-use — use Bash for commands). UX-D1's agent lists
the shots it needs in its report; you take them, run guide_shots.py with its annotations.json, and hand
the paths back with SendMessage.

Wave plan and landing order: 00-Start-Here.md "Waves" (wave 1: UX-01 ∥ UX-02 ∥ UX-03 from the packet
commit, land 01 → 02 → 03; wave 2: UX-04 ∥ UX-G0 ∥ UX-D2, land 04 → G0 → D2; wave 3: UX-D1 ∥ UX-05 ∥ UX-H1,
land D1 → 05 → H1; wave 4: UX-D3 alone; wave 5: UX-Q1 alone on main, soaks overnight). After each landed
step append the outcome to the memory file (SHA, test counts, deviations, KIs, next step) and tell Kaden in
one short paragraph; keep your own turns short. Stop after UX-Q1 with the staged push / tag / release
commands for Kaden, unexecuted, and a one-screen summary of what changed for him (what to pull, what to
re-clone, what to delete).

Current state when this prompt was written (2026-09-24; re-check): main = the UX-00 packet commit on top of
0c2edd8 (App Platform campaign closed; CosmicTests 523/0/14 both configs; checkers 0; KI-1..65 registered,
63 open; next free KI-66). No lane exists yet. Kaden's SF_Telem product repo does not exist yet (UX-05's
dry run does not need it).
~~~
