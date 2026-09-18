# WO-05 — serial shutdown, shared ownership and acceptance

2026-09-17. Main-only, `COSMIC_2D_ONLY=ON`. No push. Revalidation found
HEAD and the local `origin/main` ref both at `0e71e0c3f2cc0aa6ca998302c8fcb93fce2e8bf7`;
the supplied older origin
snapshot was stale. No fetch was required. The protected untracked root plan was
left untouched. Only Cosmic paths were edited.

## Fix first, then campaign

Local commit `552ef4519475e614b2e18f35d656daa8c011a8ca` landed the initial
WO-05a H1 fix before the matrix. The actual SF_Telem root's OnDetach took 2507 ms
with a barrier-confirmed 2500-ms delayed open before the fix, violating 2000 ms.
The same regression passed after cancellation-before-wait, independent open-job
ownership, owner-thread session adoption and serial-first root teardown.
Evidence: `before/` and `after-fix/`. Both formerly skipped H1/H3 seats are active.
Those original H1 logs retain a historical diagnostic message saying skipped;
the seat actually ran (its assertion failed before and passed after). Current
source removes that stale message; the counts/assertions are authoritative.

The root owns `SerialLink m_Link` **by value**. TelemHub and TestingManager borrow
its address; screens share services. Tests use that production wiring and real
root commands. There is no independent test SerialPort standing in for shutdown.
The test DLL attaches the full shipping SF_Telem screens/assets and exercises
Application's real native window-close and deferred launcher/unload safe zone.
No shipping host shutdown behavior was replaced.

Additional failing-before regressions:

| Defect | Before | Fix |
| --- | --- | --- |
| KI-5 pending writer/device release | Root OnDetach returned with one active writer using isolated WO-04 SerialPort source plus null-stop signature adapter (`write-before/`) | Cancel and drain write; protect device release with writer mutex; join external writer before root destruction |
| KI-6 stalled consumer queue | 1049088 queued bytes exceeded 1048576 (`queue-before/`) | 1-MiB accepted-prefix cap, exact raw/overflow/close-discard accounting |
| KI-7 rapid stall reconnect | Old six-byte hub partial survived; following valid frame rejected (`generation-before/`) | Monotonic adopted-session generation resets hub even when both policy ticks see open |
| KI-8 PS5.1 empty stdout | Regex.Match(null) aborted the parent before JSON/JUnit (`runner-before.txt`) | Preserve missing-count FAILED with observed -1; retained runner self-tests pass |
| KI-9 owner cancellation grace | All 100 held-open cancellations took about 501 ms, exceeding the 250-ms UI bar (`cancel-before/`) | 100-ms grace, independently owned late cleanup; passing-after percentiles recorded below |

The before-source substitutions were restored in finally blocks; no reset, branch,
worktree, push or frozen snapshot mutation occurred. Protocol/firmware source was
not changed. See [ownership](../../contracts/serial-ownership.md) and
[parity/loss ledger](../../contracts/serial-parity-ledger.md).

## Results

Debug and Release each pass all 9 U runner cases and 362 headless tests are
registered. The retained suite is 350/350 in both configurations, including the
activated H1/H3 regressions. Both final builds have zero warnings. The two GPU
host cases are explicitly run in their own fresh-process tier; default U skips
are not substitutes for their native results.

| ID | Software result, Debug and Release unless noted | Evidence |
| --- | --- | --- |
| T01 | PASS: 33 tests / 3834 assertions; every split, independent XOR/channel oracle, retained parser cases | `current-debug/`, `current-release/` |
| T02 | PASS: 1 test / 1009 assertions; exact purge threshold, 1000 faults, delimiter recovery and staleness boundary | same U reports |
| T03 root | PASS: 22500 full-cross iterations/config, 704500 assertions/config; max close Debug 25 ms / Release 18 ms | same U reports |
| T03 late/cancel/write | PASS: 100 root-destruction late successes +100 owner cancellations/config, separately drained app-owned writer | same U reports |
| T03 native | PASS: all 1800 Release native children /18 schedules x100; normal process-exit upper bound max 272 ms <=2000 ms | `native-current-a/`, `native-current-b/` |
| T04 | ENVIRONMENT_BLOCKED: all six OS/transport seats, zero physical iterations | `hardware-current/`, `T04-hardware-blocked.md` |
| T05 | PASS: 216000 accepted /0 rejects, 72000 per ESC, exactly 108000 stored ticks and CSV rows for EACH entity; rings 512; real-host policy PASS both configs | U reports, `long-current-audit/`, `policy-current-debug/`, `policy-current-release/` |
| T06 | PASS: 8 tests /407 assertions; 72000 10x burst frames, silence, 20 reconnects, 1 deliberate reject/recovery, exact queue loss; independent shipping KISS/CRC8 and retained COBS | same U reports |

The new KI-9 before case failed all 100 responsiveness assertions at approximately
501 ms with the original 500-ms grace. With 100-ms grace, Debug cancellation
p95=110.304 ms, p99=110.718 ms, max=121.911 ms; Release p95=111.065 ms,
p99=111.643 ms, max=116.552 ms. All 100 iterations/config satisfy 250 ms.
These owner-call durations qualify the fake held-open cancellation path;
physical driver drain and complete real-device UI heartbeat remain unqualified.

Native normal-exit upper bounds: p95=222 ms, p99=235 ms, maximum=272 ms. All 18 schedule/transition groups contain 100 children; each of 5 storage states occurs 20 times/group. See `native-current-summary.json`, both parent JSON/JUnit reports, child summaries and `native-child-log-hashes.txt`.

The root matrix defines 9 OS schedules, 5 transitions, 5 storage states and
100 repetitions per full cross: 22500 iterations/configuration. OS schedules are
delayed success, delayed failure, successful but unadopted open, pending read,
silent stall, hard drop, in-flight write, reconnect pending and closed. Transitions
are root close, root return, actual Home command hiding the connection surface,
all real screen commands and explicit disconnect. Storage states are monitor,
intentional recording, barrier-held export, barrier-held autosave and loaded replay.
It checks exact saved channel values, all transport operations/handles, successes
versus releases, no cleanup races, callbacks drained, no flush alive and no
post-teardown reconnect. Export barriers release only after real serial cleanup.

Native host close and deferred launcher unload are a separate fresh-process
campaign: 9 x 2 x 100 =1800. Each storage state occurs 20 times per actual host
schedule; the service matrix supplies 100 per complete storage cross. Each child
requires one detached/destroyed root and full Application teardown within 2 s
of the close request. Final native shards additionally compare the real root's
close-request timestamp with the runner's evidence-completion time, after normal
child exit and log/count validation: a conservative process-exit upper bound
must be nonnegative and <=2000 ms. The native Application assertion uses monotonic
GetTickCount64 independently. No manual kill contributes a passing result.

The T05 30-minute stream uses a controlled logical clock, not a 30-minute wall
soak: 216000 valid data frames, 72000 per ESC, and 108000 60-Hz stored ticks per
entity. The long fixture contains zero heartbeat lines; heartbeat parsing has
separate retained tests. Every accepted frame's decoded channels use independent equations.
The 512-entry plot rings remain bounded. Separate real-host pause/minimize/restore
checks corroborate the acquisition policy. This does not qualify a two-hour
export or recording durability; those are WO-06.

## Cancellation and limitations

Lifecycle/status and SerialLink calls except Write are owner-thread only. Write and byte
flush are the supported concurrent SerialPort operations; app-owned writers
must be joined before their caller/link is destroyed; SerialLink Write is the
same supported pass-through. SerialLink Poll remains owner-only. BeginOpen is responsive;
legacy Open is intentionally synchronous. Concurrent lifecycle callers and
destruction racing a new call are unsupported.

The open worker captures only shared job/transport, no port/root/screens/callbacks.
Close signals stop first, requests synchronous cancellation, waits at most 100 ms
for open cleanup and joins a finished worker. A cancelled driver that returns
late retains independent ownership, closes a late successful handle, never starts
a reader and cannot stack another attempt on that transport. Only this isolated
job may outlive the root. Shipping Cosmic.dll remains imported by the host for
the process lifetime; an injected implementation module must also remain loaded
until late cleanup finishes. The native fixture constructs its injected transport
in CosmicTests.exe, so its virtual implementation survives project DLL unload.
Permanently wedged driver cleanup is not claimed.
The separate 100-iteration non-cooperative regression destroys the actual root
before allowing late success, then verifies cleanup/destruction.

Readers and writers retain borrowed buffers/events until overlapped completion,
and are stopped/drained before device release. They are never detached with a
destroyed owner. A driver that ignores read/write cancellation can still block
drain; fake tests cannot certify a physical 2-s deadline. The approved close and
export limits remain 2 s and 30 s. See
[hardware seats/procedure](T04-hardware-blocked.md) for all unproven rows.

## Evidence provenance and harness failures

Each WO-04 runner report has commit/diff/config, environment, exact command,
exit, count, seed, fixture hash, timestamps, capability blockers and golden hashes.
Runtime source/binary hashes supplement untracked-source provenance. Raw `.log`
files remain locally beside reports; committed text excerpts and log hashes
preserve useful output without committing every child log. The parent SHA is
552ef45 with dirty WO-05 changes at execution; the follow-on local commit contains
the tested source and evidence. The unrelated untracked protected plan also makes
the runner's dirty flag true. No acceptance golden was regenerated.

Final host children use a unique CWD, actual saves/settings and project log folders
per child. Immutable runtime assets are linked into that root; every source and
destination remains inside Cosmic. Shards share only immutable assets. Application
does not consume COSMIC_USER_DATA, so the initial host campaign's shared build CWD
was not described as final per-child isolation. The final isolated campaign repeats
all 18 native schedules at 100 repetitions, in two independent WO-04 runner shards
(1000 +800). The scoped evidence `.gitignore` keeps linked assets and raw saves
out of commits; child summaries and log excerpts/hashes remain tracked evidence.

Failures were preserved, diagnosed and corrected without weakening passing bars:

- `debug-unit/`: an initial read-entry barrier could precede append; corrected to
  observe the next actual read call after an already-entered pending read.
- `debug-host/`: constructing a second Application/GL singleton in one process
  crashed. Each actual shutdown now uses a fresh process; multi-Application reuse
  is outside the shipping host lifecycle and was not fixed in WO-05.
- `host-preflight/`: test DLL's separate static GLFW instance could not retrieve
  the host window. The fixture now finds the exact thread-owned HWND via the
  shipping `CosmicWindowPtr` property. All 18 native/deferred preflight cases passed.
- `final-debug/` and `final-release/`: the initial retained-suite filter also
  excluded five WO-05a tests. Expected 350, observed 345 with exit0: runner FAILED.
  The filter now excludes only `WO-05 `; the count requirement remains 350.
- `long-audit-initial-error.txt`: the independent descriptor auditor incorrectly
  assumed unused name padding was zero. MSVC Debug strncpy_s fills padding with
  0xFE after the first NUL. The auditor now uses the actual format string terminator;
  every entity descriptor, exact file length and CSV row count is still required.
- `verified-debug-ps51/`: all cases passed but Git line-ending warnings triggered
  PS5.1's native-stderr exception path, leaving the runner's diff hash null. Final
  reports use a process-only `core.safecrlf=false` Git override to prevent those
  warnings; no Git configuration file was edited. Full diff hashes are populated.

Environment: Windows 11 Education 26200.9457, Ryzen 7 7800X3D (SSE4.2 floor),
31.2 GiB RAM, NVIDIA RTX 5070 Ti driver32.0.16.1692, OpenGL4.5 core as logged
by real host children, VS18/2026 bundled cmake. The outer initial reports used
PowerShell7.6.5; host orchestration uses Windows PowerShell5.1. Final interpreter
identity is recorded explicitly in the final current reports.

## Local completion

Initial reproduction fix landed first in `552ef45`. The follow-on commit immediately
after it contains the final cancellation/queue/generation changes, runner regression,
full software tests and evidence. Both commits follow D-commit; neither was pushed.
Software T01/T02/T03/T05/T06 are proven at the stated scope. T04 remains
ENVIRONMENT_BLOCKED, so G3 physical qualification is not closed. All final runtime
source/binary hashes still match `software-hashes-final.txt`; the tested-source parent
is 552ef45 with dirty diff e7ac079b581cb01c671550b228f66c3b4c0de41d55cbd9309ca2eb506e1c52ee.
Later documentation/evidence edits do not change the runtime source or binaries.