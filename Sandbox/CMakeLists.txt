# ----------------------------
# 1. Project Setup
# ----------------------------
project(Sandbox LANGUAGES C CXX)

# ----------------------------
# 2. Identify Game Source Files
# ----------------------------
# Automatically find all .cpp and .h files in your Sandbox src tree
file(GLOB_RECURSE SANDBOX_SOURCES 
    "src/*.cpp" 
    "src/*.h"
)

add_executable(Sandbox ${SANDBOX_SOURCES})

# ----------------------------
# 3. Include and Linking
# ----------------------------
# FIX: Since SandboxLayer.h is now in 'src', point here instead of 'include'
target_include_directories(Sandbox PRIVATE src)

# This links to the 'Cosmic' library. 
# It will now pull in headers from 'Cosmic/src' because the engine 
# exports that directory as PUBLIC.
target_link_libraries(Sandbox PRIVATE Cosmic)

# ----------------------------
# 4. Asset Management & Debugging
# ----------------------------
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/../Cosmic/assets" 
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

set_target_properties(Sandbox PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)