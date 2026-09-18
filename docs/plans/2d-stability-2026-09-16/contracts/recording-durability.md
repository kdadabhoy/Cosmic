# WO-06 recording and replay contract

v1 remains the little-endian CSMC layout with float timestamps and float channel
values. Scientific consumer data stays double; the CSV utility stores doubles.
The pinned `tests/acceptance/fixtures/wo06` specimens were independently encoded,
not produced by SF-Stable. Genuine SF-Stable runtime compatibility remains
ENVIRONMENT_BLOCKED until a genuine capture and provenance are supplied.

`scene.bin` is authoritative. Each output uses a unique same-directory temporary
file; write, flush and close must succeed before atomic Windows replacement.
Recorder publishes scene.bin only after all CSV serializers have completed.
Failures retain the previous scene.bin, expose FlushState::Failed, and log failure.
CSV files are individually complete derived exports; the directory is **not an
atomic multi-file transaction**. A failed later publication can leave complete
CSVs from a newer generation beside the previous binary. Use scene.bin for
recovery; a failed export is never reported as a complete session. Temporary files
left by process interruption are ignored by replay (their extensions are not .bin).

Stream flush/close and MoveFileExW WRITE_THROUGH are checked. This establishes
normal-process publication, not a guarantee against sudden power loss, storage
controller cache loss or filesystem corruption. Fault tests inject stream/OS
outcomes at open/write/flush/close/partial-write/publication and exercise native
read-only replacement and directory/path errors. They do not fill the user's disk.

Recorder lifecycle, registration, Tick, Flush, Clear and destruction are owner-thread
operations. Register/reserve/clear require all Record callers joined; Record may
run concurrently on registered stable entities and with an existing flush worker.
Snapshotting locks each entity independently: every entity has a consistent prefix,
with no promise of one global cut. A second Flush while busy is ignored; Clear
and destruction join a pending worker. Duplicate registration retains the original
ID/metadata. New registration is supported after Record callers have joined.
No recorder worker detaches or outlives its owner.

SF_Telem Stop/Export queues a final intentional export while another save is pending.
Save completion is serviced on every root screen without pumping replay/live bytes
on inactive screens. Start is refused while saving; failed intentional exports remain
dirty. An explicit monitoring Export also establishes save/keep intent. Root serial
cleanup precedes recorder teardown. Shutdown drains pending work,
then finalizes newer dirty records. Shutdown can block for disk completion; the
qualified two-hour fixture must finish within the unchanged 30-second export limit.
A permanently wedged filesystem is not qualified by the injection tests; harness
deadline expiry is failure, never success. Loaded replay leaves live serial bytes
queued; Start unloads replay and returning to Main consumes live bytes.

Intentional sessions stop and finalize at the named 7200-second or 432000-frame
limit, whichever occurs first, including when ordinary manual-stop auto-export is disabled.
At the limit subsequent fixed calls cannot append;
Start begins a new session. The 18000-frame reserve is preallocation, not retention.
RecordFixed still captures monitoring ticks for the existing live/export workflow;
the two-hour bound qualifies intentional sessions, not unlimited passive monitoring.

Autosave is attempted every five seconds of supplied recorder time, and is skipped
while busy. There is **no unconditional five-second loss bound**. Count loss against
the most recent successfully published snapshot, independently per entity. In the
barrier schedule, at t=17 the last published snapshot has five of 17 intended samples:
12 samples/seconds are unpublished; after the held second publication seven remain
unpublished; final post-join publication accounts for all 17. Write duration and
missed autosave opportunities add to the interval; repeated failures can make loss
span the entire unsaved session. Publication ages must also distinguish float
recorded time, nominal integer-tick time and wall time.

Two-hour 60-Hz history has 432000 samples per entity: 48384000 logical float bytes
for the 8/8/9-channel SF fixture, plus reserved capacity, an equal logical float
snapshot and up to 34560000 bytes of widened CSV columns for the largest entity.
The CSV formatter additionally buffers about 1 MiB. Evidence reports reserved
bytes, private memory at export allocation/stream checkpoints and publication time.
The accelerated logical fixture is not a two-hour wall soak; S01/S03 qualification
and physical-driver T04 remain separate work.

Replay load always replaces/clears prior data before attempting a load. Any failed
file in a legacy folder clears the whole result. Existing scene.bin takes precedence
even if invalid/empty; fallback is used only when it is absent. Invalid load leaves
unloaded, paused, empty entities, position/duration zero; speed retains its prior
finite value. Existing count/remaining-byte checks remain; files >512 MiB, >4096
entities, >1024 channels/entity or modeled decoded frame storage >1 GiB are rejected.
Metadata must be NUL-terminated in its fixed-width field; names/channels nonempty;
entity names unique across the loaded session. Trailing bytes remain accepted for
v1 compatibility and are explicitly tested. Payload floats retain v1 representation.

Times/rate must be finite; sample rate positive, timestamps nonnegative and
nondecreasing. Duplicate times remain compatible; an exact interior query chooses
the last duplicate at that time; the final endpoint chooses the final row. Values
hold before a delayed entity's first row and after its last row. Finite seeks clamp
to the session [0,duration], returned timestamp is that query time. Exact stored
times copy values without interpolated rounding; other times interpolate linearly.
NaN/Inf seeks and speeds are ignored; SampleAt invalid time returns false without
changing output; negative/nonfinite dt does not advance playback. Pause freezes
position, reverse/forward stop at the respective endpoints, zero speed stays still
without clearing Play intent even at an endpoint. A held reverse tick at the upper
endpoint retains Play intent.
At coincident endpoints (a zero-duration recording), any nonzero speed stops on
a valid Tick, including a held tick; zero speed retains Play intent.

The long fixture exposes float accumulator drift (~16.855 seconds at the last sample
versus nominal ticks). KI-16 is open for WO-10; WO-06 preserves the format and records
the limitation instead of claiming clock qualification.
