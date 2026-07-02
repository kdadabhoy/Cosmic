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

add_library(spdlog INTERFACE)
target_include_directories(spdlog INTERFACE dependencies/spdlog/include)

# toml++ (single header, MIT) — TOML parser behind utils/Config. PRIVATE to the
# engine: Config.h exposes no toml types, so clients never need this include.
add_library(tomlplusplus INTERFACE)
target_include_directories(tomlplusplus INTERFACE dependencies/tomlplusplus)

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE dependencies/stb_image)

add_library(stb_truetype INTERFACE)
target_include_directories(stb_truetype INTERFACE dependencies/stb_truetype)

add_library(stb_image_write INTERFACE)
target_include_directories(stb_image_write INTERFACE dependencies/stb_image_write)

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
target_include_directories(Cosmic PRIVATE dependencies/stb_image dependencies/stb_truetype dependencies/stb_image_write)
target_compile_definitions(Cosmic PRIVATE COSMIC_BUILD_DLL)

# Distribution build is implied by the Release config — disables the Launcher's
# "New Project" UI. Evaluated per-config, so Debug/RelWithDebInfo stay non-DIST
# automatically with no sticky cache flag.
target_compile_definitions(Cosmic PRIVATE $<$<CONFIG:Release>:COSMIC_DIST>)

# UPDATED: Linked 'entt' PUBLICLY so all dependent client targets inherit the headers
target_link_libraries(Cosmic PUBLIC imgui implot glad glm entt glfw OpenGL::GL stb_image stb_truetype stb_image_write spdlog)
target_link_libraries(Cosmic PRIVATE tomlplusplus)

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