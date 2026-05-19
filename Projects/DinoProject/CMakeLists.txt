cmake_minimum_required(VERSION 3.21)
project(DinoProject LANGUAGES C CXX)

# ----------------------------
# 1. Identify Source Files
# ----------------------------
file(GLOB_RECURSE DINO_SOURCES "src/*.cpp" "src/*.h")

# CRITICAL CHANGE: Build as a dynamic SHARED library module (.dll)
add_library(DinoProject SHARED ${DINO_SOURCES})

# ----------------------------
# 2. Include Paths & Linking
# ----------------------------
target_include_directories(DinoProject PRIVATE 
    src
    ../../Cosmic/src
    ../../Cosmic/dependencies/imgui
    ../../Cosmic/dependencies/implot
    ../../Cosmic/dependencies/glm
    ../../Cosmic/dependencies/spdlog/include
)

# Link against Cosmic.exe directly to resolve shared types and systems
target_link_libraries(DinoProject PRIVATE Cosmic)

# ----------------------------
# 3. Output Configuration
# ----------------------------
# Drop the output DLL directly into Cosmic's execution workspace directory
set_target_properties(DinoProject PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Cosmic"
)