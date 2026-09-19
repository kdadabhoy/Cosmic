cmake_minimum_required(VERSION 3.21)
project(Cosmic LANGUAGES C CXX)

# ----------------------------
# 1. Engine configuration flags
# ----------------------------
# COSMIC_2D_ONLY (Phase 29) — the pure-2D engine configuration. It drops every 3D
# subsystem: the terrain/voxel/water/nav/particles source trees, the 3D renderer
# passes, model + skeletal-animation loading, and the assimp and recastnavigation
# vendored dependencies (which are not even configured). See §4 of
# docs/plans/28-phase29-engine-split-plan.md for the authoritative exclusion table.
#
# Both options are also declared in the root CMakeLists.txt (which forwards them to
# the project scanner); option() is a no-op when the cache entry already exists, so
# declaring them here as well keeps this directory configurable standalone.
# Defaults ON on the 2D stability trunk (WO-03): the root CMakeLists' cache entry
# governs an in-tree build, but this default makes a standalone `cmake -S Cosmic`
# configure select the 2D engine too. The hard OFF-rejection lives in the root
# CMakeLists (the enforcement point); engine-3d keeps this OFF.
option(COSMIC_2D_ONLY "Build the 2D-only engine (excludes all 3D subsystems)" ON)

# Jolt ships on BOTH configurations (Phase 29 decision 3) — physics is dimension
# agnostic, so 2D keeps rigid bodies, box/sphere/capsule colliders and the
# character controller. OFF is a supported configuration since W3: it drops
# physics/backends/JoltBackend.cpp and leaves the null backend plus whatever an app
# registers through IPhysicsBackend (physics/PhysicsBackend.h).
option(COSMIC_WITH_JOLT "Build the Jolt physics backend" ON)

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
# Configured on both engine configurations (Phase 29 decision 3).
if(COSMIC_WITH_JOLT)
    add_subdirectory(dependencies/JoltPhysics)
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

# physics/backends/ (W3) — the Jolt backend is the only TU that includes
# <Jolt/...>, so COSMIC_WITH_JOLT=OFF drops it and leaves the null backend plus
# whatever an app registers through IPhysicsBackend. Orthogonal to COSMIC_2D_ONLY:
# both engine configurations ship Jolt by default (decision 3).
if(NOT COSMIC_WITH_JOLT)
    list(FILTER COSMIC_SOURCES EXCLUDE REGEX "/src/physics/backends/JoltBackend\\.cpp$")
endif()

add_library(Cosmic SHARED ${COSMIC_SOURCES})

# ----------------------------
# 4. Engine Configuration and Linking
# ----------------------------
target_include_directories(Cosmic PUBLIC src)
target_include_directories(Cosmic PRIVATE dependencies/stb_image dependencies/stb_truetype dependencies/stb_image_write dependencies/miniaudio)
target_compile_definitions(Cosmic PRIVATE COSMIC_BUILD_DLL)

# COSMIC_2D_ONLY is PUBLIC, unlike every other engine define: Starforge, the game
# projects and CosmicTests all compile against public headers that carry the
# partition fences (Cosmic.h, Components.h, SceneRenderer.h), so the flag has to
# resolve identically in every consumer or the ABI silently diverges. Empty (not
# defined at all) in the 3D configuration.
target_compile_definitions(Cosmic PUBLIC $<$<BOOL:${COSMIC_2D_ONLY}>:COSMIC_2D_ONLY>)

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
# Jolt is generator-expression gated so the link list follows the same condition
# as the add_subdirectory call above — an unconfigured target name would
# otherwise reach the linker as a bare library.
target_link_libraries(Cosmic PRIVATE tomlplusplus ImGuizmo nlohmann_json
    $<$<BOOL:${COSMIC_WITH_JOLT}>:Jolt>)

# Compile-time counterpart of the option: PhysicsBackend.cpp calls
# RegisterJoltPhysicsBackend() and defaults the registry to "jolt" only when this
# is defined; without it the default is "null".
if(COSMIC_WITH_JOLT)
    target_compile_definitions(Cosmic PRIVATE COSMIC_WITH_JOLT)
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