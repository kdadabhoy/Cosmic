# WO-09 F-CORRUPT regression fixtures

Every file here is a malformed document that a Cosmic parser must HANDLE (reject,
or accept into a consistent object) without a crash, an uncaught exception or a
deadline overrun. They are loaded by name-extension in the C04/C05 fixture cases
(`tests/test_wo09_c04_graphs.cpp`, `tests/test_wo09_c05_json.cpp`):

| Extension | Parser under test |
| --- | --- |
| `.cflow`   | `FlowAsset::LoadFromString` |
| `.cstory`  | `StoryGraph::LoadFromString` |
| `.cscene`  | `SceneSerializer::LoadFromString` |
| `.cprefab` | `SceneSerializer::InstantiatePrefab` |
| `.cmat`    | `SceneSerializer::LoadReflectedFromString` (MaterialAsset) |
| `.toml`    | `Config::Parse` |

Files named `fuzz-<parser>-<seed>-<case>.*` are MINIMIZED seeded-fuzz failures
(the seed and case number identify the mutation in `tests/wo09_fuzz.h`); the
others are hand-written boundary cases. Changing or removing a fixture needs a
reviewed rationale (catalog 03, fixture register).

`header-bang.toml` (`[!x]`) and `fuzz-config-0x090570A1-539.toml` (a NUL spliced into a
`[[motors]]` header) are the KI-49 twins: both tripped a toml++ 3.4 assertion in a Debug build
before `Config::Parse` gained its table-header gate.
