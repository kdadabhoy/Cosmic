# T04 physical qualification seats — ENVIRONMENT_BLOCKED

2026-09-17 environment: Windows 11 Education 26200.9457. Inventory shows only
legacy UART COM1 (`ACPI\PNP0501\0`); it was not opened. There is no configured
com0com pair, representative USB serial device or Bluetooth SPP equipment, and
no Windows 10 target in this workspace/session. The existing WO-04 runner records
three grouped ENVIRONMENT_BLOCKED cases in `hardware-current/results.json`.
Its static capability table is conservative: unavailable/unknown custom
capabilities cannot produce a pass. These W seats deliberately have no passing
placeholder command; a real configured qualification workload must replace them.

| OS/transport | Delayed/abandoned open | Pending read / silent stall | Hard physical loss / reconnect | In-flight write / explicit close | Host close / launcher / panel-hide / screens / recording-export-replay |
| --- | --- | --- | --- | --- | --- |
| Windows 10 com0com | BLOCKED | BLOCKED | BLOCKED | BLOCKED | BLOCKED |
| Windows 11 com0com | BLOCKED | BLOCKED | BLOCKED | BLOCKED | BLOCKED |
| Windows 10 representative USB | BLOCKED | BLOCKED | BLOCKED | BLOCKED | BLOCKED |
| Windows 11 representative USB | BLOCKED | BLOCKED | BLOCKED | BLOCKED | BLOCKED |
| Windows 10 representative Bluetooth SPP | BLOCKED | BLOCKED | BLOCKED | BLOCKED | BLOCKED |
| Windows 11 representative Bluetooth SPP | BLOCKED | BLOCKED | BLOCKED | BLOCKED | BLOCKED |

For each configured seat, use a receive-only fixture at the selected baud's wire
capacity, with independent known text values and counts. Record device/adapter,
driver, OS build, port pair, firmware and candidate hashes. Drive the real
SF_Telem connection and screen/record/export commands; inject bytes from the
mate port or representative device. Never substitute a standalone SerialPort
for the root/shared-service chain. Do not issue motor or control commands.

Repeat each physical disconnect/close scenario at least 20 times per OS/device,
crossing monitor, recording, pending export, autosave and loaded replay with
actual window close and return-to-launcher, then panel hiding and screen
switching followed by close. Panel hiding is the actual Home command; Serial
Link has no independent close button. Script/coordinated mate-port handshakes
or device-side acknowledgements must establish pending operations and losses;
elapsed sleeps alone do not establish a transition schedule.

For USB remove/reinsert the device; for SPP power off/disconnect the peer and
restore it. Test peer silence separately from hard removal. For virtual COM,
coordinate mate closure/recreation plus delayed-open behavior actually supported
by that driver; do not label an unavailable driver behavior as tested. Observe
reconnect and parser reset with a valid frame after a partial old-session frame.
Record driver cancellation return and overlapped completion separately.

Measure close request through normal process exit, approved limit 2 s; measure
declared pending export separately, approved limit 30 s. Preserve exit code,
accepted/rejected/per-ESC/overflow/discard counts, thread/handle/callback balance,
file validation and any hang/crash evidence. Timeout or process kill is FAILED;
manual process kill is never a pass. Verify no post-unload callback. A permanently
wedged open may outlive the root only with the documented independent job/module
lifetime; a wedged read/write cannot be dismissed by destroying borrowed storage.

No physical iteration was run and no physical driver deadline is certified by
the fake-transport software results. The two-hour export-volume/durability fixture
belongs to WO-06; the WO-05 matrix proves lifetime ordering with small real saves.
