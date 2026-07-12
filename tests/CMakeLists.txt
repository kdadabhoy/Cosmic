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
    test_s5_navigation.cpp
    test_phase10_world.cpp
    test_flycamera.cpp
    test_frustum.cpp
    test_presets.cpp
    test_render_queue.cpp
    test_reflect.cpp
    test_primitives.cpp
    test_meshimport.cpp
    test_animation.cpp
    test_material.cpp
    test_scene_serializer.cpp
    test_hierarchy.cpp
    test_scene_components.cpp
    test_scenemanager.cpp
    test_commandstack.cpp
    test_filewatcher.cpp
    test_scripthost.cpp
    test_worldsystems.cpp
    test_physics_world.cpp
    test_physics_scene.cpp
    test_physics_events.cpp
    test_physics_character.cpp
    test_physics_terrain.cpp
    test_physics_determinism.cpp
    test_ui_rects.cpp
    test_flowmachine.cpp
    test_camera2d.cpp
    test_tilemap.cpp
    test_template_scripts.cpp
    test_voxel.cpp
    test_voxel_collision.cpp
    test_branding.cpp
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
