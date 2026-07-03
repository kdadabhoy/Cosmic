# CosmicTests — headless unit tests (no window, no GL context).
# Built when COSMIC_BUILD_TESTS=ON (root CMakeLists). Uses the vendored doctest
# single header (Cosmic/dependencies/doctest). Run from build/Runtime/<CONFIG>/
# (the exe is placed next to Cosmic.dll) or via `ctest`.

add_executable(CosmicTests
    test_main.cpp
    test_framing.cpp
    test_spatial.cpp
    test_bufferlayout.cpp
    test_telemetry_roundtrip.cpp
    test_config.cpp
    test_integrators.cpp
    test_lookuptable.cpp
    test_filters.cpp
    test_random.cpp
    test_noise.cpp
    test_audio.cpp
    test_components.cpp
    test_assetlibrary.cpp
    test_s5_navigation.cpp
    test_phase10_world.cpp
)

target_link_libraries(CosmicTests PRIVATE Cosmic)
target_include_directories(CosmicTests PRIVATE "${COSMIC_SDK_DIR}/Cosmic/dependencies/doctest")

# Newer MSVC (C5285) rejects doctest's forward-declarations of std types in
# namespace std; this makes doctest include the real <string>/<tuple> instead.
target_compile_definitions(CosmicTests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)

# Land next to Cosmic.dll so the exe runs without PATH games.
set_target_properties(CosmicTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>"
)

add_test(NAME CosmicTests
    COMMAND CosmicTests
    WORKING_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>"
)
