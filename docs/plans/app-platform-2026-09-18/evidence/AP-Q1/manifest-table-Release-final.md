
**manifests-Release-final** (`C:/dev/Cosmic/docs/plans/app-platform-2026-09-18/evidence/AP-Q1/manifests-Release-final`)

| Manifest | Profile | Cfg | Case | Tier | Verdict | Exit | Tests | s | Commit | Detail |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ap01-units | release | Release | V01 | U | PASSED | 0 | 13 | 0.6 | fa1223a | exit 0 as expected |
| ap01-units | release | Release | V02-U | U | PASSED | 0 | 10 | 0.6 | fa1223a | exit 0 as expected |
| ap01-units | release | Release | V02-W-PLAYER | W | PASSED | 0 | 1 | 1.4 | fa1223a | exit 0 as expected |
| ap01-units | release | Release | V06 | U | PASSED | 0 | 10 | 0.6 | fa1223a | exit 0 as expected |
| ap02-gpu | release | Release | V03 | G | PASSED | 0 | 7 | 1.0 | fa1223a | exit 0 as expected |
| ap02-gpu | release | Release | E05 | G | PASSED | 0 | 1 | 0.8 | fa1223a | exit 0 as expected |
| ap02-units | release | Release | V04 | U | PASSED | 0 | 11 | 0.6 | fa1223a | exit 0 as expected |
| ap03-editor | release | Release | AP03-EDITOR | I | PASSED | 0 | 2 | 174.1 | fa1223a | exit 0 as expected |
| ap04-sample | release | Release | Y01 | W | PASSED | 0 |  | 36.0 | fa1223a | exit 0 as expected |
| ap04-units | release | Release | Y01-U | U | PASSED | 0 | 5 | 0.7 | fa1223a | exit 0 as expected |
| ap04-units | release | Release | F02-U | U | PASSED | 0 | 3 | 0.6 | fa1223a | exit 0 as expected |
| ap04-units | release | Release | E01-U | U | PASSED | 0 | 6 | 0.6 | fa1223a | exit 0 as expected |
| ap05-purge | release | Release | B06 | U | PASSED | 0 | 5 | 4.0 | fa1223a | exit 0 as expected |
| apq1-y02 | release | Release | Y02 | I | PASSED | 0 | 3 | 39.1 | fa1223a | exit 0 as expected |
| pr-blocked | pr | Release | GOLDENS-G | G | PASSED | 0 | 6 | 0.9 | fa1223a | exit 0 as expected |
| pr-blocked | pr | Release | EDITOR-I | I | ENVIRONMENT_BLOCKED |  |  | 0 | fa1223a | missing capability: editor-ui |
| pr-blocked | pr | Release | SOAK-Q | Q | ENVIRONMENT_BLOCKED |  |  | 0 | fa1223a | missing capability: serial-device |
| pr-units | pr | Release | T01-PC-text | U | PASSED | 0 | 33 | 0.2 | fa1223a | exit 0 as expected |
| pr-units | pr | Release | T02-PC-faults | U | PASSED | 0 | 1 | 0.1 | fa1223a | exit 0 as expected |
| pr-units | pr | Release | T03-write-close | U | PASSED | 0 | 1 | 0.0 | fa1223a | exit 0 as expected |
| pr-units | pr | Release | T03-late-open | U | PASSED | 0 | 1 | 11.2 | fa1223a | exit 0 as expected |
| pr-units | pr | Release | T03-owner-cancel | U | PASSED | 0 | 1 | 11.0 | fa1223a | exit 0 as expected |
| pr-units | pr | Release | T06-burst-reconnect | U | PASSED | 0 | 8 | 0.1 | fa1223a | exit 0 as expected |
| pr-units | pr | Release | D01 | U | PASSED | 0 | 1 | 0.1 | fa1223a | exit 0 as expected |
| pr-units | pr | Release | D01-IDEMPOTENT | U | PASSED | 0 | 1 | 0.3 | fa1223a | exit 0 as expected |
| pr-units | pr | Release | D02 | U | PASSED | 0 | 3 | 0.0 | fa1223a | exit 0 as expected |
| pr-units | pr | Release | D03 | U | PASSED | 0 | 2 | 2.1 | fa1223a | exit 0 as expected |
| pr-units | pr | Release | D04 | U | PASSED | 0 | 3 | 0.1 | fa1223a | exit 0 as expected |
| pr-units | pr | Release | R04-U | U | PASSED | 0 | 8 | 0.0 | fa1223a | exit 0 as expected |
| pr-units | pr | Release | R06-U | U | PASSED | 0 | 4 | 0.1 | fa1223a | exit 0 as expected |
| pr-units | pr | Release | N02-U | U | PASSED | 0 | 3 | 0.9 | fa1223a | exit 0 as expected |
| pr-units | pr | Release | N03 | U | PASSED | 0 | 7 | 0.6 | fa1223a | exit 0 as expected |
| pr-units | pr | Release | N04 | U | PASSED | 0 | 3 | 0.7 | fa1223a | exit 0 as expected |
| pr-units | pr | Release | N04-FILTERS | U | PASSED | 0 | 9 | 0.6 | fa1223a | exit 0 as expected |
| pr-units | pr | Release | N04-LOOKUP | U | PASSED | 0 | 7 | 0.6 | fa1223a | exit 0 as expected |
| pr-units | pr | Release | N04-SCENE | U | PASSED | 0 | 2 | 0.6 | fa1223a | exit 0 as expected |
| pr-windows | pr | Release | D05 | W | PASSED | 0 | 8 | 0.2 | fa1223a | exit 0 as expected |
| pr-windows | pr | Release | D06 | W | PASSED | 0 | 5 | 0.2 | fa1223a | exit 0 as expected |
| pr-windows | pr | Release | D01-genuine-SF-Stable | W | ENVIRONMENT_BLOCKED |  |  | 0 | fa1223a | missing capability: sf-stable-v1-capture |
| selftest-selftestmode | pr | Release | H01a-intentional-nonzero-exit | W | FAILED | 7 |  | 0.3 | fa1223a | nonzero-exit: exit 7, expected 0 |
| selftest-selftestmode | pr | Release | H01b-crashing-fixture | W | FAILED | -2146232797 |  | 2.6 | fa1223a | crash: exit -2146232797, expected 0 |
| selftest-selftestmode | pr | Release | H01c-passing-fixture | W | PASSED | 0 |  | 0.2 | fa1223a | exit 0 as expected |
| selftest-selftestmode | pr | Release | H02-hanging-child-times-out | W | TIMEOUT |  |  | 2.1 | fa1223a | exceeded per-case deadline of 2s; child tree killed, parent survived |
| selftest-selftestmode | pr | Release | H03-missing-fixture | U | MISSING |  |  | 0.0 | fa1223a | missing fixture: C:\dev\Cosmic\tests\acceptance\fixtures\does-not-exist.ps1 |
| selftest-selftestmode | pr | Release | H03-missing-test-zero-count | U | FAILED | 0 | 0 | 0.0 | fa1223a | expected >= 1 tests, got 0 (exit 0, wanted 0) |
| selftest-selftestmode | pr | Release | H04-fake-transport-runs | W | PASSED | 0 | 5 | 0.3 | fa1223a | exit 0 as expected |
| selftest-selftestmode | pr | Release | Hnormal-minimal-pass | U | PASSED | 0 | 1 | 0.0 | fa1223a | exit 0 as expected |
| wo05 | pr | Release | T01-PC-text | U | PASSED | 0 | 33 | 0.2 | fa1223a | exit 0 as expected |
| wo05 | pr | Release | T03-root-matrix | U | PASSED | 0 | 1 | 315.7 | fa1223a | exit 0 as expected |
| wo05 | pr | Release | T03-late-open | U | PASSED | 0 | 1 | 11.1 | fa1223a | exit 0 as expected |
| wo05 | pr | Release | T03-write-close | U | PASSED | 0 | 1 | 0.0 | fa1223a | exit 0 as expected |
| wo05 | pr | Release | Retained-suite | U | PASSED | 0 | 507 | 22.5 | fa1223a | exit 0 as expected |
| wo05 | pr | Release | T03-owner-cancel | U | PASSED | 0 | 1 | 11.0 | fa1223a | exit 0 as expected |
| wo06 | pr | Release | D01 | U | PASSED | 0 | 1 | 0.1 | fa1223a | exit 0 as expected |
| wo06 | pr | Release | D02 | U | PASSED | 0 | 3 | 0.1 | fa1223a | exit 0 as expected |
| wo06 | pr | Release | D03 | U | PASSED | 0 | 2 | 1.9 | fa1223a | exit 0 as expected |
| wo06 | pr | Release | D04 | U | PASSED | 0 | 3 | 0.1 | fa1223a | exit 0 as expected |
| wo06 | pr | Release | D05 | W | PASSED | 0 | 8 | 0.2 | fa1223a | exit 0 as expected |
| wo06 | pr | Release | D06 | W | PASSED | 0 | 5 | 0.2 | fa1223a | exit 0 as expected |
| wo06 | pr | Release | D05-two-hour | W | PASSED | 0 | 1 | 2.0 | fa1223a | exit 0 as expected |
| wo07-ki1 | release | Release | KI-1 | I | PASSED | 0 | 1 | 0.9 | fa1223a | exit 0 as expected |
| wo07-l01 | release | Release | L01 | G | PASSED | 0 | 110 | 70.3 | fa1223a | exit 0 as expected |
| wo07-l02 | release | Release | L02 | I | PASSED | 0 | 2 | 424.5 | fa1223a | exit 0 as expected |
| wo07-l03 | release | Release | L03 | G | PASSED | 0 | 15 | 5.7 | fa1223a | exit 0 as expected |
| wo07-l04 | release | Release | L04 | G | PASSED | 0 | 50 | 36.1 | fa1223a | exit 0 as expected |
| wo07-l05 | release | Release | L05-sftelem | I | PASSED | 0 | 1 | 14.1 | fa1223a | exit 0 as expected |
| wo07-l05 | release | Release | L05-editor | I | PASSED | 0 | 1 | 17.4 | fa1223a | exit 0 as expected |
| wo07-p01 | release | Release | P01 | G | PASSED | 0 | 3 | 4.7 | fa1223a | exit 0 as expected |
| wo08-gpu | release | Release | R01 | G | PASSED | 0 | 2 | 0.9 | fa1223a | exit 0 as expected |
| wo08-gpu | release | Release | R02 | G | PASSED | 0 | 7 | 1.7 | fa1223a | exit 0 as expected |
| wo08-gpu | release | Release | R03 | G | PASSED | 0 | 5 | 1.0 | fa1223a | exit 0 as expected |
| wo08-gpu | release | Release | R04-G | G | PASSED | 0 | 4 | 1.2 | fa1223a | exit 0 as expected |
| wo08-gpu | release | Release | R05 | G | PASSED | 0 | 2 | 1.5 | fa1223a | exit 0 as expected |
| wo08-gpu | release | Release | R06-G | G | PASSED | 0 | 2 | 0.8 | fa1223a | exit 0 as expected |
| wo08-r07 | release | Release | R07 | Q | PASSED | 0 | 2 | 141.1 | fa1223a | exit 0 as expected |
| wo08-retained | release | Release | retained-units | U | PASSED | 0 | 507 | 174.1 | fa1223a | exit 0 as expected |
| wo08-retained | release | Release | retained-goldens | G | PASSED | 0 | 6 | 0.8 | fa1223a | exit 0 as expected |
| wo08-units | release | Release | R04-U | U | PASSED | 0 | 8 | 0.1 | fa1223a | exit 0 as expected |
| wo08-units | release | Release | R06-U | U | PASSED | 0 | 4 | 0.1 | fa1223a | exit 0 as expected |
| wo09-editor | release | Release | C05-I | I | PASSED | 0 | 2 | 1.1 | fa1223a | exit 0 as expected |
| wo09-gpu | release | Release | C01-G | G | PASSED | 0 | 3 | 1.0 | fa1223a | exit 0 as expected |
| wo09-gpu | release | Release | C02-G | G | PASSED | 0 | 3 | 1.0 | fa1223a | exit 0 as expected |
| wo09-gpu | release | Release | C03-G | G | PASSED | 0 | 3 | 1.4 | fa1223a | exit 0 as expected |
| wo09-retained | release | Release | retained-units | U | PASSED | 0 | 479 | 174.9 | fa1223a | exit 0 as expected |
| wo09-retained | release | Release | retained-goldens | G | PASSED | 0 | 6 | 1.0 | fa1223a | exit 0 as expected |
| wo09-retained | release | Release | retained-wo08-gpu | G | PASSED | 0 | 22 | 4.4 | fa1223a | exit 0 as expected |
| wo09-units | release | Release | C01-U | U | PASSED | 0 | 8 | 1.2 | fa1223a | exit 0 as expected |
| wo09-units | release | Release | C02-U | U | PASSED | 0 | 5 | 0.8 | fa1223a | exit 0 as expected |
| wo09-units | release | Release | C03-U | U | PASSED | 0 | 6 | 2.2 | fa1223a | exit 0 as expected |
| wo09-units | release | Release | C04 | U | PASSED | 0 | 8 | 6.5 | fa1223a | exit 0 as expected |
| wo09-units | release | Release | C05-U | U | PASSED | 0 | 7 | 22.4 | fa1223a | exit 0 as expected |
| wo09-units | release | Release | C05-XB | U | PASSED | 0 | 3 | 0.6 | fa1223a | exit 0 as expected |
| wo09-units | release | Release | C06-U | U | PASSED | 0 | 6 | 0.8 | fa1223a | exit 0 as expected |
| wo09-units | release | Release | C06-AUDIO | W | PASSED | 0 | 1 | 1.2 | fa1223a | exit 0 as expected |
| wo10-host | release | Release | N01-30hz | W | PASSED | 0 | 1 | 1.3 | fa1223a | exit 0 as expected |
| wo10-host | release | Release | N01-60hz | W | PASSED | 0 | 1 | 1.3 | fa1223a | exit 0 as expected |
| wo10-host | release | Release | N01-144hz | W | PASSED | 0 | 1 | 1.7 | fa1223a | exit 0 as expected |
| wo10-host | release | Release | N01-irregular | W | PASSED | 0 | 1 | 1.2 | fa1223a | exit 0 as expected |
| wo10-host | release | Release | N01-speed0 | W | PASSED | 0 | 1 | 1.3 | fa1223a | exit 0 as expected |
| wo10-host | release | Release | N01-speed025 | W | PASSED | 0 | 1 | 1.3 | fa1223a | exit 0 as expected |
| wo10-host | release | Release | N01-speed4 | W | PASSED | 0 | 1 | 1.3 | fa1223a | exit 0 as expected |
| wo10-host | release | Release | N01-pause | W | PASSED | 0 | 1 | 1.4 | fa1223a | exit 0 as expected |
| wo10-host | release | Release | N01-stall | W | PASSED | 0 | 1 | 1.3 | fa1223a | exit 0 as expected |
| wo10-host | release | Release | N01-local-quarter | W | PASSED | 0 | 1 | 1.3 | fa1223a | exit 0 as expected |
| wo10-host | release | Release | N01-global-quarter | W | PASSED | 0 | 1 | 1.3 | fa1223a | exit 0 as expected |
| wo10-host | release | Release | N02-origin-0 | W | PASSED | 0 | 1 | 1.7 | fa1223a | exit 0 as expected |
| wo10-host | release | Release | N02-origin-2h | W | PASSED | 0 | 1 | 1.7 | fa1223a | exit 0 as expected |
| wo10-host | release | Release | N02-origin-24h | W | PASSED | 0 | 1 | 1.7 | fa1223a | exit 0 as expected |
| wo10-host | release | Release | N02-policy-hz | W | PASSED | 0 | 1 | 1.4 | fa1223a | exit 0 as expected |
| wo10-host | release | Release | N02-policy-scale-nan | W | PASSED | 0 | 1 | 1.2 | fa1223a | exit 0 as expected |
| wo10-host | release | Release | N02-policy-scale-negative | W | PASSED | 0 | 1 | 1.2 | fa1223a | exit 0 as expected |
| wo10-host | release | Release | N02-policy-scale-inf | W | PASSED | 0 | 1 | 1.2 | fa1223a | exit 0 as expected |
| wo10-retained | release | Release | retained-units | U | PASSED | 0 | 466 | 173.9 | fa1223a | exit 0 as expected |
| wo10-retained | release | Release | wo09-units | U | PASSED | 0 | 40 | 32.3 | fa1223a | exit 0 as expected |
| wo10-retained | release | Release | wo10-units-all | U | PASSED | 0 | 13 | 0.9 | fa1223a | exit 0 as expected |
| wo10-retained | release | Release | retained-goldens | G | PASSED | 0 | 6 | 0.9 | fa1223a | exit 0 as expected |
| wo10-retained | release | Release | retained-wo08-gpu | G | PASSED | 0 | 22 | 3.2 | fa1223a | exit 0 as expected |
| wo10-retained | release | Release | retained-wo09-gpu | G | PASSED | 0 | 9 | 1.5 | fa1223a | exit 0 as expected |
| wo10-sample | release | Release | X01 | I | PASSED | 0 | 4 | 40.9 | fa1223a | exit 0 as expected |
| wo10-units | release | Release | N02-U | U | PASSED | 0 | 3 | 1.0 | fa1223a | exit 0 as expected |
| wo10-units | release | Release | N03 | U | PASSED | 0 | 7 | 0.6 | fa1223a | exit 0 as expected |
| wo10-units | release | Release | N04 | U | PASSED | 0 | 3 | 0.7 | fa1223a | exit 0 as expected |
| wo10-units | release | Release | N04-FILTERS | U | PASSED | 0 | 9 | 0.6 | fa1223a | exit 0 as expected |
| wo10-units | release | Release | N04-LOOKUP | U | PASSED | 0 | 7 | 0.6 | fa1223a | exit 0 as expected |
| wo10-units | release | Release | N04-SCENE | U | PASSED | 0 | 2 | 0.6 | fa1223a | exit 0 as expected |

Counts: {'PASSED': 117, 'ENVIRONMENT_BLOCKED': 3, 'FAILED': 3, 'TIMEOUT': 1, 'MISSING': 1}
