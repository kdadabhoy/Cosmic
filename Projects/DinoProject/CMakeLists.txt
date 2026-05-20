cmake_minimum_required(VERSION 3.21)
project(DinoProject LANGUAGES C CXX)

# ----------------------------
# 1. Identify Source Files
# ----------------------------
file(GLOB_RECURSE DINO_SOURCES "src/*.cpp" "src/*.h")

# Build as a dynamic SHARED library module (.dll)
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

# Links against your engine dynamic target
target_link_libraries(DinoProject PRIVATE Cosmic)

# ----------------------------
# 3. Output Configuration
# ----------------------------
# Drops the compiled game DLL directly alongside CosmicApp.exe and Cosmic.dll
set_target_properties(DinoProject PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Runtime"
)

# ----------------------------
# 4. Project-Specific Asset Management
# ----------------------------
# Copies game-specific assets into a unique project folder next to the DLL.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_custom_command(TARGET DinoProject POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:DinoProject>/assets/projects/DinoProject"
        COMMENT "Syncing DinoProject game assets to sandbox output directory..."
    )
endif()