# ----------------------------
# 1. Project and Standards
# ----------------------------
cmake_minimum_required(VERSION 3.21)

project(Cosmic LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)





# ----------------------------
# 2. External Dependencies (Third-Party Libraries)
# ----------------------------

# Find OpenGL (System Library)
find_package(OpenGL REQUIRED)

# GLFW (Source build)
add_subdirectory(dependencies/glfw)

# GLAD (Custom static library)
add_library(glad STATIC dependencies/glad/src/glad.c)
target_include_directories(glad PUBLIC dependencies/glad/include)
target_link_libraries(glad PUBLIC OpenGL::GL)

# GLM (Header-only library)
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE dependencies/glm)

# ImGui (Custom static library for Docking branch)
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
target_link_libraries(imgui PUBLIC
    glfw
    OpenGL::GL
)







# ----------------------------
# 3. Engine Source Files (Library)
# ----------------------------

# CHANGED: add_library instead of add_executable
add_library(Cosmic STATIC)

target_sources(Cosmic PRIVATE
    # REMOVED: src/main.cpp (This belongs in Sandbox)

    # /camera
    src/camera/OrthographicCamera.cpp
    
    # /core
    src/core/Application.cpp
    src/core/Window.cpp
    src/core/LayerStack.cpp
    
    # /graphics
    src/graphics/IndexBuffer.cpp
    src/graphics/Renderer.cpp
    src/graphics/Shader.cpp
    src/graphics/VertexArray.cpp
    src/graphics/VertexBuffer.cpp

    # /layers
    src/layers/ImGuiLayer.cpp
    src/layers/MenuLayer.cpp
)





# ----------------------------
# 4. Engine Configuration and Linking
# ----------------------------

# UPDATED: Changed to PUBLIC so the Sandbox can find your .h files
target_include_directories(Cosmic PUBLIC
    include
)

# UPDATED: Changed to PUBLIC so the Sandbox inherits these dependencies
target_link_libraries(Cosmic PUBLIC
    imgui
    glad
    glm
    glfw
    OpenGL::GL
)







# ----------------------------
# 5. Asset Management (Shaders)
# ----------------------------

# This copies shaders into the build folder so the library can access them
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets" 
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")