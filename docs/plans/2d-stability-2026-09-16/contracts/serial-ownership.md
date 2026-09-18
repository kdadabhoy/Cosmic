# WO-05 serial cancellation and ownership

SerialLink calls except Write, and SerialPort lifecycle/status calls, run on the application's
owner (main/render) thread. GetState/IsOpen adopt completed opens on that thread.
FlushBuffer is mutex protected. Write alone may run concurrently on an app-owned
writer (including the SerialLink Write pass-through); join that writer before
destroying its caller/link. SerialLink Poll remains owner-only because it updates
policy time and adopts sessions. Concurrent lifecycle
callers, status polling from other threads, and destruction racing a new call are
unsupported. Open is the legacy synchronous API; use BeginOpen for responsive UI.
Write is synchronous: use an app-owned writer when UI responsiveness is required.
The 100-ms open cancellation grace is checked against the approved 250-ms UI
service budget. Physical read/write drain and heartbeat remain unqualified.

The actual SF_Telem root owns one link by value. TelemHub and TestingManager borrow
its address; switching screens and hiding a connection panel retain that connection.
OnDetach clears reconnect intent and cancels serial work before screen destruction
or recorder flush. Host unload calls OnDetach before deleting the root/FreeLibrary.

BeginOpen's worker captures only a shared-owned OpenJob and transport, never a
SerialPort/root/screen pointer or app callback. Only the owner starts a reader after
adopting a completed open. Close signals the job's event/abandon flag before waiting,
retries CancelSynchronousIo for up to 100 ms, and joins a completed worker. A driver
that ignores cancellation retains a shared-owned job/transport after thread detach;
late success is closed by that worker, no read session is started, and no second
attempt can reuse that transport until cleanup completes. The worker's code and
shipping Win32 transport reside in the host's Cosmic.dll, which must remain loaded
until its workers finish (the normal host imports it for its entire process lifetime).
Injected transports must likewise keep their implementation module alive through
late cleanup. This is lifetime isolation, not detaching a worker that references a
destroyed SerialPort. A permanently wedged driver can retain its job until process
exit; eventual cleanup is not claimed for a driver that never returns.

Reader and writer cancellation drain overlapped completion before buffers/events
die. Close signals stop before joining the reader and acquiring the writer mutex;
only then does it close the transport. Write waits at most 1 s before requesting
cancellation, then drains completion. Windows driver cancellation/drain deadlines
still require T04 physical qualification; an uncooperative read/write driver cannot
be certified by fake tests. The approved close budget remains 2 s, with a separate
30 s export budget; exceeding either is failure, never an extended passing deadline.

Session close clears queued bytes. Parser, firmware, KISS/CRC8 and PC tagged-text
grammar are unchanged; COBS is a separate engine framing surface.

Each adopted session increments ConnectionGeneration. SerialLink observes that
generation on its owner-thread update, so ConsumeJustConnected resets the hub's
partial frame even if a reconnect completes between two updates that both see open.
Disconnect/Shutdown clear pending connection notification and reconnect intent.

The receive queue accepts a prefix up to 1 MiB, discarding incoming excess until
the consumer drains it. Lifetime cumulative ReceivedBytes counts raw returned
bytes, OverflowBytes counts capacity loss, and DiscardedOnCloseBytes counts queued
bytes cleared by teardown/reconnect. Overflow is transport loss, not a change to
malformed-input grammar. The hub retains its existing 4096-byte text purge.

Main drains live telemetry and records on fixed ticks. Home and Analysis retain
serial bytes within the queue limit; Testing owns its separate hub. Loaded Replay
uses player data and leaves live bytes queued; returning to Main drains them.
Application pause continues owner updates with dt=0 and skips fixed recording;
the default SetPauseOnMinimize(false) continues dispatch while minimized. Opting
into pause-on-minimize suppresses execution passes until restore, while the serial
reader may continue filling the bounded queue. Connection/service clocks use the
provided dt, so pause also freezes their time-based stale/retry calculations.

The Serial Link panel has no independently closable X button: ImGui::Begin uses
nullptr. Switching to Home is the production command that removes its drawing
surface, without transferring/destroying ownership. Tests label that distinction;
they do not claim mouse/native-dialog coverage. Actual WM_CLOSE and the host's
deferred TransitionToLauncher safe zone are exercised separately by the test DLL.

Windows cancellation is a request, not a driver completion guarantee. See
[Microsoft cancellation guidance](https://learn.microsoft.com/en-us/windows/win32/fileio/canceling-pending-i-o-operations)
and [CancelSynchronousIo](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelsynchronousio).
T04 must measure real driver completion before claiming a physical close deadline.
