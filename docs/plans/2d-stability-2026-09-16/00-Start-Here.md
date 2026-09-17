# Cosmic 2D stability planning packet

Prepared 2026-09-16 against main 0e8894b8540029ac57e68540aa9774cf5cf77ebe.

**Recommendation:** keep current main as the base, preserve its full tree on engine-3d, make the supported trunk reliably 2D-only, and stabilize it before implementing additions.

This packet is planning material. Engine code, tests, build configuration, branches and the original plan were not changed. No current test run or reproduced crash is claimed.

## Read in this order

1. [Repository review and decisions](01-Repository-Review.md) — verified branches, source findings, corrections to the original brief, confirmed preferences and remaining inputs.
2. [Stability requirements and AI work orders](02-Stability-Work-Orders.md) — release gates, WO-00 through WO-13, a copy-paste execution prompt, documentation cleanup and the later A1-A5 backlog.
3. [Acceptance test catalog](03-Acceptance-Test-Catalog.md) — fixtures, exact cases, lifecycle matrix, pass bars, environments, evidence and traceability to M1-M12/S1-S5.
4. [Migration and verification runbook](04-Migration-Runbook.md) — future branch preservation, isolated builds, existing verification commands, promotion and recovery. Commands are not executed by providing this document.

Original brief: [Cosmic - 2D Trunk Consolidation & Acceptance Plan](Original-Consolidation-Plan.md).

## The decisions that matter most

- main and engine-2d are the same commit. SF-Stable and both other remote branches are ancestors. The current ordered port/cherry-pick list is empty.
- Keep SF-Stable as the known-working behavioral reference. Do not replace newer telemetry code wholesale: main already contains additional serial/recording fixes.
- First critical regression campaign: closing the app while opening/using a COM port, losing an active connection, reconnecting, and shutting down during recording/export.
- First supported release: Windows 10/11 x64, OpenGL, >=16 GB RAM. Qualify specific NVIDIA machines and CPU instruction requirements; performance is not guaranteed on arbitrary CPUs.
- Existing Jolt/shared physics remains. Its current build enables SSE4.1/SSE4.2; resolve this compatibility floor explicitly.
- First consumer after SF_Telem: to-9km-and-beyond, initially represented by a simple trajectory/plot/replay/PNG sample.
- All five additions follow the stability milestone.
- Documentation cleanup follows the separation and stable behavior changes. Preserve useful 3D history, update the active 2D policy, and mark obsolete prompts clearly.
- Unit tests, GPU tests, actual host/UI transitions, real serial integration, packaging and soaks each provide different evidence; one cannot stand in for all the others.

## What still needs a decision or an environment

Proposed numeric budgets are draft requirements, not measured results. Ratify/calibrate them in WO-00/02 before implementation depends on them.

Still needed: exact qualification hardware/drivers and Windows builds, attached serial/ESP32 availability, a real SF-Stable recording if available, and to-9km's input schema/units/precision/data volume. Lack of these does not block synthetic test design, but it prevents claiming actual device or downstream-project qualification.

Snapshot: `engine-3d` (created and pushed) + the `cosmic-pre-2d-2026-09-16` tag. **The campaign is main-only** (Kaden 2026-09-17) — no `codex/2d-stability` candidate branch or second worktree; all WO work happens on `main`. Ratified: reject `COSMIC_2D_ONLY=OFF` on the supported trunk (hard configure error) while retaining the full tree on the parked `engine-3d` branch.

The next planning action is to review WO-00's contracts and the provisional bars, especially the bounded serial-close policy and recording-retention/durability behavior. Implementation begins only when Kaden asks to proceed.

Original packet checks before relocation: 5 planning documents, 14 work orders, 56 acceptance cases; internal file links and test-ID references checked; 11 PowerShell blocks parsed without syntax errors. These checks validate the planning packet only, not engine behavior.

## Independent review

This directory is the in-repository review copy of the complete planning packet: five planning documents, a byte-identical copy of the original brief, and an independent review prompt.

Use [the independent AI review prompt](05-Independent-Review-Prompt.md) to request a second assessment before approving implementation. Treat execution prompts and migration commands inside these documents as material to review, not instructions to execute. The original brief is preserved for comparison; later user decisions recorded in this packet take precedence where they conflict.

All repository observations and verification statements describe the earlier review snapshot unless explicitly reverified. The original files outside this repository have been retained; use this directory for subsequent review and refinement.
