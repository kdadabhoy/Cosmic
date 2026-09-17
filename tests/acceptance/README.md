# Cosmic 2D acceptance runner (WO-04)

`Run-Acceptance.ps1` turns an acceptance manifest into executed, observable evidence.
It is the process runner the acceptance catalog
([`../../docs/plans/2d-stability-2026-09-16/03-Acceptance-Test-Catalog.md`](../../docs/plans/2d-stability-2026-09-16/03-Acceptance-Test-Catalog.md))
specifies. WO-04 builds the runner and the H01-H04 self-tests; the individual
T/D/R/C/N cases are authored by their own work orders.

## Layout

| Path | Purpose |
| --- | --- |
| `Run-Acceptance.ps1` | Entry point: probes environment/capabilities, filters cases, runs them, writes JSON + JUnit, sets an honest exit code. |
| `AcceptanceRunner.psm1` | The runner library (process runner with per-case deadlines, capability gating, golden hashing, evidence emit). |
| `manifests/*.manifest.json` | Case manifests. `selftest.manifest.json` is the H01-H04 proof set. |
| `fixtures/` | Small self-test fixtures (nonzero exit, crash, hang, pass). |
| `goldens/` | Golden files, hashed before/after every run (never written in acceptance mode). |
| `_results/` | Per-run output (JSON, JUnit, per-case logs). Git-ignored. |

## Model

Five execution tiers: **U** (pure deterministic), **W** (Windows integration),
**G** (hidden-window GPU), **I** (real editor UI), **Q** (qualified / soak). A case
declares its tier, the capabilities it `requires`, its command, an independent
`deadlineSec`, and its oracle (`expectExit`, optional `minTests`, optional `golden`).

Honest gates (enforced, not optional):

- A **timeout is FAILED**, never a graceful pass. On timeout the runner kills **only**
  the child's process tree (`taskkill /T`); the parent survives and runs the rest.
- A **missing capability** (GPU / COM / Windows / editor UI) is **ENVIRONMENT_BLOCKED**
  with the missing prerequisite named - never a phantom pass. Fake-transport cases
  still run and identify themselves as `transport=fake` in evidence.
- A filtered profile with **zero selected cases is a FAILURE** (exit 2).
- **Goldens are read-only**: `-UpdateGoldens` is refused in acceptance mode (exit 3),
  and the golden dir is SHA-256 hashed before and after; any change is flagged.
- A case that declares `minTests` but runs fewer (e.g. a filter matching zero tests)
  is a **FAILURE**.

Every run records the evidence contract to `_results/run-<stamp>/results.json`: commit
SHA, dirty-diff hash, mode/config, environment (OS build, CPU, RAM, GPU/driver),
capabilities (and any forced-unavailable), and per case the exact command, exit code,
crash/hang evidence, duration, seed, fixture hash, and log paths. `results.junit.xml`
is the CI-parseable form.

All paths are parameterized (`{BIN}`, `{CONFIG}`, `{MODE}`, `{ACCEPTANCE}`, `{REPO}`,
`{RUNTEMP}`, `{USERDATA}`) - no absolute user paths appear in a manifest. The child's
`TEMP`/`TMP`/`COSMIC_USER_DATA` are redirected into a per-run isolated directory so a
case never touches real recordings, settings, or the SF-Stable install.

## Running

```powershell
# H01-H04 self-tests. -DisableCapability gpu-gl exercises the GPU-blocked branch on a
# machine that has a GPU; the forced removal is recorded in evidence (never faked).
./Run-Acceptance.ps1 -Manifest manifests/selftest.manifest.json -SelfTest -DisableCapability gpu-gl -Config Release

# A real profile run (once cases exist): every 'pr' case, no self-test oracle.
./Run-Acceptance.ps1 -Manifest manifests/<name>.manifest.json -Profile pr -Config Release
```

In `-SelfTest` mode each case carries an `expectVerdict`; the runner asserts it
classified every case exactly as expected and exits nonzero if not. This is how the
runner proves it catches failure, hang, missing suite/golden, and missing environment
- rather than trusting it to.

Requires Windows PowerShell 5.1+ and a built `CosmicTests.exe` under
`build/Runtime/<Config>` (override with `-BinDir`).
