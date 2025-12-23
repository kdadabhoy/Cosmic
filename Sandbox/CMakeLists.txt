# ----------------------------
# 1. Project Setup
# ----------------------------
project(Sandbox LANGUAGES C CXX)

# ----------------------------
# 2. Identify Game Source Files
# ----------------------------
# We only list .cpp files here. 
# .h files are found via target_include_directories.
add_executable(Sandbox 
    src/SandboxApp.cpp 
    src/SandboxLayer.cpp
)

# ----------------------------
# 3. Include and Linking
# ----------------------------
# This allows you to #include "SandboxLayer.h" from SandboxApp.cpp 
# without needing complex relative paths.
target_include_directories(Sandbox PRIVATE include)

# This links to the 'Cosmic' library and automatically 
# pulls in Cosmic's 'include' folder, GLFW, Glad, and ImGui.
target_link_libraries(Sandbox PRIVATE Cosmic)

# ----------------------------
# 4. Asset Management & Debugging
# ----------------------------
# Since this file is inside /Sandbox, we look up one level to find /Cosmic/assets
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/../Cosmic/assets" 
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

# Sets the "Working Directory" so the Sandbox.exe finds shaders immediately
set_target_properties(Sandbox PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)