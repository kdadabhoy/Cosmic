# ----------------------------
# 1. Project and Standards
# ----------------------------
cmake_minimum_required(VERSION 3.21)

project(AirplaneSim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)





# ----------------------------
# 2. External Dependencies (Third-Party Libraries)
# ----------------------------

# Find OpenGL (System Library)
find_package(OpenGL REQUIRED)





# GLFW (Source build) - Assumes CMakeLists.txt is inside the glfw folder
add_subdirectory(dependencies/glfw)





# GLAD (Custom static library)
add_library(glad STATIC dependencies/glad/src/glad.c)
target_include_directories(glad PUBLIC dependencies/glad/include)
target_link_libraries(glad PUBLIC OpenGL::GL)





# GLM (Header-only library)
# We create an 'INTERFACE' library because there are no .cpp files to compile.
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
# 3. Application Source Files
# ----------------------------

# Create the executable target first
add_executable(AirplaneSim)


# BEST PRACTICE: Explicitly list all source files using target_sources.
target_sources(AirplaneSim PRIVATE
    # main 
    src/main.cpp
    src/Application.cpp

    # /graphics
    src/graphics/Window.cpp
    src/graphics/Renderer.cpp
    src/graphics/Shader.cpp

)






# ----------------------------
# 4. Target Configuration and Linking
# ----------------------------

target_include_directories(AirplaneSim PRIVATE
    # This single line is all that's required for everything in your 'include' folder.
    include
)

# Link the final application to the libraries you need.
target_link_libraries(AirplaneSim PRIVATE
    imgui
    glad
    glm
)