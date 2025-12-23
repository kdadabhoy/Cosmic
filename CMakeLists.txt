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
add_executable(Cosmic)


# BEST PRACTICE: Explicitly list all source files using target_sources.
target_sources(Cosmic PRIVATE
    # main 
    src/main.cpp


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
# 4. Target Configuration and Linking
# ----------------------------

target_include_directories(Cosmic PRIVATE
    # This single line is all that's required for everything in your 'include' folder.
    include
)

# Link the final application to the libraries you need.
target_link_libraries(Cosmic PRIVATE
    imgui
    glad
    glm
)




# ----------------------------
# 5. Asset Management (Shaders)
# ----------------------------

# This creates a folder named 'assets' inside your build directory 
# and copies the shaders into 'assets/shaders'.
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets" 
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

# This tells Visual Studio to set the "Working Directory" to the build folder
# so that it finds the assets immediately when you press F5.
set_target_properties(Cosmic PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)