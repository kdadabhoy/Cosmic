# CosmicTests

Headless unit tests for the engine — no window, no OpenGL context, no `Application`.
Framework: [doctest](https://github.com/doctest/doctest) (vendored single header at
`Cosmic/dependencies/doctest/doctest.h`, v2.4.12).

## Building

Built by default with the normal scripts (`build.bat` / `build_all.bat`) via the root
CMake option `COSMIC_BUILD_TESTS` (default `ON`). Disable with
`-DCOSMIC_BUILD_TESTS=OFF`. The test exe is **not** installed/packaged.

## Running

The exe lands next to `Cosmic.dll`:

```
build\Runtime\Debug\CosmicTests.exe            # run everything
build\Runtime\Debug\CosmicTests.exe -ts="*COBS*"   # filter by test-suite/name
```

or via CTest from the build directory: `ctest -C Debug --output-on-failure`.

## What's covered

| File | Covers |
| --- | --- |
| `test_framing.cpp` | `serial/Framing.h` — CRC16-CCITT check vector, COBS round-trips (incl. zeros and the 254-byte group boundary), malformed-input rejection, frame encode/decode + CRC corruption rejection |
| `test_spatial.cpp` | `math/Spatial.h` — Euler ZYX ↔ quaternion round-trips, body-rate integration, NED ↔ render frame mapping |
| `test_bufferlayout.cpp` | `graphics/Buffer.h` — offsets/stride/component counts, instanced flag |
| `test_telemetry_roundtrip.cpp` | `DataRecorder` → `scene.bin` → `DataPlayer` full binary round-trip, directory fallback (per-entity `.bin` without `scene.bin`), clean failure on empty dirs |

## Ground rules for new tests

- **Headless only** — anything needing a GL context or a window does not belong here.
- Tests write scratch files only under `%TEMP%/cosmic_tests/` and clean up after themselves.
- One `.cpp` per subsystem; register it in `tests/CMakeLists.txt`.
- Engine headers assume the engine PCH — include the std headers you need (`<string>`,
  `<vector>`, …) **before** engine headers in test files.
