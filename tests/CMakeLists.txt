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
)

target_link_libraries(CosmicTests PRIVATE Cosmic)
target_include_directories(CosmicTests PRIVATE "${COSMIC_SDK_DIR}/Cosmic/dependencies/doctest")

# Land next to Cosmic.dll so the exe runs without PATH games.
set_target_properties(CosmicTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>"
)

add_test(NAME CosmicTests
    COMMAND CosmicTests
    WORKING_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>"
)
