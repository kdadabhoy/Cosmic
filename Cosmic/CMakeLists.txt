# ----------------------------
# 1. Project Setup
# ----------------------------
cmake_minimum_required(VERSION 3.21)
project(Cosmic LANGUAGES C CXX)

# ----------------------------
# 2. External Dependencies
# ----------------------------
find_package(OpenGL REQUIRED)

# GLFW - Windowing and Input
add_subdirectory(dependencies/glfw)

# GLAD - OpenGL Loader
add_library(glad STATIC dependencies/glad/src/glad.c)
target_include_directories(glad PUBLIC dependencies/glad/include)
target_link_libraries(glad PUBLIC OpenGL::GL)

# GLM - Mathematics
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE dependencies/glm)

# ImGui - Debug UI
add_library(imgui STATIC
    dependencies/imgui/imgui.cpp
    dependencies/imgui/imgui_demo.cpp
    dependencies/imgui/imgui_draw.cpp
    dependencies/imgui/imgui_widgets.cpp
    dependencies/imgui/imgui_tables.cpp
    dependencies/imgui/backends/imgui_impl_glfw.cpp
    dependencies/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
    dependencies/imgui
    dependencies/imgui/backends
)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

# ImPlot - Advanced Plotting for ImGui
add_library(implot STATIC
    dependencies/implot/implot.cpp
    dependencies/implot/implot_items.cpp
    dependencies/implot/implot_demo.cpp
)
target_include_directories(implot PUBLIC dependencies/implot)
target_link_libraries(implot PUBLIC imgui)

# spdlog - Logging Framework
add_library(spdlog INTERFACE)
target_include_directories(spdlog INTERFACE dependencies/spdlog/include)

# stb_image - Image Loading
add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE dependencies/stb_image)

# ----------------------------
# 3. Engine Source Files
# ----------------------------
file(GLOB_RECURSE COSMIC_SOURCES 
    "src/*.cpp" 
    "src/*.h"
    "dependencies/stb_image/*.cpp"
)

add_library(Cosmic STATIC ${COSMIC_SOURCES})

# ----------------------------
# 4. Engine Configuration and Linking
# ----------------------------
# PUBLIC: Anything linking to Cosmic also gets 'src' in its include path
target_include_directories(Cosmic PUBLIC src)
# PRIVATE: Only the engine needs the internal stb headers
target_include_directories(Cosmic PRIVATE dependencies/stb_image)

target_link_libraries(Cosmic PUBLIC
    imgui
    implot
    glad
    glm
    glfw
    OpenGL::GL
    stb_image
    spdlog
)

# ----------------------------
# 5. Asset Management
# ----------------------------
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets" 
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")