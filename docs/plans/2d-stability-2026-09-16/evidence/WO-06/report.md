# WO-06 execution report — 2026-09-17

Only WO-06 was executed, directly on `main`. Debug and Release software verification
passes; D01's genuine SF-Stable v1 compatibility prerequisite is
**ENVIRONMENT_BLOCKED**, so this report does not close the entire G3 gate.
There is no binary format/version change.

## Scope and provenance

Initial HEAD and local `origin/main` were
`0435d3c31ec986b8a95ae07a3ea418075ab76fa1`. The only initial untracked file was the
protected root plan; it was neither edited nor staged. Its SHA-256 remains
`6a4b3c112b924d35ec76a57479fed5ffde7e3463a670942ac0b5ca96a727a107`.
No branch, worktree, push, rollback-tag change or golden regeneration was performed.
The work-order README's main-only decision supersedes the older runbook branch model.
The containing local commit uses kdadabhoy as both author and committer, with no trailers.

`origin/SF-Stable` resolves locally to
`fa6ed9fe6910f8a30e5e15561bd2a9b6b8b5d66e`; its tree contains no tracked `.bin`
recording. No genuine runtime-produced v1 capture was available. The four immutable
specimens in `tests/acceptance/fixtures/wo06` are independently encoded from the v1
layout, explicitly identified as synthetic, with hashes and provenance in their manifest.
They cover zero/one/many samples, unequal/delayed entities, signed zero, truncation,
wrong version and legacy fallback. They are not substitutes for genuine captures.

The bundled Visual Studio CMake 4.3.1-msvc1 configured VS18 2026 x64 with
`COSMIC_2D_ONLY=ON` and tests enabled. Reconfiguration included the new GLOBbed
AtomicOutput.cpp. Both shipping SF_Telem and CosmicTests/host fixtures were built
in Debug and Release. Windows PowerShell 5.1.26100.9444 ran the WO-04 runner with
absolute evidence directories, repository-local TempRoot and KeepArtifacts.

Reference environment: Windows 11 Education 26200.9457, Ryzen 7 7800X3D (8 cores,
16 threads, SSE4.2 floor), 31.2 GiB RAM, RTX 5070 Ti driver 32.0.16.1692; native
host logs exercise the OpenGL 4.5 application. Main runner environment records are
in each results.json; headless GL fields are null rather than assumed.

## Final acceptance results

| Acceptance | Debug / Release | Evidence and independent assertions |
|---|---|---|
| D01 | 1 / 1 passed; genuine capture blocked | Independent v1 constants, exact float bits, descriptors/counts/rates; scene.bin precedence including invalid/empty scenes; absent-scene fallback and whole-load clearing. Genuine slot is explicitly blocked by the runner. |
| D02 | 3 / 3 passed | Analytic interpolation/end holds; pause/seek/reverse/zero-speed and invalid controls; production Load/Start commands with connected fake transport, queued live bytes isolated from loaded replay. |
| D03 | 2 / 2 passed, plus nightly repeat | Existing truncation/count tests retained; 2,000 PR and 50,000 nightly bounded mutations/config, seed 6, maximum corpus file 736 bytes. Twelve mutation classes include unterminated metadata, rates, timestamps, counts/magic/version and truncation. Failed loads leave documented empty state. Trailing bytes explicitly accepted. |
| D04 | 3 / 3 passed | Four entities x10,000 calls, barriers for snapshot prefix/busy flush, clear/re-register/destructor join; shared entity four writers x10,000 calls with owner Tick/query, all 40,000 calls accounted and zero timestamp reversals. |
| D05 | 8 small + 1 two-hour / same passed | Production Start/Stop/Export/shutdown/limit, interrupted autosave, pending newer records, manual monitoring export intent, stream/OS failures, exact loss accounting and full-size export. Actual native close/launcher cases separately pass. |
| D06 | 5 / 5 passed | 36 explicit grammar cases, independent double expectations, locale/BOM/Unicode paths, unsafe writer parameters, circular ordering and write/flush/close/publication failures preserving prior output. |

`accepted-Debug/results.json` and `accepted-Release/results.json` each have eleven
passing runner cases, zero failures and one ENVIRONMENT_BLOCKED case out of twelve.
They cover 23 distinct new headless tests; the nightly case repeats one D03 test.
Two native runner cases/config contain ten fresh retained host children plus two large-export children.
Main and native independent audit manifests also pass in both configurations.
Registration is 389 tests: 385 headless (362 retained +23 new) and four native
tests skipped by default. Filtered counts are checked; zero-test runs are not passes.

The accepted Debug and Release retained cases each pass all **362**
pre-WO-06 headless tests and **2,843,526** assertions. The retained WO-05 matrix
still runs 22,500 schedules/config; max root close was 50/22 ms and max owner
cancel 122.017/123.529 ms, within existing budgets. Parser/serial ownership fixes
were preserved. Fresh native retained recording-state children have close-to-exit
upper bounds of 219/191 ms, below the unchanged ordinary 2,000-ms budget.

## Defects and before/after evidence

Every finding was entered in the known-issue register before its fix. Original-source
counterfactuals used repository-local copies and finally restoration, not destructive
reset. Failures remain in evidence and are not counted as final passes.

| Finding | Failing-before | Fix and passing-after |
|---|---|---|
| KI-10 invalid replay time/control | before-complete D02/D03 | Finite, nonnegative, ordered timestamps; invalid controls leave state/output unchanged. Final D02/D03. |
| KI-11 blank/nonfinite CSV, unsafe writer inputs | before-complete D06 | Restricted finite decimal grammar, locale-independent conversion and writer validation. Final D06. |
| KI-12 binary replaced despite CSV/stream failure | before-complete D05 | Checked atomic staging; authoritative binary published last, failure observable. Final D05/D06 injected stream stages and native path/read-only errors. |
| KI-13 shutdown loses newer records behind autosave | before-complete D05 | Join pending snapshot, finalize current dirty prefix, clean only after successful intentional publication. Final D05 and native host paths. |
| KI-14 session limit unenforced | ceiling-before | Named 7200-second/432000-frame stop-and-finalize guard. Final D05. |
| KI-15 Debug export exceeds 30 seconds | debug-initial: 30.6457 seconds | Buffered max_digits10 to_chars; accepted Debug 6.831 seconds. Deadline unchanged. |
| KI-17 proposed Stop blocks on held writer | queued-before: exceeds 250 ms | Queue final export, service completion on all root screens. Final D05 held-worker barrier. |
| KI-18 shared-entity timestamps reverse | shared-before: 1268 reversals in first 40,000-call execution | Sample elapsed time inside entity append lock. Final D04: 40,000/40,000 and zero reversals. |
| KI-20 unterminated fixed-width v1 metadata | player-before original DataPlayer: 4007 failed assertions across armed D03 checks | Validate bounded NUL and descriptors before allocation; player-after-fixed and final D03 pass. |
| KI-22 failed manual monitoring Export stays clean | monitor-before | Export establishes save intent before queueing/writing. Final D05. |
| KI-23 held reverse/zero endpoint stops playback | endpoint-before | Stop upper endpoint only for forward travel. Final D02. |
| KI-24 ceiling bypass with ordinary auto-export disabled | policy-before: final scene.bin absent and dirty, two failed assertions before shutdown | Always queue/export at the mandatory ceiling; ordinary manual Stop still respects its preference. Armed ceiling test crosses both public policy values. |
| KI-25 reverse held at coincident endpoints | endpoint-zero-before: one failed assertion on independent timestamp-zero single sample | Nonzero speed stops at the coincident endpoints; zero speed retains Play intent. Final D02. |

Harness corrections are separately recorded: KI-19's first independent audit used
wire IDs R/L/W as descriptor tags; the corrected oracle asserts actual Drive/Weapon
categories without weakening numeric checks. KI-21's Copy-Item restoration retained
an older mtime, causing a stale original-source object and a failing apparent after
run. Explicit mtime invalidation/rebuild made player-after-fixed pass; source and final
binary hashes were then captured. Initial zero-test filters, missing manifest description
and duplicate fixture factory linking were corrected; their failures were never accepted.

KI-16 remains **open for WO-10**: the unchanged float time accumulator ends at
7183.1279296875 seconds versus nominal last sample 7199.983333, about -16.8554
seconds drift. WO-06 qualifies sample/value accounting and export behavior, not clock accuracy.

## Loss and durability measurements

Autosave's five-second interval is not a loss bound. In the independent 17-sample
barrier schedule, the first published snapshot contains five samples (timestamps
0..4). The second snapshot takes ten samples but remains held while recording reaches
17. At that point five are recoverable and **12 samples / 12 nominal seconds are
unpublished** (oldest unpublished timestamp 5; current time 17). After the held
publication, seven remain unpublished. Final post-join export accounts for all 17,
zero unpublished; accepted Debug publication wall time was 5.887 ms. Repeated failures
can leave an entire unsaved session exposed.

The [durability contract](../../contracts/recording-durability.md) makes scene.bin
authoritative. Same-directory temporary files, checked write/flush/close and Windows
atomic replacement preserve its last complete generation on normal-process failure.
CSV files are individually complete; this is not a folder-wide transaction. A later
failure can leave newer complete CSVs beside the old binary, so recovery uses scene.bin.
Failure state and dirty intent persist; no partial export is logged as a successful
session. Clear/destruction join workers. Fault injection sets real stream error state
or controls publication outcomes; native tests exercise read-only/path/directory errors.
Physical disk exhaustion, power loss and a permanently wedged filesystem are not certified.

## Full-size session, memory and independent oracle

The accelerated two-hour 60-Hz fixture contains **432,000 samples in each of three
production entities**, 1,296,000 total. A separate Python little-endian decoder and
numeric CSV reader assert metadata, byte length, every stored value, timestamp order,
all CSV rows and exact widening of source floats to doubles. Analytic channel constants
are independent of production serialization; native close/launcher fixtures use zeros.
Main and each native output's binary/CSV SHA-256 appear in the independent-audit JSONs.

| Measurement | Debug | Release |
|---|---:|---:|
| Accelerated capture wall time | 1.257 s | 0.101 s |
| Final snapshot + complete export | 6.831 s | 0.763 s |
| Observed private bytes at checkpoints | 139,513,856 | 138,919,936 |
| Native large-export close-to-normal-exit upper bound | 2,670 ms | 488 ms |

Logical float history is 48,384,000 bytes, reserved capacity 51,667,504 bytes,
snapshot 48,384,000 bytes and largest widened CSV allocation 34,560,000 bytes.
The formatter adds about 1 MiB. Memory checkpoints remain below the approved 2-GiB
envelope but are not a continuous peak sampler. The final binary is 48,385,224 bytes.
Both main and actual native large exports meet the unchanged **30-second** limit;
native children exit normally, with no kill/timeout accepted as success.

This logical fixture is not the S01 two-hour wall soak. S01/S03 broader qualification
was not run here and remains later work. T04 virtual-COM/representative USB/SPP and
second-Windows-version prerequisites remain ENVIRONMENT_BLOCKED from WO-05. No software
fake result upgrades those physical gates. Genuine SF-Stable capture compatibility
is also explicitly blocked; supplying it is required before claiming that part of D01.

## CSV contract and reproducibility

The [asserted CSV grammar](../../contracts/csv-grammar.md) supports unquoted rectangular
finite decimal/exponent data, LF/CRLF, blank lines, outer spaces/tabs, initial UTF-8 BOM,
Unicode filenames and locale-independent conversion. Blank cells, ragged/trailing rows,
quotes, hex, junk, nonfinite values, overflow/unrepresentable underflow and invalid
headers reject with both output containers empty. Numeric first rows are data;
mixed numeric-looking headers reject. Finite nonzero doubles round-trip bit exactly.
Writer validation occurs before touching output; failures preserve prior files.
AppendRow does not validate prior grammar/column counts and copies the prior file;
concurrent writes to the same path are unsupported. General quoted CSV and scientific
import/schema qualification remain outside WO-06.

Exact expanded commands, exit codes, seed, fixture hash, dirty source parent and
environment are in the runner JSON/JUnit reports. Reproduction templates below run
from `C:\dev\Cosmic`; use Debug then Release. Keep all generated output repository-local.

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake -S C:\dev\Cosmic -B C:\dev\Cosmic\build -A x64 -DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=ON
& $cmake --build C:\dev\Cosmic\build --config Debug --target CosmicTests SF_Telem --parallel 8
& $cmake --build C:\dev\Cosmic\build --config Release --target CosmicTests SF_Telem --parallel 8
$ps51 = 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe'
& $ps51 -NoProfile -ExecutionPolicy Bypass -File C:\dev\Cosmic\tests\acceptance\Run-Acceptance.ps1 -Manifest manifests/wo06-verification.manifest.json -Config Debug -OutDir C:\dev\Cosmic\docs\plans\2d-stability-2026-09-16\evidence\WO-06\rerun-Debug -TempRoot C:\dev\Cosmic\tests\acceptance\_temp\wo06-rerun-Debug -KeepArtifacts
& $ps51 -NoProfile -ExecutionPolicy Bypass -File C:\dev\Cosmic\tests\acceptance\Run-Acceptance.ps1 -Manifest manifests/wo06-retained.manifest.json -Config Debug -OutDir C:\dev\Cosmic\docs\plans\2d-stability-2026-09-16\evidence\WO-06\rerun-retained-Debug -TempRoot C:\dev\Cosmic\tests\acceptance\_temp\wo06-rerun-retained-Debug -KeepArtifacts
```

The accepted final runs use the full wo06.manifest.json; the two commands above
permit focused headless and retained reproduction. Independent audits use separate
manifests. Main audit uses the copied `<Config>-oracle-input.json` accepted runner
report to locate its retained two-hour output. Native audit uses native-long-accepted-Config. These audit input paths
and native child roots must identify a fresh reproduction's output; do not overwrite
existing child roots containing asset junctions. Report paths for accepted final runs:

- `accepted-Debug`, `accepted-Release`: D01–D06, large export/nightly, native children,
  all 362 retained and the genuine-capture blocked slot.
- `audit-accepted-Debug`, `audit-accepted-Release`: independent main sample/CSV oracle.
- `native-audit-accepted-Debug`, `native-audit-accepted-Release`: independent large native outputs.

Earlier complete before/after reports are retained. The `final-complete-Debug/Release`
tool sessions ended during retained testing, left no complete runner report, and are
**interrupted, not accepted passes**. The accepted runs use fresh directories and
preserve the original logs. No process kill is counted as a successful close.

[final-hashes.json](final-hashes.json) pins effective cache, tested source hashes,
final Debug/Release binaries, immutable fixtures, reference refs and every retained
raw log hash. Its raw_git_diff_sha256 hashes raw Git stdout at evidence capture;
runner dirty_diff_sha256 hashes its own PowerShell-joined diff representation and is
not interchangeable. New untracked source bytes are independently pinned by source
hashes. [raw-log-excerpts.txt](raw-log-excerpts.txt) preserves readable build, before,
after, timing and assertion excerpts; full raw logs and large output folders remain
local and are not committed. No expected failure or missing prerequisite is hidden.
