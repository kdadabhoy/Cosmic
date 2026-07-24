cmake_minimum_required(VERSION 3.21)
project(ForgeIsle LANGUAGES C CXX)

# Forge Isle — the Phase 28 flagship showcase (docs/design/forge-isle.md). A Cosmic
# project game module: one SHARED library exporting CreatePluginLayer (standalone
# PlayerLayer boot) + CosmicModule_Register (scripts/components).
#
# DUAL-MODE (the ViperSim pattern): builds in-tree via the root scanner
# (add_subdirectory → links the Cosmic target) AND standalone as an external
# project (Starforge Ctrl+B passes -DCOSMIC_SDK_DIR + -DGAME_OUTPUT_DIR +
# -DGAME_HOT_SUFFIX; links the imported Cosmic.dll).

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /external:W0 /external:templates-)
else()
    add_compile_options(-Wall -Wextra)
endif()
if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX)
endif()

# Hot-reload suffix: each editor rebuild emits ForgeIsle_hotN.dll so a loaded
# previous DLL never blocks the new build (E12 §3.3). Empty for the in-tree build.
set(GAME_HOT_SUFFIX "" CACHE STRING "Unique suffix for hot-reloaded module DLLs")

# Module DLL output root (S1). Starforge's project-local build passes
# -DGAME_OUTPUT_DIR=<projectRoot>/build; in-tree/unset falls back to the SDK
# runtime dir next to Cosmic.dll / CosmicApp.exe.
set(GAME_OUTPUT_DIR "" CACHE PATH "Output root for the module DLL (<dir>/<CONFIG>)")

# ---- SDK path -------------------------------------------------------------------
# In-tree the root CMakeLists caches COSMIC_SDK_DIR = repo root before scanning.
if(NOT COSMIC_SDK_DIR)
    set(COSMIC_SDK_DIR $ENV{COSMIC_SDK})
endif()
if(NOT COSMIC_SDK_DIR)
    message(FATAL_ERROR "Cosmic SDK path not found. Pass -DCOSMIC_SDK_DIR=<path> or set COSMIC_SDK.")
endif()

# ---- Sources ----------------------------------------------------------------------
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.h")

# Standalone builds compile ImGui/ImPlot in (their symbols are not re-exported from
# Cosmic.dll); in-tree the Cosmic target already provides them.
if(NOT TARGET Cosmic)
    list(APPEND GAME_SOURCES
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui.cpp"
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui_draw.cpp"
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui_widgets.cpp"
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui_tables.cpp"
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/implot/implot.cpp"
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/implot/implot_items.cpp"
    )
endif()

add_library(ForgeIsle SHARED ${GAME_SOURCES})
set_target_properties(ForgeIsle PROPERTIES OUTPUT_NAME "ForgeIsle${GAME_HOT_SUFFIX}")

target_include_directories(ForgeIsle PRIVATE src)
target_include_directories(ForgeIsle SYSTEM PRIVATE
    ${COSMIC_SDK_DIR}/Cosmic/src
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/implot
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/glm
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/entt/src
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/spdlog/include
)

# ---- Link the engine (in-tree target OR imported DLL) ------------------------------
if(TARGET Cosmic)
    target_link_libraries(ForgeIsle PRIVATE Cosmic)
else()
    add_library(CosmicCore_Imported SHARED IMPORTED)
    if(WIN32)
        set_target_properties(CosmicCore_Imported PROPERTIES
            IMPORTED_IMPLIB_DEBUG     "${COSMIC_SDK_DIR}/build/Runtime/Debug/Cosmic.lib"
            IMPORTED_LOCATION_DEBUG   "${COSMIC_SDK_DIR}/build/Runtime/Debug/Cosmic.dll"
            IMPORTED_IMPLIB_RELEASE   "${COSMIC_SDK_DIR}/build/Runtime/Release/Cosmic.lib"
            IMPORTED_LOCATION_RELEASE "${COSMIC_SDK_DIR}/build/Runtime/Release/Cosmic.dll"
            MAP_IMPORTED_CONFIG_MINSIZEREL     "Release"
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO "Release")
    else()
        set_target_properties(CosmicCore_Imported PROPERTIES
            IMPORTED_LOCATION "${COSMIC_SDK_DIR}/build/Runtime/Debug/libCosmic${CMAKE_SHARED_LIBRARY_SUFFIX}")
    endif()
    target_link_libraries(ForgeIsle PRIVATE CosmicCore_Imported)
endif()

# ---- Output + asset sync -----------------------------------------------------------
if(NOT GAME_OUTPUT_DIR)
    set(GAME_OUTPUT_DIR "${COSMIC_SDK_DIR}/build/Runtime")
endif()
set_target_properties(ForgeIsle PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${GAME_OUTPUT_DIR}/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${GAME_OUTPUT_DIR}/$<CONFIG>")

# In-tree: mirror the project folder into the runtime VFS layout so the Launcher /
# `CosmicApp --project ForgeIsle` finds project://… beside the exe (the ViperSim
# POST_BUILD pattern; scenes/flows/manifest all live at the project root, so sync
# the whole folder minus src/build).
if(TARGET Cosmic)
    add_custom_command(TARGET ForgeIsle POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/scenes"
            "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>/assets/projects/ForgeIsle/scenes"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/flows"
            "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>/assets/projects/ForgeIsle/flows"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/project.cproj"
            "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>/assets/projects/ForgeIsle/project.cproj"
        COMMENT "Syncing ForgeIsle content to the VFS output directory...")
endif()
