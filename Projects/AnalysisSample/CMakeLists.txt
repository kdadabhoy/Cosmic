cmake_minimum_required(VERSION 3.21)
project(AnalysisSample LANGUAGES C CXX)

# AnalysisSample — the WO-10 / X01 analysis reference project (2D stability
# catalog). An EXTERNAL Cosmic consumer: this folder is configured and built on
# its own against a Cosmic SDK checkout (-DCOSMIC_SDK_DIR=<sdk> or the COSMIC_SDK
# environment variable), never as part of the SDK's root build — the root
# CMakeLists lists it in COSMIC_SKIP_PROJECTS on purpose. The consumption path is
# the one the Starforge packager drives for an external project:
#
#   cmake -S <copy of this folder> -B <copy>/build -A x64 -DCOSMIC_SDK_DIR=<sdk> -DGAME_OUTPUT_DIR=<copy>/build
#   cmake --build <copy>/build --config Release --parallel
#
# which yields <copy>/build/Release/AnalysisSample.dll, a plain Cosmic runtime
# plugin (CreatePluginLayer + InitializePluginContexts) the packaged
# AnalysisSample.exe (CosmicApp.exe renamed) boots through boot.cfg.
#
# Compatibility specimen only (D-9km): synthetic F-TRAJECTORY / F-SERIES-LARGE
# fixtures, no real to-9km schema.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /external:W0 /external:templates-)
endif()
if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX)
endif()

# Engine build mode — MUST match the Cosmic SDK this plugin links against (the 2D
# stability trunk ships Cosmic.dll compiled with COSMIC_2D_ONLY; the PUBLIC headers
# carry #ifndef COSMIC_2D_ONLY fences, so every plugin TU needs the same definition
# or the plugin-boundary ABI silently diverges). Same option the scaffold template
# and the hot-reload configure use.
option(COSMIC_2D_ONLY "Match the 2D-only Cosmic SDK this plugin links against" ON)

# Output root for the plugin DLL (<dir>/<CONFIG>). The Starforge packager passes
# -DGAME_OUTPUT_DIR=<project>/build for an external project; left unset, the DLL
# stays in this build tree (never in the SDK's runtime dir — this is a consumer).
set(GAME_OUTPUT_DIR "" CACHE PATH "Output root for the plugin DLL (<dir>/<CONFIG>)")

# ---- SDK path -----------------------------------------------------------------
if(NOT COSMIC_SDK_DIR)
    set(COSMIC_SDK_DIR $ENV{COSMIC_SDK})
endif()
if(NOT COSMIC_SDK_DIR)
    message(FATAL_ERROR "Cosmic SDK path not found. Pass -DCOSMIC_SDK_DIR=<path> or set COSMIC_SDK.")
endif()
if(TARGET Cosmic)
    message(FATAL_ERROR "AnalysisSample is an external consumer specimen (WO-10 X01): configure it standalone against COSMIC_SDK_DIR, not inside the SDK build.")
endif()

# ---- Sources ------------------------------------------------------------------
file(GLOB_RECURSE SAMPLE_SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.h")

# ImGui/ImPlot symbols are not re-exported from Cosmic.dll: a plugin compiles them
# in and adopts the HOST's contexts in InitializePluginContexts (one shared UI
# state across the boundary) — the same pattern every Cosmic project uses.
list(APPEND SAMPLE_SOURCES
    "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui.cpp"
    "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui_draw.cpp"
    "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui_widgets.cpp"
    "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui_tables.cpp"
    "${COSMIC_SDK_DIR}/Cosmic/dependencies/implot/implot.cpp"
    "${COSMIC_SDK_DIR}/Cosmic/dependencies/implot/implot_items.cpp"
)

add_library(AnalysisSample SHARED ${SAMPLE_SOURCES})

if(COSMIC_2D_ONLY)
    target_compile_definitions(AnalysisSample PRIVATE COSMIC_2D_ONLY)
endif()

target_include_directories(AnalysisSample PRIVATE src)
target_include_directories(AnalysisSample SYSTEM PRIVATE
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
target_link_libraries(AnalysisSample PRIVATE CosmicCore_Imported)

if(NOT GAME_OUTPUT_DIR)
    set(GAME_OUTPUT_DIR "${CMAKE_BINARY_DIR}")
endif()
set_target_properties(AnalysisSample PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${GAME_OUTPUT_DIR}/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${GAME_OUTPUT_DIR}/$<CONFIG>")
