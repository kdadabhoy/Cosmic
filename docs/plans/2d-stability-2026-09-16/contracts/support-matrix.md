# Cosmic 2D — support matrix

Status: WO-00 decision record, 2026-09-16. Gate G0.
Base packet: `main` at `0e8894b8540029ac57e68540aa9774cf5cf77ebe`.
Revalidated at HEAD `72b47771c869666f3a645a47bfbfae917d3167f2` (two doc-only commits
above the packet base; every engine/build source anchor below is byte-identical to `0e8894b`).

This matrix records the **declared** shipping envelope for the stable 2D trunk. It does not
claim any listed combination has passed the acceptance catalog — qualification of exact
models/drivers is a WO-02 (baseline) and WO-13 (release) deliverable. See
[`numeric-bar-policy.md`](numeric-bar-policy.md) for why no number here gates a merge yet.

## Locked decision D-CPU — CPU floor accepted

Supported CPU = **any x86-64 with SSE4.1 + SSE4.2**. This is the floor the shipped Jolt build
already requires, not a new promise:

> `Cosmic/dependencies/JoltPhysics/CMakeLists.txt:44-45`
> ```cmake
> target_compile_definitions(Jolt PUBLIC
>     JPH_USE_SSE4_1
>     JPH_USE_SSE4_2)
> ```

`/arch` is deliberately left at its SSE2 default (comment at `CMakeLists.txt:38-42`), so **only**
Jolt's own translation units emit SSE4.1/4.2; nothing propagates to the rest of Cosmic. The
adjacent `JPH_CROSS_PLATFORM_DETERMINISTIC` definition is retained and is unrelated to the CPU
floor.

Consequences, written into this matrix per the packet:
- "Broad CPU support" means **any SSE4.2-capable x86-64**, *not* literally every CPU. In practice:
  Intel Nehalem (~2008) and later; AMD Bulldozer (~2011) and later.
- **No SSE2-only build is promised or shipped.** A CPU without SSE4.2 is *out of support*, not a
  bug to fix.
- **Jolt stays as-is.** Jolt is an explicitly retained dependency for this stabilization milestone,
  not an optional backend to migrate or strip (see the dependency note below). Changing the CPU
  floor would require a separate, measured decision — it is out of scope here.
- Raising to AVX2 (the commented alternative at `CMakeLists.txt:39-42`) is explicitly **not** done.

## Locked decision D-GPU — target only

| Axis | Declared target | Notes |
| --- | --- | --- |
| OS | Windows 10 x64 **and** Windows 11 x64 | Desktop only. No Linux/macOS this milestone. Exact editions/builds recorded in WO-02. |
| Graphics API | OpenGL **4.5 core** | Current and only backend. The RendererAPI boundary is preserved so a second backend stays *possible later*, but none is required or built now. |
| RAM | **≥ 16 GB** | Assumed minimum per Kaden's decision; the two-hour soak (S01/S02) and the 2 GiB recording ceiling are budgeted against this. |
| GPU vendor | **NVIDIA**, including the **GeForce RTX 50-series** | "5000 series" interpreted provisionally as RTX 50-series. Other capable NVIDIA parts may be *compatible* (see below) without being *qualified*. |
| Exact GPU models / drivers | **WO-02 / WO-13 fields — not assumed** | Recorded during baseline and release qualification, keyed to GL renderer/version/driver string. |

A VM or headless host without a suitable GPU **cannot** certify OpenGL rendering (K03); GPU tests
on such a host are `ENVIRONMENT_BLOCKED`, never a pass.

## "Qualified" vs "compatible"

These two words are used precisely throughout this campaign:

- **Qualified** — an exact combination (Windows edition/build + GPU model + driver version + CPU
  model + Debug/Release + `COSMIC_2D_ONLY=ON`) on which the mandatory acceptance catalog was
  *actually executed and passed*, with reviewed evidence keyed to a commit SHA and environment
  fingerprint. Only qualified combinations appear in the release report (WO-13, S04). Performance
  bars (e.g. the 10,000-instance frame-time bar) apply **only** to a named qualified machine.
- **Compatible** — a combination that meets the declared envelope above (SSE4.2 x86-64, Win10/11
  x64, OpenGL 4.5 core, ≥16 GB RAM, NVIDIA) and is *expected* to run, but has **not** been
  performance-qualified. Compatible is a plausibility statement, not a certification. A compatible
  machine may run correctly yet miss a performance bar; that is allowed and is not a defect against
  this matrix.

"Stable 2D baseline" (per `01-Repository-Review.md`) means the *qualified* Windows 10/11
configurations pass the mandatory catalog. It does **not** mean every input, CPU, NVIDIA model, or
third-party driver has been proven correct.

## Retained dependency note — Jolt / physics

Jolt (`Cosmic/dependencies/JoltPhysics`) is recorded as an **explicitly retained** dependency of
the supported 2D trunk. The shared physics backend and the `RendererAPI`/physics-backend seam stay
in place and unfenced:

- "2D-only" excludes the *designated 3D subsystems* (terrain, voxel, water, nav/Recast, the old 3D
  particle path, `Renderer3D`, model import/assimp) — it does **not** remove shared math (`vec3`,
  perspective helpers), the shared camera types, or the shared Jolt physics backend. See B04's
  explicit "shared Jolt/physics/cameras allowed" carve-out and
  [`retained-feature-register.md`](retained-feature-register.md).
- No physics-backend replacement or migration is in scope. Keeping Jolt avoids an unrelated backend
  migration and is *why* the SSE4.2 floor is the CPU floor.

## Pending fields (do not let these disappear)

The following are **known-pending**, not resolved, and are owned downstream. Their absence does not
weaken any mandatory test; a missing fixture/host yields `ENVIRONMENT_BLOCKED`, not a pass.

| Field | Owner | Status |
| --- | --- | --- |
| Exact Windows 10 / 11 editions and build numbers | WO-02 | pending |
| Exact NVIDIA GPU model(s) + driver version(s) | WO-02, WO-13 | pending |
| Representative CPU model(s) used for qualification | WO-02, WO-13 | pending |
| Reference machine identity for every performance bar | WO-02 | pending |
| Real to-9km data schema / units / precision (consumer qualification) | deferred by **D-9km** | out of this milestone |

## Cross-references

- CPU floor also written into the numeric-bar policy: [`numeric-bar-policy.md`](numeric-bar-policy.md).
- 2D-only enforcement policy (default ON + configure-time reject of OFF on trunk):
  [`contracts.md`](contracts.md) and [`known-issues.md`](known-issues.md) KI-3.
- Documentation owner for the single authoritative support/build policy: WO-12 (DOC03).
