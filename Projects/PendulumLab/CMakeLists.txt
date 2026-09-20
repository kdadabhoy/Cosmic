cmake_minimum_required(VERSION 3.21)
project(PendulumLab LANGUAGES C CXX)

# PendulumLab — the App Platform showcase project (AP-04; catalog F02, Y01–Y03).
# An EXTERNAL Cosmic consumer, like AnalysisSample: this folder is configured and
# built on its own against a Cosmic SDK checkout (-DCOSMIC_SDK_DIR=<sdk> or the
# COSMIC_SDK environment variable), never as part of the SDK's root build — the
# root CMakeLists lists it in COSMIC_SKIP_PROJECTS on purpose and the guard below
# refuses an in-tree add_subdirectory. The consumption path is the one the
# Starforge packager drives for an external project (Y01 / the K01 procedure):
#
#   cmake -S <this folder> -B <fresh dir> -A x64 -DCOSMIC_SDK_DIR=<sdk> -DGAME_OUTPUT_DIR=<fresh dir>
#   cmake --build <fresh dir> --config Release --parallel
#
# which yields <fresh dir>/Release/PendulumLab.dll — a Cosmic GAME MODULE
# (CS_MODULE_BEGIN/END: CosmicModule_Register + CreatePluginLayer + the ImGui
# context handshake) that the packaged PendulumLab.exe (CosmicApp.exe renamed)
# boots through boot.cfg and that Starforge hot-loads in the editor.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /external:W0 /external:templates-)
endif()
if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX)
endif()

# Hot-reload suffix: each editor rebuild emits <name>_hotN.dll so a locked/loaded
# previous DLL never blocks the new build (E12 §3.3).
set(GAME_HOT_SUFFIX "" CACHE STRING "Unique suffix for hot-reloaded module DLLs")

# Engine build mode — kept in step with the Cosmic SDK this module links against.
# Since AP-05 the SDK's public headers carry no COSMIC_2D_ONLY fences, so the
# definition is a compatibility no-op; it stays because Cosmic.dll still carries
# the same PUBLIC define and a module may test it (same option as the templates).
option(COSMIC_2D_ONLY "Match the 2D-only Cosmic SDK this module links against" ON)

# Output root for the module DLL (<dir>/<CONFIG>). The Starforge packager and the
# editor's BuildRunner pass -DGAME_OUTPUT_DIR=<project>/build; left unset, the DLL
# stays in this build tree (never in the SDK's runtime dir — this is a consumer).
set(GAME_OUTPUT_DIR "" CACHE PATH "Output root for the module DLL (<dir>/<CONFIG>)")

# ---- SDK path -----------------------------------------------------------------
if(NOT COSMIC_SDK_DIR)
    set(COSMIC_SDK_DIR $ENV{COSMIC_SDK})
endif()
if(NOT COSMIC_SDK_DIR)
    message(FATAL_ERROR "Cosmic SDK path not found. Pass -DCOSMIC_SDK_DIR=<path> or set COSMIC_SDK.")
endif()
if(TARGET Cosmic)
    message(FATAL_ERROR "PendulumLab is an external consumer specimen (AP-04 / Y01): configure it standalone against COSMIC_SDK_DIR, not inside the SDK build.")
endif()

# ---- Sources ------------------------------------------------------------------
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.h")

# ImGui/ImPlot symbols are not re-exported from Cosmic.dll: a module compiles them
# in and adopts the HOST's contexts in InitializePluginContexts (one shared UI
# state across the boundary) — the PhasePlot hosted panel draws through them.
list(APPEND GAME_SOURCES
    "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui.cpp"
    "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui_draw.cpp"
    "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui_widgets.cpp"
    "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui_tables.cpp"
    "${COSMIC_SDK_DIR}/Cosmic/dependencies/implot/implot.cpp"
    "${COSMIC_SDK_DIR}/Cosmic/dependencies/implot/implot_items.cpp"
)

add_library(PendulumLab SHARED ${GAME_SOURCES})
set_target_properties(PendulumLab PROPERTIES OUTPUT_NAME "PendulumLab${GAME_HOT_SUFFIX}")

if(COSMIC_2D_ONLY)
    target_compile_definitions(PendulumLab PRIVATE COSMIC_2D_ONLY)
endif()

target_include_directories(PendulumLab PRIVATE src)
target_include_directories(PendulumLab SYSTEM PRIVATE
    ${COSMIC_SDK_DIR}/Cosmic/src
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/implot
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/glm
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/entt/src
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/spdlog/include
)

# ---- Link the engine DLL (the SDK's build outputs) ------------------------------
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
target_link_libraries(PendulumLab PRIVATE CosmicCore_Imported)

if(NOT GAME_OUTPUT_DIR)
    set(GAME_OUTPUT_DIR "${CMAKE_BINARY_DIR}")
endif()
set_target_properties(PendulumLab PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${GAME_OUTPUT_DIR}/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${GAME_OUTPUT_DIR}/$<CONFIG>")
