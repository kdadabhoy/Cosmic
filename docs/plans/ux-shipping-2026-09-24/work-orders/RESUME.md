# UX & Shipping — resume point (2026-09-24, moving to a new PC)

Kaden stopped the campaign on the evening of 2026-09-24 (~21:50), about 1.5 h into wave 1, to move the repository to a
new PC. **Nothing from wave 1 has landed on `main`.** The three wave-1 lanes are committed on their branches (WIP
where unfinished), each with an `evidence/UX-xx/HANDOFF.md`. The orchestrator on the new PC starts here, then
follows [`ORCHESTRATOR.md`](ORCHESTRATOR.md) as before.

## State

`main` = `6021822` (UX-00 packet) → `2afe37c` (D-SOAKS: soaks postponed to
[`../../TESTING-PLAN.md`](../../TESTING-PLAN.md)) → the commit that adds this file. Tags unchanged
(`cosmic-pre-2d-2026-09-16`, `cosmic-app-platform-g5-2026-09-20`, both already on origin).

| Lane | Branch | Tip | Base | State | Handoff |
| --- | --- | --- | --- | --- | --- |
| UX-01 | `ux/01` | `d2aae20` | `2afe37c` (already rebased) | all six fixes committed; `ux01-editor` 5/5 PASSED Debug + Release; report.md, retained-run verdicts and CosmicTests counts missing; agent cut off (usage limit) | `evidence/UX-01/HANDOFF.md` (on `ux/01` only) |
| UX-02 | `ux/02` | `9667c84` | `6021822` | Phase A nearly done: 530/0/14 both configs; ED01/02/03/05 PASS both configs, ED04 fix committed but not re-run; retained runs partly done; report.md missing; Phase B pending | `evidence/UX-02/HANDOFF.md` (on `ux/02` only) |
| UX-03 | `ux/03` | `1aabb8f` | `6021822` | one orchestrator WIP commit of the agent's whole tree (agent cut off before its first commit); `ux03-launcher` 3/3 PASSED Debug + Release; 3 retained Release failures under load to re-run; report.md missing; Phase B pending | `evidence/UX-03/HANDOFF.md` (on `ux/03` only) |

KI numbers were **pre-allocated by the orchestrator** so the lanes would not collide (this replaced "next free
number" for wave 1): UX-01 **KI-66..71**, UX-02 **KI-72..76**, UX-03 **KI-77**, extras "KI-78 or above". All three lanes
then registered an extra as **KI-78** (a collision to fix at landing, lowest landing first):

| On branch | Title | Final number |
| --- | --- | --- |
| ux/01 KI-78 | the Screens panel is docked by no built-in preset (open) | **KI-78** (UX-01 lands first) |
| ux/02 KI-78 | a hot-reload build finishing after a project switch loads the old module and clears the new dirty flag (open) | **KI-79** |
| ux/02 KI-79 | `SaveScene` returns true when the write fails (open) | **KI-80** |
| ux/03 KI-77 | the Launcher lists the test fixtures | KI-77 |
| ux/03 KI-78 | the homescreen project card draws `thumb.png` upside down | **KI-81** |

UX-02 renumbers its two at its rebase onto UX-01, UX-03 its KI-78 at its rebase onto UX-02 (grep the evidence and
commit messages of the lane for the old number too). After wave 1 the register runs gap-free KI-66..81 and
`README.md`'s "next entry" line becomes **KI-82** (it still says KI-66 on `main`).

Gotcha found tonight: sub-agents were **refused writing `evidence/UX-xx/report.md` with the Write tool** (UX-02 hit it;
`HANDOFF.md` was allowed). Tell each lane agent to write its report through Bash, or write it as the orchestrator
from the agent's final message. Retained manifests run while three lanes build can fail on load (MSB6003, a scene
save rename race) — re-run such a failure alone before registering a KI.

Open orchestrator TODOs: `01-Contracts.md` §12 acceptance index (≈ line 324) still lists the soaks under UX-Q1 — fix
it to point at TESTING-PLAN.md after wave 1 lands (UX-02/03 edit that file); README next-free KI as above.

## How wave 1 was run (keep these adaptations when resuming)

- The orchestrator creates each lane worktree itself (`git worktree add build\_lanes\ux-<id> ux/<id>`), and the
  agent's prompt says to skip the `git worktree add` step.
- Agent prompt = the ORCHESTRATOR.md preamble + an "Orchestrator notes" block + the WO's `~~~text` block verbatim.
  Notes used: work only in your worktree; set `COSMIC_SDK` to the worktree in every command (shell state does not
  persist); the pre-allocated KI numbers above; `--parallel 4` per build when three lanes compile at once (16
  threads / 31 GB on the old PC); isolate temp/prefs/projects dirs under the worktree's `build\_temp` and kill only
  your own processes; never merge or remove worktrees.
- UX-02 and UX-03 run **two-phase**: Phase A = the whole WO committed against the base; if their predecessors are
  not on `main` yet they stop "ready for rebase"; the orchestrator messages them (or, on the new PC, prompts a fresh
  agent) for Phase B = the prompt's Land (L2) step (`git rebase main`, post-rebase items, rebuild, rerun, commit).
  UX-02's post-rebase items: `m_Editors.AnyDirty()` in the unsaved-changes test; UX-01's
  `FlowEditor::HarnessSelection` confirming the transition in ED03.

## New-PC prerequisites

- Windows 10/11 x64; **Visual Studio 2026 (18) Community** with "Desktop development with C++" (the bundled
  `cmake.exe` path in [`README.md`](README.md) must exist — adjust the path there if the edition differs); Git for
  Windows (Git Bash); Python 3 via the `py` launcher; Windows PowerShell 5.1.
- A GPU with OpenGL 4.x drivers (render tests, the editor self-tests).
- Wave 2 (UX-04): **Inno Setup 6** (`ISCC.exe`) to build `Starforge-Setup-<ver>.exe`, otherwise SD02 is
  `ENVIRONMENT_BLOCKED`.
- Wave 3 (UX-D1): guide pictures are taken at **2560x1440, 100 % scaling**; the Claude desktop app with
  computer-use for the shots the capture driver cannot reach.
- Optional: the Claude memory folder copied from the old PC (below).

## Steps on the new PC (Kaden)

1. On the **old** PC, before deleting anything: push (`git push origin main ux/01 ux/02 ux/03`) and check
   `git ls-remote origin main ux/01 ux/02 ux/03` shows the same SHAs as the table above.
2. Optional: copy the folder `C:\Users\Kaden\.claude\projects\C--dev-Cosmic\memory\` (every file, including
   `MEMORY.md`) to the same path on the new PC. Clone to `C:\dev\Cosmic` so the `C--dev-Cosmic` key matches.
3. `git clone https://github.com/kdadabhoy/Cosmic.git C:\dev\Cosmic`, then in `C:\dev\Cosmic`:
   `git branch ux/01 origin/ux/01`, `git branch ux/02 origin/ux/02`, `git branch ux/03 origin/ux/03`.
4. Start the orchestrator session in `C:\dev\Cosmic` with the restart prompt Kaden was given (it points here).

## What the resuming orchestrator does

1. Re-check: `git log --oneline -3`, the lane branch tips against the table, `git status --short` clean.
2. Recreate the worktrees: `git worktree add build\_lanes\ux-0N ux/0N` (N = 1, 2, 3). `build\` is gitignored, so every
   lane needs a fresh configure + build (the old PC's builds are gone).
3. For each lane read its `HANDOFF.md` (≤ 40 lines) and spawn a **fresh** agent (the old agents do not exist on the
   new PC): its prompt = the preamble + the orchestrator notes above + the WO `~~~text` block verbatim + a paragraph
   "already on disk: <the HANDOFF summary>; continue from that state, do not reset; the base is still `6021822`,
   `main` has moved only by docs commits".
4. Land 01 → 02 → 03 per ORCHESTRATOR.md (verify, rebase, rebuild, `git merge --no-ff`, CosmicTests both configs,
   golden hashes into `evidence/UX-Q1/golden-hashes.txt`), then waves 2–5 as planned. Soaks are **not** in the
   campaign (D-SOAKS); UX-Q1 runs the quick legs only.
