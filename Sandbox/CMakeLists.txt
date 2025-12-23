# ----------------------------
# 1. Project Setup
# ----------------------------
# This project represents your actual game/simulation
project(Sandbox LANGUAGES C CXX)

# ----------------------------
# 2. Identify Game Source Files
# ----------------------------
# We only list the files inside Sandbox/src
add_executable(Sandbox 
    src/SandboxApp.cpp 
    "src/SandboxLayer.cpp"
)

# ----------------------------
# 3. Link to the Engine
# ----------------------------
# This links to the 'Cosmic' library target defined in the other folder.
# It automatically pulls in all engine headers and third-party dependencies.
target_link_libraries(Sandbox PRIVATE Cosmic)

# ----------------------------
# 4. Asset Management & Debugging
# ----------------------------
# We copy the assets to the Sandbox build folder so the .exe can find them.
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/../Cosmic/assets" 
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

# Sets the "Working Directory" in Visual Studio to the build folder
# so it finds the shaders/assets immediately when you press F5. [cite: 7]
set_target_properties(Sandbox PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)