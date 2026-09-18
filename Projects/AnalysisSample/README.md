# AnalysisSample — the X01 analysis reference project (WO-10, 2D stability)

A small **external Cosmic consumer**: one runtime plugin (`AnalysisSampleLayer`) that loads
two synthetic fixtures with the engine's existing primitives, animates a marker and trail,
plots position / speed and a 100,000-sample series with ImPlot, plays / pauses / scrubs on
the recorded time base, exports a still through the viewport framebuffer, and is packaged
and run through the editor's ordinary File ▸ Package path.

It is a **compatibility specimen, not consumer qualification** (decision **D-9km**): the
fixtures are the catalog's synthetic `F-TRAJECTORY` and `F-SERIES-LARGE`; no real to-9km
schema, units or volume is represented, and nothing here claims a "qualified downstream
consumer". Real to-9km qualification remains deferred until real data exists.

## What it consumes, and how precision is handled

| Fixture | Source of truth | Path through the engine |
| --- | --- | --- |
| `F-TRAJECTORY` — `data/trajectory.csv`, 1,201 rows, `t = i/120`, `x = 30 t`, `y = 50 t − ½·9.80665·t²`, `vx = 30`, `vy = 50 − 9.80665 t` (SI) | **double** (written independently by `tests/fixtures/wo10/gen_trajectory.py`; verified against the equations on load) | `DataExport::LoadCSV` (restricted numeric CSV) → double arrays for ImPlot → the LOCAL coordinates recorded with `DataRecorder` (float, v1) → `DataPlayer` (float replay: play / pause / scrub / `SampleAt`) drives the marker |
| `F-SERIES-LARGE` — 100,000 samples × 8 finite channels + time at 1 kHz | **generated** by `src/AnalysisFixtures.h` from the equations + seed (`0x0A105E21`, PCG32) recorded in its metadata; never committed as a blob | double arrays for ImPlot; extrema + discontinuity markers written to `series-large.meta.json` |

Scientific values stay `double`. The trajectory lives in a world frame with a **1e11 m
origin offset**; the sample subtracts the origin **in double** (`LocalFrame::ToLocalX`) and
only then narrows to the `float` the renderer and the recorder take. That reproduces every
local value to half the double ulp at 1e11 (7.6 µm); converting the world value to float
first would be off by up to 300 m (a float has 8,192 m steps at 1e11). This tests the
sample's conversion — it is not an engine-wide large-coordinate promise
(`contracts/contracts.md` §5).

## Building it — outside the SDK checkout

The root SDK build deliberately **skips** this folder (`COSMIC_SKIP_PROJECTS`) and its
`CMakeLists.txt` refuses to be added in-tree. Configure it standalone against an SDK that has
already been built (`build/Runtime/Release/Cosmic.dll` + `.lib`):

```bash
cmake -S <copy-of-this-folder> -B <copy>/build -A x64 -DCOSMIC_SDK_DIR=C:/dev/Cosmic -DGAME_OUTPUT_DIR=<copy>/build
cmake --build <copy>/build --config Release --parallel
```

`<copy>/build/Release/AnalysisSample.dll` is the plugin. `COSMIC_2D_ONLY` defaults ON and
must match the SDK. This is exactly what the Starforge packager does for any external
project (`StarforgeApp::BeginPackage`), which is how the X01 acceptance builds it
(`tests/acceptance/fixtures/Run-WO10Sample.ps1`): the source is copied to
`build/wo10-sample-external/AnalysisSample`, the editor opens it and packages it, and the
staged `dist/AnalysisSample/AnalysisSample.exe` (CosmicApp.exe renamed + `Cosmic.dll` + the
plugin + `assets/projects/AnalysisSample/data/trajectory.csv` + `boot.cfg`) is run from a
different working directory.

To run it unpackaged from a dev tree: copy `data/` to
`build/Runtime/Release/assets/projects/AnalysisSample/data/` and start
`CosmicApp.exe --project <absolute path to AnalysisSample.dll>`.

## The in-app harness

`COSMIC_X01_SELFTEST=<result.json>` (+ optional `COSMIC_X01_OUTPUT=<dir>`) arms
`src/X01SelfTest.cpp`: load → play → pause → scrub at exact and midpoint sample times →
export PNG → JSON verdict, driven through the same transport functions the buttons call.
The acceptance wrapper then re-checks the JSON and the PNG out of process.
