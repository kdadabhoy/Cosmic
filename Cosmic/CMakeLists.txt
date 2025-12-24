# ----------------------------
# 1. Project and Standards
# ----------------------------
cmake_minimum_required(VERSION 3.21)
project(Cosmic LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ----------------------------
# 2. External Dependencies
# ----------------------------
find_package(OpenGL REQUIRED)

# GLFW
add_subdirectory(dependencies/glfw)

# GLAD
add_library(glad STATIC dependencies/glad/src/glad.c)
target_include_directories(glad PUBLIC dependencies/glad/include)
target_link_libraries(glad PUBLIC OpenGL::GL)

# GLM
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE dependencies/glm)

# ImGui
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

# ----------------------------
# 3. Engine Source Files (Automatic)
# ----------------------------
# This automatically finds every .cpp and .h file in your src tree
file(GLOB_RECURSE COSMIC_SOURCES 
    "src/*.cpp" 
    "src/*.h"
)

add_library(Cosmic STATIC ${COSMIC_SOURCES})

# ----------------------------
# 4. Engine Configuration and Linking
# ----------------------------
# Since all headers are now in src, we point here. 
# PUBLIC ensures SandboxApp can #include "core/Application.h"
target_include_directories(Cosmic PUBLIC src)

target_link_libraries(Cosmic PUBLIC
    imgui
    glad
    glm
    glfw
    OpenGL::GL
)

# ----------------------------
# 5. Asset Management
# ----------------------------
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets" 
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")