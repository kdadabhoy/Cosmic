cmake_minimum_required(VERSION 3.21)
project(DinoProject LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
endif()

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX) 
endif()

# -------------------------------------------------------------------
# 1. SDK Path Resolution (The Future-Proof Secret)
# -------------------------------------------------------------------
if(NOT COSMIC_SDK_DIR)
    set(COSMIC_SDK_DIR $ENV{COSMIC_SDK})
endif()

if(NOT COSMIC_SDK_DIR)
    message(FATAL_ERROR "Cosmic Engine SDK path not found! Please verify environment parameters.")
endif()

# -------------------------------------------------------------------
# 2. Identify Source Files
# -------------------------------------------------------------------
file(GLOB_RECURSE DINO_SOURCES "src/*.cpp" "src/*.h")

# NATIVE STANDALONE WORKAROUND: Fallback for third-party tools context linkages
if(NOT TARGET Cosmic)
    list(APPEND DINO_SOURCES
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui.cpp"
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui_draw.cpp"
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui_widgets.cpp"
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui_tables.cpp"
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/implot/implot.cpp"
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/implot/implot_items.cpp"
    )
endif()

add_library(DinoProject SHARED ${DINO_SOURCES})

# -------------------------------------------------------------------
# 3. Include Paths (Cleanly mapped from SDK variable root)
# -------------------------------------------------------------------
target_include_directories(DinoProject PRIVATE 
    src
    ${COSMIC_SDK_DIR}/Cosmic/src
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/implot
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/glm
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/spdlog/include
)

# -------------------------------------------------------------------
# 4. Smart Target Linking Architecture
# -------------------------------------------------------------------
if(TARGET Cosmic)
    target_link_libraries(DinoProject PRIVATE Cosmic)
else()
    target_link_libraries(DinoProject PRIVATE "${COSMIC_SDK_DIR}/build/Runtime/Debug/Cosmic.lib")
endif()

# -------------------------------------------------------------------
# 5. Output Redirection Configuration
# -------------------------------------------------------------------
set_target_properties(DinoProject PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime"
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