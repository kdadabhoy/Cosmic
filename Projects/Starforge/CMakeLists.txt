cmake_minimum_required(VERSION 3.21)
project(Starforge LANGUAGES C CXX)

# Starforge — the Cosmic editor (docs/plans/11-phase13-starforge-plan.md).
# Target name MUST equal the folder name (root-CMake project scanner + packaging rule).

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_options(/external:W0)
    add_compile_options(/external:templates-)
    # StarforgeApp.cpp is a very large translation unit; it crossed the COFF
    # /2^16-section limit (C1128) as Phase 27 grew the Entity menu + viewport.
    add_compile_options(/bigobj)
else()
    add_compile_options(-Wall -Wextra)
endif()

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX)
endif()

# -------------------------------------------------------------------
# 1. SDK Path Resolution
# -------------------------------------------------------------------
if(NOT COSMIC_SDK_DIR)
    set(COSMIC_SDK_DIR $ENV{COSMIC_SDK})
endif()

if(NOT COSMIC_SDK_DIR)
    message(FATAL_ERROR "Cosmic Engine SDK path not found! Set COSMIC_SDK environment variable or pass -DCOSMIC_SDK_DIR=<path>.")
endif()

# -------------------------------------------------------------------
# 2. Source Files (CONFIGURE_DEPENDS re-globs at build time — new panel/editor
#    files from the Phase 13 work orders are picked up automatically)
# -------------------------------------------------------------------
file(GLOB_RECURSE STARFORGE_SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.h")

# Phase 29 / W7 — the 2D editor's TU exclusions (plan doc 28 §8.5). Three files
# are 3D end-to-end: the Voxels panel (VoxelVolume brush + palette), the World
# Systems panel (terrain/water/particle recipes) and the Animation Editor
# (Skeleton + AnimationClip + PreviewRig::RenderSkeletal). Every OTHER Starforge
# file stays in both configurations and carries in-file #ifndef COSMIC_2D_ONLY
# fences instead. The option is redeclared here (a no-op once the root
# CMakeLists' cache entry exists) so this directory still configures standalone.
option(COSMIC_2D_ONLY "Build the 2D-only engine (excludes all 3D subsystems)" OFF)
if(COSMIC_2D_ONLY)
    list(FILTER STARFORGE_SOURCES EXCLUDE REGEX "src/panels/VoxelPanel\\.(cpp|h)$")
    list(FILTER STARFORGE_SOURCES EXCLUDE REGEX "src/panels/WorldSystemsPanel\\.(cpp|h)$")
    list(FILTER STARFORGE_SOURCES EXCLUDE REGEX "src/editors/AnimationEditor\\.(cpp|h)$")
endif()

# imgui-node-editor (U6 Flow Graph panel) — vendored library sources compiled
# into the editor DLL, warnings off (external code; see the dependency's
# VENDOR-NOTES.md). In-tree the ImGui symbols come from the propagated static
# imgui target; standalone they come from the bundled sources below.
set(STARFORGE_NODE_EDITOR_SOURCES
    "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui-node-editor/imgui_node_editor.cpp"
    "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui-node-editor/imgui_node_editor_api.cpp"
    "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui-node-editor/imgui_canvas.cpp"
    "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui-node-editor/crude_json.cpp"
)
if(MSVC)
    # /W0 (not /w): both silence the vendored sources, but /w "overrides" cl's
    # implicit /W1 and emits command-line warning D9025 on a full rebuild — /W0
    # sets the level cleanly with no notice.
    set_source_files_properties(${STARFORGE_NODE_EDITOR_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0")
else()
    set_source_files_properties(${STARFORGE_NODE_EDITOR_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
endif()
list(APPEND STARFORGE_SOURCES ${STARFORGE_NODE_EDITOR_SOURCES})

# When building standalone (outside the engine root), bundle vendor ImGui/ImPlot sources
if(NOT TARGET Cosmic)
    list(APPEND STARFORGE_SOURCES
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui.cpp"
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui_draw.cpp"
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui_widgets.cpp"
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui_tables.cpp"
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/implot/implot.cpp"
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/implot/implot_items.cpp"
    )
endif()

add_library(Starforge SHARED ${STARFORGE_SOURCES})

# -------------------------------------------------------------------
# 3. Include Paths
# -------------------------------------------------------------------
target_include_directories(Starforge PRIVATE src)

target_include_directories(Starforge SYSTEM PRIVATE
    ${COSMIC_SDK_DIR}/Cosmic/src
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/implot
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui-node-editor
    ${COSMIC_SDK_DIR}/Cosmic/dependencies
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/glm
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/entt/src
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/spdlog/include
)

# -------------------------------------------------------------------
# 4. Linking — agnostic (works in-tree or standalone)
# -------------------------------------------------------------------
if(TARGET Cosmic)
    target_link_libraries(Starforge PRIVATE Cosmic)
else()
    add_library(CosmicCore_Imported SHARED IMPORTED)

    if(WIN32)
        set_target_properties(CosmicCore_Imported PROPERTIES
            IMPORTED_IMPLIB_DEBUG     "${COSMIC_SDK_DIR}/build/Runtime/Debug/Cosmic.lib"
            IMPORTED_LOCATION_DEBUG   "${COSMIC_SDK_DIR}/build/Runtime/Debug/Cosmic.dll"
            IMPORTED_IMPLIB_RELEASE   "${COSMIC_SDK_DIR}/build/Runtime/Release/Cosmic.lib"
            IMPORTED_LOCATION_RELEASE "${COSMIC_SDK_DIR}/build/Runtime/Release/Cosmic.dll"
            MAP_IMPORTED_CONFIG_MINSIZEREL    "Release"
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO "Release"
        )
    else()
        set_target_properties(CosmicCore_Imported PROPERTIES
            IMPORTED_LOCATION "${COSMIC_SDK_DIR}/build/Runtime/Debug/libCosmic${CMAKE_SHARED_LIBRARY_SUFFIX}"
        )
    endif()

    target_link_libraries(Starforge PRIVATE CosmicCore_Imported)
endif()

# -------------------------------------------------------------------
# 5. Output directories — land next to CosmicApp.exe
# -------------------------------------------------------------------
set_target_properties(Starforge PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>"
)

# -------------------------------------------------------------------
# 6. Asset sync (project:// VFS). Inert while assets/ is absent. E6/E12 add
#    editor templates + sample assets here; they sync to
#    assets/projects/Starforge/ next to the runtime.
# -------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_custom_command(TARGET Starforge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>/assets/projects/Starforge"
        COMMENT "Syncing Starforge assets to VFS output directory..."
    )
endif()

# -------------------------------------------------------------------
# 7. Drop-a-file branding (Phase 22 / K1): the default molten-orange mark ships
#    NEXT TO THE EXE as branding/icon.png — the first hit of the runtime icon
#    resolution order (utils/Branding), so the mechanism is visibly live out of
#    the box. Replace the copied file (or the source here) to re-brand; a
#    running editor hot-swaps it without a restart.
# -------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/branding/icon.png")
    add_custom_command(TARGET Starforge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/branding/icon.png"
            "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>/branding/icon.png"
        COMMENT "Staging the Starforge branding icon next to the runtime..."
    )
endif()
