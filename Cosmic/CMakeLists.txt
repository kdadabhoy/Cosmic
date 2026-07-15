cmake_minimum_required(VERSION 3.21)
project(Cosmic LANGUAGES C CXX)

# ----------------------------
# 2. External Dependencies
# ----------------------------
find_package(OpenGL REQUIRED)
add_subdirectory(dependencies/glfw)

add_library(glad STATIC dependencies/glad/src/glad.c)
target_include_directories(glad PUBLIC dependencies/glad/include)
target_link_libraries(glad PUBLIC OpenGL::GL)

add_library(glm INTERFACE)
target_include_directories(glm INTERFACE dependencies/glm)

# ADDED: EnTT Header-Only Dependency Target
add_library(entt INTERFACE)
target_include_directories(entt INTERFACE dependencies/entt/src)

add_library(imgui STATIC
    dependencies/imgui/imgui.cpp
    dependencies/imgui/imgui_draw.cpp
    dependencies/imgui/imgui_widgets.cpp
    dependencies/imgui/imgui_tables.cpp
    dependencies/imgui/backends/imgui_impl_glfw.cpp
    dependencies/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC dependencies/imgui dependencies/imgui/backends)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

add_library(implot STATIC
    dependencies/implot/implot.cpp
    dependencies/implot/implot_items.cpp
    dependencies/implot/implot_demo.cpp
)
target_include_directories(implot PUBLIC dependencies/implot)
target_link_libraries(implot PUBLIC imgui)

# ImGuizmo (MIT) — transform gizmos behind the engine's graphics/Gizmo wrapper
# (S5.5). Only the gizmo TU is vendored; the header stays out of public engine
# headers, so clients use Cosmic::Gizmo, never ImGuizmo directly.
add_library(ImGuizmo STATIC
    dependencies/ImGuizmo/ImGuizmo.cpp
)
target_include_directories(ImGuizmo PUBLIC dependencies/ImGuizmo)
target_link_libraries(ImGuizmo PUBLIC imgui)

add_library(spdlog INTERFACE)
target_include_directories(spdlog INTERFACE dependencies/spdlog/include)

# toml++ (single header, MIT) — TOML parser behind utils/Config. PRIVATE to the
# engine: Config.h exposes no toml types, so clients never need this include.
add_library(tomlplusplus INTERFACE)
target_include_directories(tomlplusplus INTERFACE dependencies/tomlplusplus)

# nlohmann/json (single header, MIT) — JSON scene/prefab/material serialization
# behind scene/SceneSerializer (E2). PRIVATE: included only in engine .cpp files,
# so json types never leak into public headers or client DLL compile times. The
# include root is dependencies/ so the canonical <nlohmann/json.hpp> resolves.
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE dependencies)

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE dependencies/stb_image)

add_library(stb_truetype INTERFACE)
target_include_directories(stb_truetype INTERFACE dependencies/stb_truetype)

add_library(stb_image_write INTERFACE)
target_include_directories(stb_image_write INTERFACE dependencies/stb_image_write)

# Jolt Physics (J1, Phase 15) — vendored static lib, MIT. Built from its own
# curated CMakeLists (dependencies/JoltPhysics/CMakeLists.txt); linked PRIVATE into
# Cosmic below so the JPH:: types never leak past the engine's physics/ TUs.
add_subdirectory(dependencies/JoltPhysics)

# Recast & Detour navigation (N1, Phase 26) — vendored static lib, Zlib. Built
# from its own curated CMakeLists (dependencies/recastnavigation/CMakeLists.txt);
# linked PRIVATE into Cosmic below so the Recast/Detour types never leak past the
# engine's nav/ TUs (the Jolt firewall — only nav/*.cpp include <Recast.h> etc.,
# behind the NavWorld pimpl).
add_subdirectory(dependencies/recastnavigation)

# assimp (A1, Phase 20) — vendored mesh-import backend, BSD-3-Clause, pinned +
# trimmed (see dependencies/assimp/README-COSMIC.md). Default ON since A1; the
# OBJ-only fallback stays one configure flag away. Built through assimp's OWN
# CMakeLists — its cmake_minimum_required is 3.22, so CMP0077 lets these plain
# variables override its option() defaults. Static, linked PRIVATE into
# Cosmic.dll below (the Jolt firewall: only assets/MeshImport.cpp includes
# assimp headers, so aiScene types never reach game/project DLLs).
option(COSMIC_WITH_ASSIMP "FBX/STL/DAE/PLY mesh import via vendored assimp" ON)
if(COSMIC_WITH_ASSIMP)
    set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF)   # importers trimmed to the E16 five
    set(ASSIMP_BUILD_FBX_IMPORTER ON)
    set(ASSIMP_BUILD_OBJ_IMPORTER ON)
    set(ASSIMP_BUILD_STL_IMPORTER ON)
    set(ASSIMP_BUILD_COLLADA_IMPORTER ON)
    set(ASSIMP_BUILD_PLY_IMPORTER ON)
    set(ASSIMP_NO_EXPORT ON)                          # exporters compiled out
    set(ASSIMP_BUILD_TESTS OFF)
    set(ASSIMP_BUILD_ASSIMP_TOOLS OFF)
    set(ASSIMP_BUILD_SAMPLES OFF)
    set(ASSIMP_BUILD_DOCS OFF)
    set(ASSIMP_INSTALL OFF)
    set(ASSIMP_BUILD_ZLIB ON)                         # vendored zlib, no system dep
    set(ASSIMP_WARNINGS_AS_ERRORS OFF)
    set(ASSIMP_IGNORE_GIT_HASH ON)                    # vendored tree has no .git
    set(ASSIMP_BUILD_USE_CCACHE OFF)                  # would set a GLOBAL launcher hook
    set(ASSIMP_INJECT_DEBUG_POSTFIX OFF)
    set(BUILD_SHARED_LIBS OFF)
    # assimp 5.4.3's mkdir custom commands predate CMP0175; OLD keeps the
    # legacy POST_BUILD assumption without a per-configure warning wall.
    set(CMAKE_POLICY_DEFAULT_CMP0175 OLD)
    add_subdirectory(dependencies/assimp)
    unset(CMAKE_POLICY_DEFAULT_CMP0175)
    # assimp's dir scope pins C++17; hoist its target to the engine's /std:c++20
    # so MSVC sees ONE standard flag (the Jolt precedent). /W0 silences the
    # vendored targets only (the engine's zero-warning bar is for our code) —
    # spelled /W0, not /w, so the VS generator emits a WarningLevel and MSBuild
    # doesn't inject its /W1 default next to it (D9025).
    set_target_properties(assimp PROPERTIES CXX_STANDARD 20)
    if(MSVC)
        target_compile_options(assimp PRIVATE /W0)
        target_compile_options(zlibstatic PRIVATE /W0)
    endif()
endif()

# ----------------------------
# 3. Engine Source Files
# ----------------------------
file(GLOB_RECURSE COSMIC_SOURCES
    "src/*.cpp"
    "src/*.h"
    "dependencies/stb_image/*.cpp"
    "dependencies/stb_truetype/*.cpp"
    "dependencies/stb_image_write/*.cpp"
)
add_library(Cosmic SHARED ${COSMIC_SOURCES})

# ----------------------------
# 4. Engine Configuration and Linking
# ----------------------------
target_include_directories(Cosmic PUBLIC src)
target_include_directories(Cosmic PRIVATE dependencies/stb_image dependencies/stb_truetype dependencies/stb_image_write dependencies/miniaudio dependencies/cgltf)
target_compile_definitions(Cosmic PRIVATE COSMIC_BUILD_DLL)

# TypeRegistry.cpp instantiates a reflection thunk set per registered component;
# with the UI + 2D component additions (Phase 17) its object file exceeds MSVC's
# COFF section limit. /bigobj lifts it (no runtime cost, standard for heavy TUs).
if(MSVC)
    target_compile_options(Cosmic PRIVATE /bigobj)
endif()

# Distribution build is implied by the Release config — disables the Launcher's
# "New Project" UI. Evaluated per-config, so Debug/RelWithDebInfo stay non-DIST
# automatically with no sticky cache flag.
target_compile_definitions(Cosmic PRIVATE $<$<CONFIG:Release>:COSMIC_DIST>)

# UPDATED: Linked 'entt' PUBLICLY so all dependent client targets inherit the headers
target_link_libraries(Cosmic PUBLIC imgui implot glad glm entt glfw OpenGL::GL stb_image stb_truetype stb_image_write spdlog)
# PRIVATE deps: no public engine header includes them, so clients never see the
# include dirs — ImGuizmo stays behind graphics/Gizmo (its symbols aren't
# dllexported, so leaking the headers would only invite link errors). Jolt is the
# same story: only physics/*.cpp include <Jolt/...>, behind the PhysicsWorld pimpl.
# RecastNavigation likewise: only nav/*.cpp include <Recast.h>/<Detour...>, behind
# the NavWorld pimpl.
target_link_libraries(Cosmic PRIVATE tomlplusplus ImGuizmo nlohmann_json Jolt RecastNavigation)

# assimp backend (A1): PRIVATE link + define — MeshImport.cpp's #ifdef block is
# the only consumer; MeshImport::AssimpEnabled() reports the gate at runtime.
if(COSMIC_WITH_ASSIMP)
    target_link_libraries(Cosmic PRIVATE assimp)
    target_compile_definitions(Cosmic PRIVATE COSMIC_WITH_ASSIMP)
endif()

# Borderless custom window chrome uses DWM (drop shadow) on Windows.
if(WIN32)
    target_link_libraries(Cosmic PRIVATE dwmapi)
endif()

# FIXED: Map binaries safely across VS (multi-config) and Ninja/Make (single-config)
set_target_properties(Cosmic PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>"
    ARCHIVE_OUTPUT_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>"
)

target_precompile_headers(Cosmic PRIVATE src/CosmicPCH.h)

# FIXED: Uses generator expressions to safely resolve paths during asset sync
add_custom_command(TARGET Cosmic POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>/assets"
    COMMENT "Syncing Cosmic Engine core assets to output directory..."
)

# ----------------------------
# 5. Packaging (cmake --install)
# ----------------------------
# Engine DLL lands flat next to CosmicApp.exe; core assets go under assets/.
install(TARGETS Cosmic
    RUNTIME DESTINATION .
    LIBRARY DESTINATION .)
install(DIRECTORY assets/ DESTINATION assets)