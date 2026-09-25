# CosmicTests — headless unit tests (no window, no GL context).
# Built when COSMIC_BUILD_TESTS=ON (root CMakeLists). Uses the vendored doctest
# single header (Cosmic/dependencies/doctest). Run from build/Runtime/<CONFIG>/
# (the exe is placed next to Cosmic.dll) or via `ctest`.

# Shared tier (plan doc 28 §9.4) — since the AP-05 purge every file here is plain
# 2D source: the 3D-only TUs were deleted (part A) and the in-file COSMIC_2D_ONLY
# fences around the cases that reached for a 3D component are gone (part B).
set(COSMIC_TEST_SOURCES
    test_main.cpp
    test_framing.cpp
    test_spatial.cpp
    test_bufferlayout.cpp
    test_telemetry_roundtrip.cpp
    test_config.cpp
    test_filesystem_mounts.cpp
    test_exe_resources.cpp
    test_integrators.cpp
    test_lookuptable.cpp
    test_filters.cpp
    test_random.cpp
    test_noise.cpp
    test_audio.cpp
    test_components.cpp
    test_assetlibrary.cpp
    test_s5_navigation.cpp        # EDITOR-camera navigation, not pathfinding — both suites
    test_reflect.cpp
    test_material.cpp
    test_scene_serializer.cpp
    test_hierarchy.cpp
    test_scene_components.cpp
    test_scenemanager.cpp
    test_commandstack.cpp
    test_filewatcher.cpp
    test_scripthost.cpp
    # Physics is dimension agnostic (§4.1): every test_physics_* file runs here
    # (the terrain-collider suite left with the 3D purge, AP-05).
    test_physics_world.cpp
    test_physics_scene.cpp
    test_physics_events.cpp
    test_physics_character.cpp
    test_physics_determinism.cpp
    # Phase 29 W3 — the pluggable backend seam and the 2D physics contract.
    test_physics_backend.cpp
    test_physics_2d.cpp
    test_ui_rects.cpp
    test_flowmachine.cpp
    test_story.cpp
    test_camera2d.cpp
    test_tilemap.cpp
    test_template_scripts.cpp
    test_branding.cpp
    test_events.cpp
    # Phase 29 W2 — the regression net the engine split is built on top of.
    # 2D invariants (plan doc 28 §9.2):
    test_sprite_order.cpp
    test_light2d.cpp
    test_sprite_animation.cpp
    test_tilemap_extra.cpp
    test_ui_anchor.cpp
    test_timeline_state.cpp
    test_scene2d_determinism.cpp
    # Phase 29 W9 — telemetry / serial / SF_Telem hardening (plan doc 28 §9.6).
    # All shared tier: the telemetry and serial stacks are dimension agnostic,
    # and SF_Telem ships on both branches (decision 4), so these run in both
    # suites. test_crossbuild_scene asserts VERBATIM preservation of the 3D
    # component blocks a scene may carry (its 3D-engine half, which asserted
    # real component loading, went with the fences in AP-05).
    test_telemetry_robustness.cpp
    test_wo06.cpp
    # App Platform / AP-04 — PendulumLab's service compiled in-exe (F02 U, Y01 units)
    # and the template-scene / template-script smoke. Compiling the real service TU
    # (not a copy) is the point: the thing under test is the code the sample ships.
    test_pendulumlab.cpp
    ../Projects/PendulumLab/src/services/PendulumService.cpp
    ../Projects/Starforge/assets/templates/app/src/services/AppService.cpp
    ../Projects/Starforge/assets/templates/game/src/scripts/BouncingBall.cpp
    ../Projects/Starforge/assets/templates/game/src/scripts/PidController.cpp
    test_wo06_host.cpp
    test_serial_lifecycle.cpp
    # WO-05a (2D stability) — the reported COM close/link-loss crash: the reachable
    # ownership-chain teardown guard + reserved ENVIRONMENT_BLOCKED seats for the
    # connected-state repros WO-05 lights up once the WO-04 transport seam exists.
    test_serial_shutdown_race.cpp
    # WO-04 (2D stability) — the injectable transport seam's own coverage: drives the
    # real SerialPort/SerialLink state machine over FakeSerialTransport (tests/ only),
    # reaching the connected state and making KI-4's teardown join observable.
    test_serial_seam.cpp
    # WO-07 (2D stability) — L01 runtime-plugin (F-LIFETIME) teardown host. Skipped
    # by default (needs a real Application/window/GL); the WO-04 runner launches it.
    test_wo07_host.cpp
    # WO-07 L04 — teardown during live background activity (WO07TeardownFixture) and
    # the runtime-path registry lifecycle (WO07ModuleFixture). Skipped by default.
    test_wo07_l04.cpp
    # WO-07 P01 — ImPlot lifetime + known data under 50 plugin reloads (WO07PlotFixture).
    test_wo07_p01.cpp
    # WO-07 L05 — 200 scripted SF_Telem UI cycles with per-action stack balance (WO07UiCyclesFixture).
    test_wo07_l05.cpp
    # WO-08 (2D stability) — the headless halves of R04 (Camera2DController pan/zoom
    # anchors, DPI/offsets, zoom range, nonfinite policy, 641x359, changing viewport)
    # and R06 (CPU PNG encode/decode against an independent hand-built PNG oracle).
    test_wo08_camera.cpp
    test_wo08_png.cpp
    # WO-09 (2D stability) — authored 2D content + shared services, the headless
    # halves of C01–C06 (sprites, tilemaps, UI, flow/story/events + parser fuzz,
    # scene/prefab/material/config JSON + F-CORRUPT, EnTT/jobs/scripts/assets/
    # watcher/audio lifetimes). Shared helpers: wo09_tilemap_oracle.h, wo09_fuzz.h.
    test_wo09_c01_sprites.cpp
    test_wo09_c02_tilemap.cpp
    test_wo09_c03_ui.cpp
    test_wo09_c04_graphs.cpp
    test_wo09_c05_json.cpp
    test_wo09_c06_services.cpp
    # WO-10 (2D stability) — clocks and numerics. N01/N02 host cases drive the REAL
    # Application scheduler over the injected IFrameClock (tests/FakeFrameClock.h)
    # with the WO10ClockFixture plugin; skipped by default, one runner child per rung.
    # N02's headless half, N03 (analytic integrators + the F-TRAJECTORY double vs
    # float path) and N04 (PCG / determinism) run in the ordinary suite.
    test_wo10_n01_dispatch.cpp
    test_wo10_n02_clock.cpp
    test_wo10_n03_numerics.cpp
    test_wo10_n04_determinism.cpp
    # AP-01 (App Platform) — the foundation tier: the host-owned DataBus (V01), the
    # AppService / ServiceHost / CS_SERVICE / CS_PANEL tier with its in-exe and
    # DLL-reload halves (V02; the reload halves drive the REAL GameModule TU below and,
    # skipped by default, the real PlayerLayer through an Application), and the flow
    # channel guards / when / StartAt / key bridge + the Data() script proxy (V06).
    test_databus.cpp
    test_servicehost.cpp
    test_flow_channels.cpp
    # AP-02 (App Platform) — bound widgets: the pure helpers, the slider/toggle Update
    # machine over a real DataBus, CollectHostedPanels, reflection + serializer round-trip (V04, V05).
    test_ui_widgets.cpp
    # AP-01 V02 W — the REAL editor-side module loader (LoadLibrary + CosmicModule_Register,
    # UnregisterModule + FreeLibrary), compiled from the Starforge tree rather than copied:
    # the thing under test is the code the editor ships.
    ../Projects/Starforge/src/GameModule.cpp
    # AP-03 (App Platform) — the headless halves of E03 (UiRectGizmoMath), E08 (SourceLocator)
    # and E02 (ScreenScaffold text edits), compiled from the Starforge tree like GameModule.cpp.
    test_ap03_editor.cpp
    ../Projects/Starforge/src/UiRectGizmo.cpp
    ../Projects/Starforge/src/SourceLocator.cpp
    ../Projects/Starforge/src/ScreenScaffold.cpp
    test_wo05_matrix.cpp
    test_wo05_kiss.cpp
    test_wo05_host.cpp
    test_sftelem_protocol.cpp
    test_sftelem_hub.cpp
    test_crossbuild_scene.cpp
    # W9 refactored TelemHub::PumpSerial's pure half into the public
    # IngestChunk, so the framing/routing path can be driven without a COM port.
    # Compiling the real TU (rather than a test copy) is the point: the thing
    # under test is the code SF_Telem actually ships. imgui/implot are PUBLIC on
    # the Cosmic target, so this links without a GL context ever being created.
    ../Projects/SF_Telem/src/TelemHub.cpp
    ../Projects/SF_Telem/src/SF_Telem.cpp
    ../Projects/SF_Telem/src/TestingManager.cpp
    ../Projects/SF_Telem/src/TestHub.cpp
    ../Projects/SF_Telem/src/TestLayers.cpp
    ../Projects/SF_Telem/src/MainLayer.cpp
    ../Projects/SF_Telem/src/ReplayLayer.cpp
    ../Projects/SF_Telem/src/DrivetrainLayer.cpp
    ../Projects/SF_Telem/src/DashboardView.cpp
)

add_executable(CosmicTests ${COSMIC_TEST_SOURCES})

execute_process(COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
    "${CMAKE_CURRENT_SOURCE_DIR}/acceptance/fixtures/Generate-KissHarness.ps1"
    -Repo "${COSMIC_SDK_DIR}" -Output "${CMAKE_CURRENT_BINARY_DIR}/wo05_kiss.inc"
    COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${COSMIC_SDK_DIR}/Projects/SF_Telem/src/FirmwareTemplates.h")
target_include_directories(CosmicTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

file(GLOB WO05_SF_SOURCES "${COSMIC_SDK_DIR}/Projects/SF_Telem/src/*.cpp")
add_library(WO05HostFixture SHARED WO05HostFixture.cpp ${WO05_SF_SOURCES})
target_link_libraries(WO05HostFixture PRIVATE Cosmic)
target_compile_definitions(WO05HostFixture PRIVATE COSMIC_WO05_HOST_FIXTURE)
set_target_properties(WO05HostFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY
    "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>")
add_dependencies(CosmicTests WO05HostFixture)
add_library(WO06HostFixture SHARED WO06HostFixture.cpp ${WO05_SF_SOURCES})
target_link_libraries(WO06HostFixture PRIVATE Cosmic)
target_compile_definitions(WO06HostFixture PRIVATE COSMIC_WO05_HOST_FIXTURE)
set_target_properties(WO06HostFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY
    "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>")
add_dependencies(CosmicTests WO06HostFixture)

# WO-07 (2D stability) — the F-LIFETIME runtime plugin (L01). A standalone project
# DLL that owns one of every leakable resource class; test_wo07_host loads/unloads it.
add_library(WO07LifetimeFixture SHARED WO07LifetimeFixture.cpp)
target_link_libraries(WO07LifetimeFixture PRIVATE Cosmic)
set_target_properties(WO07LifetimeFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY
    "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>")
add_dependencies(CosmicTests WO07LifetimeFixture)

# WO-07 L03 — a project DLL that loads but exports none of the engine plugin
# signatures, so LoadProjectDLL must reject it and fall back to the launcher.
add_library(WO07NoExport SHARED WO07NoExport.cpp)
set_target_properties(WO07NoExport PROPERTIES RUNTIME_OUTPUT_DIRECTORY
    "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>")
add_dependencies(CosmicTests WO07NoExport)

# WO-07 L04 — the teardown-during-live-activity plugin (job blocked on an exe-owned
# barrier, file watcher, serial over FakeSerialTransport, selection/log/hotkey
# listeners, EventBus, GPU objects) and a REAL CS_MODULE game module hosted by the
# real PlayerLayer (registry lifecycle across repeated loads).
add_library(WO07TeardownFixture SHARED WO07TeardownFixture.cpp)
target_link_libraries(WO07TeardownFixture PRIVATE Cosmic)
set_target_properties(WO07TeardownFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY
    "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>")
add_dependencies(CosmicTests WO07TeardownFixture)
add_library(WO07ModuleFixture SHARED WO07ModuleFixture.cpp)
target_link_libraries(WO07ModuleFixture PRIVATE Cosmic)
set_target_properties(WO07ModuleFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY
    "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>")
add_dependencies(CosmicTests WO07ModuleFixture)

# WO-07 P01 — the ImPlot known-data plugin (adopts the host contexts; plots the known
# series and inspects limits, drawn geometry and presented pixels).
add_library(WO07PlotFixture SHARED WO07PlotFixture.cpp)
target_link_libraries(WO07PlotFixture PRIVATE Cosmic)
set_target_properties(WO07PlotFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY
    "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>")
add_dependencies(CosmicTests WO07PlotFixture)

# WO-07 L05 — the real SF_Telem root over the fake transport, scripted through its real
# Navigation / Replay buttons with the per-action ImGui balance oracle (WO07UiOracle.h).
add_library(WO07UiCyclesFixture SHARED WO07UiCyclesFixture.cpp ${WO05_SF_SOURCES})
target_link_libraries(WO07UiCyclesFixture PRIVATE Cosmic)
target_compile_definitions(WO07UiCyclesFixture PRIVATE COSMIC_WO05_HOST_FIXTURE)
set_target_properties(WO07UiCyclesFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY
    "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>")
add_dependencies(CosmicTests WO07UiCyclesFixture)

# AP-01 V02 W — a real CS_MODULE game module that registers a CS_SERVICE (which
# registers a CS_PANEL from OnAttach) and a CS_SCRIPT; test_servicehost loads and
# unloads it 20 times through the real GameModule TU (in-exe, ordinary suite) and,
# skipped by default, through the real Application / PlayerLayer path.
add_library(AP01ServiceFixture SHARED AP01ServiceFixture.cpp)
target_link_libraries(AP01ServiceFixture PRIVATE Cosmic)
set_target_properties(AP01ServiceFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY
    "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>")
add_dependencies(CosmicTests AP01ServiceFixture)

# WO-10 N01/N02 — the clock-probe runtime plugin: a real project DLL hosted by the
# real WorkspaceLayer that records what the production dispatch hands a plugin
# layer (OnFixedUpdate / OnUpdate / local time) into the exe-owned WO10ClockReport.
add_library(WO10ClockFixture SHARED WO10ClockFixture.cpp)
target_link_libraries(WO10ClockFixture PRIVATE Cosmic)
set_target_properties(WO10ClockFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY
    "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>")
add_dependencies(CosmicTests WO10ClockFixture)

target_link_libraries(CosmicTests PRIVATE Cosmic)
target_include_directories(CosmicTests PRIVATE "${COSMIC_SDK_DIR}/Cosmic/dependencies/doctest")
# WO-09 — the fuzzer (tests/wo09_fuzz.h) mutates documents through nlohmann::json,
# which the engine keeps PRIVATE; the tests reach the single header directly.
target_include_directories(CosmicTests PRIVATE "${COSMIC_SDK_DIR}/Cosmic/dependencies")

# Newer MSVC (C5285) rejects doctest's forward-declarations of std types in
# namespace std; this makes doctest include the real <string>/<tuple> instead.
target_compile_definitions(CosmicTests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
target_compile_definitions(CosmicTests PRIVATE COSMIC_WO06_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/acceptance/fixtures/wo06")
# WO-09 — the F-CONTENT project + F-CORRUPT regression fixtures (source tree, committed).
target_compile_definitions(CosmicTests PRIVATE COSMIC_WO09_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/wo09")
# WO-10 — the independent generators/oracles (tests/fixtures/wo10) and the analysis
# sample's committed F-TRAJECTORY reference CSV (Projects/AnalysisSample/data).
target_compile_definitions(CosmicTests PRIVATE COSMIC_WO10_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/wo10")
target_compile_definitions(CosmicTests PRIVATE COSMIC_ANALYSIS_SAMPLE_DATA="${COSMIC_SDK_DIR}/Projects/AnalysisSample/data")
# The analysis sample's fixture definitions (F-TRAJECTORY / F-SERIES-LARGE generators
# + the LocalFrame conversion) are header-only; N03/X01 evaluate them against the
# catalog equations independently of the sample.
target_include_directories(CosmicTests PRIVATE "${COSMIC_SDK_DIR}/Projects/AnalysisSample/src")
# App Platform / AP-04 — the F-PENDULUM reference (tests/fixtures/ap04, generated by the
# checked-in script), the template trees the scene-load test walks, and PendulumLab
# (its flow for F02 and its service headers for the in-exe compile).
target_compile_definitions(CosmicTests PRIVATE COSMIC_AP04_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ap04")
target_compile_definitions(CosmicTests PRIVATE COSMIC_AP04_TEMPLATES="${COSMIC_SDK_DIR}/Projects/Starforge/assets/templates")
target_compile_definitions(CosmicTests PRIVATE COSMIC_PENDULUMLAB_DIR="${COSMIC_SDK_DIR}/Projects/PendulumLab")
target_include_directories(CosmicTests PRIVATE "${COSMIC_SDK_DIR}/Projects/PendulumLab/src")
target_include_directories(CosmicTests PRIVATE "${COSMIC_SDK_DIR}/Projects/Starforge/assets/templates/app/src")
# UX-02 — the committed fixture project (ED04 scene lister, the ED01 sprite-under-UI scene).
target_compile_definitions(CosmicTests PRIVATE COSMIC_UX02_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ux02")

# Land next to Cosmic.dll so the exe runs without PATH games.
set_target_properties(CosmicTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>"
)

add_test(NAME CosmicTests
    COMMAND CosmicTests
    WORKING_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>"
)
