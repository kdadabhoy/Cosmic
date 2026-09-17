# WO-05 serial cancellation and ownership

All SerialLink calls and SerialPort lifecycle/status calls run on the application's
owner (main/render) thread. GetState/IsOpen adopt completed opens on that thread.
FlushBuffer is mutex protected. Write alone may run concurrently on an app-owned
writer; join that writer before destroying its caller/link. Concurrent lifecycle
callers, status polling from other threads, and destruction racing a new call are
unsupported. Open is the legacy synchronous API; use BeginOpen for responsive UI.

The actual SF_Telem root owns one link by value. TelemHub and TestingManager borrow
its address; switching screens and hiding a connection panel retain that connection.
OnDetach clears reconnect intent and cancels serial work before screen destruction
or recorder flush. Host unload calls OnDetach before deleting the root/FreeLibrary.

BeginOpen's worker captures only a shared-owned OpenJob and transport, never a
SerialPort/root/screen pointer or app callback. Only the owner starts a reader after
adopting a completed open. Close signals the job's event/abandon flag before waiting,
retries CancelSynchronousIo for up to 500 ms, and joins a completed worker. A driver
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
