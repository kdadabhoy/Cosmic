cmake_minimum_required(VERSION 3.21)
project(DinoProject LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX) 
endif()

# -------------------------------------------------------------------
# 1. SDK Path Resolution
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
# 3. Include Paths
# -------------------------------------------------------------------
# UPDATED: Appended the dependency path for EnTT so templates compile cleanly standalone
target_include_directories(DinoProject PRIVATE 
    src
    ${COSMIC_SDK_DIR}/Cosmic/src
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/implot
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/glm
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/entt/src
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/spdlog/include
)

# -------------------------------------------------------------------
# 4. Agnostic Target Linking Architecture
# -------------------------------------------------------------------
if(TARGET Cosmic)
    target_link_libraries(DinoProject PRIVATE Cosmic)
else()
    # Create the imported placeholder target
    add_library(CosmicCore_Imported SHARED IMPORTED)
    
    if(WIN32)
        # Fix the literal expression bug by using per-configuration properties
        set_target_properties(CosmicCore_Imported PROPERTIES
            IMPORTED_IMPLIB_DEBUG   "${COSMIC_SDK_DIR}/build/Runtime/Debug/Cosmic.lib"
            IMPORTED_LOCATION_DEBUG "${COSMIC_SDK_DIR}/build/Runtime/Debug/Cosmic.dll"
            
            IMPORTED_IMPLIB_RELEASE   "${COSMIC_SDK_DIR}/build/Runtime/Release/Cosmic.lib"
            IMPORTED_LOCATION_RELEASE "${COSMIC_SDK_DIR}/build/Runtime/Release/Cosmic.dll"
            
            # MAP FALLBACKS: Tells CMake how to route missing configurations
            MAP_IMPORTED_CONFIG_MINSIZEREL   "Release"
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO "Release"
        )
    else()
        # Non-Windows systems fallbacks (.so / .dylib)
        set_target_properties(CosmicCore_Imported PROPERTIES
            IMPORTED_LOCATION "${COSMIC_SDK_DIR}/build/Runtime/Debug/libCosmic${CMAKE_SHARED_LIBRARY_SUFFIX}"
        )
    endif()
    
    target_link_libraries(DinoProject PRIVATE CosmicCore_Imported)
endif()

# -------------------------------------------------------------------
# 5. Output Redirection Configuration
# -------------------------------------------------------------------
set_target_properties(DinoProject PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$$<CONFIG>"
)

# -------------------------------------------------------------------
# 6. Project-Specific Asset Management
# -------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_custom_command(TARGET DinoProject POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "${COSMIC_SDK_DIR}/build/Runtime/$$<CONFIG>/assets/projects/DinoProject"
        COMMENT "Syncing DinoProject game assets to sandbox output directory..."
    )
endif()