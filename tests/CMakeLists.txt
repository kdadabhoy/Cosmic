# CosmicTests — headless unit tests (no window, no GL context).
# Built when COSMIC_BUILD_TESTS=ON (root CMakeLists). Uses the vendored doctest
# single header (Cosmic/dependencies/doctest). Run from build/Runtime/<CONFIG>/
# (the exe is placed next to Cosmic.dll) or via `ctest`.

# Shared tier (plan doc 28 §9.4) — every file here compiles under BOTH engine
# configurations. Several carry in-file COSMIC_2D_ONLY fences around the cases
# that reach for a 3D component; the file itself stays in both suites.
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
    # Physics is dimension agnostic (§4.1): five of the six test_physics_* files
    # stay in the 2D suite. Only test_physics_terrain is 3D-gated below.
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
)

# 3D-only tier (§9.1). These name terrain / voxel / nav / mesh / skeletal / 3D
# renderer types the 2D engine does not ship, so they leave the build outright
# rather than becoming empty translation units.
if(NOT COSMIC_2D_ONLY)
    list(APPEND COSMIC_TEST_SOURCES
        test_phase10_world.cpp
        test_flycamera.cpp
        test_frustum.cpp
        test_presets.cpp
        test_particle_noise.cpp
        test_render_queue.cpp
        test_primitives.cpp
        test_meshimport.cpp
        test_animation.cpp
        test_sockets.cpp
        test_crossfade.cpp
        test_material_slots.cpp
        test_worldsystems.cpp
        test_physics_terrain.cpp
        test_nav_world.cpp
        test_nav_bake.cpp
        test_nav_agents.cpp
        test_voxel.cpp
        test_voxel_collision.cpp
        # ForgeIsle is a 3D flagship the 2D scanner skips outright, and this file
        # builds its terrain/water recipes to check the authored content. §9.1
        # calls the build gate cleaner than an in-file fence — it already
        # self-skips at RUNTIME when Projects/ForgeIsle is absent.
        test_forgeisle_content.cpp
        # 3D partition baselines, captured against the UN-split code (§9.3):
        test_render_desc.cpp
        test_components3d_registry.cpp
    )
endif()

add_executable(CosmicTests ${COSMIC_TEST_SOURCES})

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
