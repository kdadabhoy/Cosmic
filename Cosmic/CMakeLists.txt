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

# FIXED: Set to PUBLIC so Sandbox can find these headers through the engine link
target_include_directories(imgui PUBLIC
    dependencies/imgui
    dependencies/imgui/backends
)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

# stb_image
add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE dependencies/stb_image)

# ----------------------------
# 3. Engine Source Files (Automatic)
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
# PUBLIC ensures Sandbox can #include engine headers like "core/Application.h"
target_include_directories(Cosmic PUBLIC src)
target_include_directories(Cosmic PRIVATE dependencies/stb_image)

# Linking ImGui as PUBLIC ensures the Sandbox inherits the ImGui include paths
target_link_libraries(Cosmic PUBLIC
    imgui
    glad
    glm
    glfw
    OpenGL::GL
    stb_image
)

# ----------------------------
# 5. Asset Management
# ----------------------------
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets" 
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")