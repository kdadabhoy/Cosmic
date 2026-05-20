cmake_minimum_required(VERSION 3.21)
project(DinoProject LANGUAGES C CXX)

# -------------------------------------------------------------------
# 1. Project Standards (CRITICAL for Standalone Compilation)
# -------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC specific options to prevent spdlog compilation failures
if(MSVC)
    add_compile_options(/utf-8)
endif()

# Windows Anti-Conflict flags
if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX) 
endif()

# -------------------------------------------------------------------
# 2. Identify Source Files
# -------------------------------------------------------------------
file(GLOB_RECURSE DINO_SOURCES "src/*.cpp" "src/*.h")

# If compiling standalone via build.bat, compile ImGui/ImPlot source 
# units right alongside your project so they resolve natively.
if(NOT TARGET Cosmic)
    list(APPEND DINO_SOURCES
        "../../Cosmic/dependencies/imgui/imgui.cpp"
        "../../Cosmic/dependencies/imgui/imgui_draw.cpp"
        "../../Cosmic/dependencies/imgui/imgui_widgets.cpp"
        "../../Cosmic/dependencies/imgui/imgui_tables.cpp"
        "../../Cosmic/dependencies/implot/implot.cpp"
        "../../Cosmic/dependencies/implot/implot_items.cpp"
    )
endif()

# Build as a dynamic SHARED library module (.dll)
add_library(DinoProject SHARED ${DINO_SOURCES})

# -------------------------------------------------------------------
# 3. Include Paths
# -------------------------------------------------------------------
target_include_directories(DinoProject PRIVATE 
    src
    ../../Cosmic/src
    ../../Cosmic/dependencies/imgui
    ../../Cosmic/dependencies/implot
    ../../Cosmic/dependencies/glm
    ../../Cosmic/dependencies/spdlog/include
)

# -------------------------------------------------------------------
# 4. Smart Target Linking Architecture
# -------------------------------------------------------------------
if(TARGET Cosmic)
    # If compiled via root build_all.bat, use direct CMake memory references
    target_link_libraries(DinoProject PRIVATE Cosmic)
else()
    # If compiled standalone, link against your compiled Engine Import library map file
    target_link_libraries(DinoProject PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../build/Runtime/Debug/Cosmic.lib")
endif()

# -------------------------------------------------------------------
# 5. Output Redirection Pathing Configuration
# -------------------------------------------------------------------
set_target_properties(DinoProject PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../build/Runtime"
)

# -------------------------------------------------------------------
# 6. Project-Specific Asset Management
# -------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_custom_command(TARGET DinoProject POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:DinoProject>/assets/projects/DinoProject"
        COMMENT "Syncing DinoProject game assets to sandbox output directory..."
    )
endif()