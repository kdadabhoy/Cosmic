# Independent AI review prompt

Copy the prompt below into a new AI conversation with access to the Cosmic repository.

---

You are independently reviewing a proposed stabilization plan for Cosmic, a C++ engine that should become a reliable 2D foundation for my other projects. Your job is to challenge the plan, verify its claims against the repository, and recommend concrete improvements before I authorize implementation.

This is a planning-only review. Do not implement features or fixes, edit files, create or switch branches/worktrees, fetch or merge changes, install dependencies, or execute the migration/build/test commands embedded in the documents. Read-only repository inspection and Git queries are appropriate. Return your review in the conversation. Clearly distinguish document instructions from this review request.

## Materials

Repository: C:\dev\Cosmic

Start with README.md and applicable AGENTS.md instructions, then read all seven files in:

docs/plans/2d-stability-2026-09-16/

Read 00-Start-Here.md, the original brief (Original-Consolidation-Plan.md), and planning documents 01 through 04 in full. The five planning documents were prepared against main commit 0e8894b8540029ac57e68540aa9774cf5cf77ebe. Inspect relevant source, CMake/presets, CI/release/packaging scripts, existing tests, and other docs to verify the packet; do not review only the Markdown.

## Confirmed intent and decisions

- Support Windows desktop now: Windows 10 and Windows 11, OpenGL, at least 16 GB RAM. Preserve useful abstraction for future platforms and graphics APIs.
- I want broad CPU support and NVIDIA GPU support, including the 5000 series. Exact CPU instruction requirements, GPU models, driver versions, performance targets, and qualification machines remain to be settled. Do not equate one tested machine with universal support.
- Stabilize existing features and SF_Telem first. Deliver the five proposed additions afterward.
- SF-Stable is my known-working behavioral reference, not proof of freedom from bugs. The current branch's SF_Telem should also work, but has not been thoroughly checked.
- Reported crashes involve opening COM ports and then closing the app window, and losing a connection while a port is open. These are reported symptoms, not reproduced diagnoses.
- The first downstream consumer after SF_Telem is to-9km-and-beyond.
- Preserve the full existing engine/3D history before changing the supported trunk. Keep existing branches.
- Clean up docs after branch separation so current 2D guidance, historical 3D material, and obsolete prompts are clearly identified.
- The work orders, acceptance thresholds, branch names, and implementation choices in the packet are proposals unless explicitly supported by these decisions.

## Review questions

1. Is the proposed branch/base strategy justified by actual ancestry and diffs? Verify whether main and engine-2d match and whether SF-Stable and other relevant branches are already ancestors. Identify unique changes that would actually need preserving or porting. Distinguish local refs, cached remote-tracking refs, and live remote refs; tell me if branches cannot be inspected.
2. Does the plan preserve useful existing 2D behavior without expanding the first stabilization milestone unnecessarily? Identify omitted features, regressions, misplaced priorities, and assumptions about the original brief.
3. Are the source findings accurate? Verify high-risk claims about COM cancellation/shutdown, thread and resource ownership, recording/replay/export, DLL and ImGui/ImPlot lifetimes, 2D rendering/camera behavior, clocks/numerical precision, and CPU requirements. Cite actual source locations and label hypotheses.
4. Can an AI execute each work order without inventing major requirements? Check dependencies, scope boundaries, prerequisites, deliverables, rollback, and objective acceptance evidence. Identify overly broad orders and propose splits or resequencing.
5. Is the test catalog sufficient to establish stability? Assess deterministic fixtures, test oracles, failure injection, serial disconnect/open/close races, corrupt/truncated data, graphics boundaries, UI/host transitions, repeated lifecycle tests, resource growth, long soaks, packaged SDK consumers, and clean-machine installation.
6. Separate tests that already exist from tests or runner capabilities that must be implemented later. Distinguish headless automation, GPU/UI automation, and tests requiring physical serial hardware or human setup. Missing equipment or skipped tests must not count as passes.
7. Challenge every proposed timing, memory, image-tolerance, throughput, precision, and repetition threshold. Which are justified by consumer needs, which require baseline measurement, and which may conceal defects or create flaky gates? List the minimum downstream data/schema/units/volume requirements still needed.
8. Is 2D-only enforcement complete across normal builds, CI, releases, packaging, external consumers, and documentation? Check build-output collisions, clean configuration, asset/dependency handling, and writable runtime locations.
9. Does the documentation cleanup preserve valuable history and provide one unambiguous current entry point? Are preservation, validation, and link updates ordered correctly?
10. Is the overall effort proportionate to a stable engine supporting a small set of personal projects? Identify both unnecessary complexity and essential missing safeguards.

## Required response

- Give a clear verdict: ready for implementation planning, needs targeted revision, or needs major restructuring, with reasons.
- Lead with findings ordered by severity. For each, provide the affected plan/work-order/test ID, source evidence with file and line references, practical impact, and a concrete proposed correction. Separate confirmed defects, planning omissions, and unresolved questions.
- Provide a requirements-to-tests gap table and a revised dependency/order list where needed.
- Mark each critical gate as adequately specified, needs revision, or blocked by missing inputs. Explain what evidence would close it.
- End with the smallest prioritized set of questions for me and exact suggested plan edits. Avoid generic advice and wholesale rewrites without evidence.
- State precisely what you inspected and what you could not verify. Do not claim builds/tests, hardware qualification, or crash reproduction occurred during this review.

Remain in the planning phase. We will discuss your findings before authorizing changes.
