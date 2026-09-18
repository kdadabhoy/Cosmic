# WO-05 parser, firmware and loss ledger

Pinned SF-Stable object: `fa6ed9fe6910f8a30e5e15561bd2a9b6b8b5d66e`.
The pin is saved in `../evidence/WO-05/sfstable-parity.txt`.
No checkout or edits to SF-Stable were made.

| Surface | WO-05 disposition | Evidence/oracle |
| --- | --- | --- |
| PC tagged `$R/$L/$W` text, XOR checksum, decoded channel equations | No parser/firmware source change | T01 all byte splits, one-byte, coalesced and seed-5 chunks; independent XOR and channel equations |
| Main's existing malformed text hardening | Retained; no intentional rejection-grammar difference introduced | T02 exact 4096/4097 purge and 1000 seeded rejects, delimiter recovery; all retained tests |
| Firmware KISS 10-byte packets and CRC8 | Unchanged shipping sketch compiled directly in tests | Independent CRC8/SMBUS vector `123456789 -> F4`, corruption, junk-prefix resync, all splits and production `sendFrame` into PC parsing |
| COBS/CRC16 engine frames | Separate surface; retained | Existing COBS and CRC-protection tests; never used as proof of KISS or tagged text |
| Non-polling screen receive queue | Intentional transport loss at 1 MiB | Preserve accepted prefix, discard excess; cumulative raw/overflow/close-discard counters checked exactly in T06 |
| Reconnect partial text | Intentional removal of stale previous-session bytes | Adopted session generation replaces sampled open edge; valid new-session frame accepted after old six-byte partial |

`Telemetry.h`, `FirmwareTemplates.h`, `TestFirmwareTemplates.h` and the firmware
directory have an empty diff against the pin, recorded in
`../evidence/WO-05/sfstable-parity-diff.txt`; protocol hashes are also recorded.
TelemHub's UI command bodies were extracted into callable production commands
without changing parsing/model calculations. Queue overflow is accounted loss,
not a newly permitted or rejected malformed frame. No firmware format, units,
checksum, baud/8N1 parameter or parser acceptance rule changed in WO-05.
