# CosmicRenderTests — golden-image regression tests (Phase 29 W2 / plan doc 28 §9.5).
#
# Unlike CosmicTests this target opens a REAL window and a GL context: it renders
# 320x180 frames offscreen and compares them against the PNGs committed in
# goldens/. That makes it local-only (never CI — no GPU there) and inherently
# driver-specific, which is why it is gated behind COSMIC_BUILD_RENDER_TESTS=OFF
# at the root.
#
# Everything here is built from engine verbs (RendererAPI / RenderCommand /
# FrameBuffer / ImageIO), never raw GL, so tests/check_gl_conformance.ps1 — which
# scans tests/ too — stays green.
#
# Run:
#   build\Runtime\Debug\CosmicRenderTests.exe --reporters=console --no-intro
# Regenerate the goldens (a deliberate, reviewed act — see §9.5):
#   build\Runtime\Debug\CosmicRenderTests.exe --update-goldens

add_executable(CosmicRenderTests
    render_main.cpp
    GoldenImage.cpp
    render_2d.cpp
    render_3d.cpp
)

target_link_libraries(CosmicRenderTests PRIVATE Cosmic)
target_include_directories(CosmicRenderTests PRIVATE
    "${COSMIC_SDK_DIR}/Cosmic/dependencies/doctest"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Same doctest workaround CosmicTests needs: newer MSVC (C5285) rejects doctest's
# forward-declarations of std types in namespace std.
target_compile_definitions(CosmicRenderTests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)

# The goldens live in the SOURCE tree (they are committed), not next to the exe.
# Baked in as a define so the exe finds them from any working directory.
target_compile_definitions(CosmicRenderTests PRIVATE
    COSMIC_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/goldens")

# Land next to Cosmic.dll — the engine's POST_BUILD syncs assets/ there too, and
# the shaders load CWD-relative (render_main.cpp chdirs to the exe's directory).
set_target_properties(CosmicRenderTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>"
)

# Registered with ctest for convenience, but it is NOT part of the CI suite —
# the workflow only ever builds with COSMIC_BUILD_RENDER_TESTS off.
add_test(NAME CosmicRenderTests
    COMMAND CosmicRenderTests --reporters=console --no-intro
    WORKING_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>"
)
