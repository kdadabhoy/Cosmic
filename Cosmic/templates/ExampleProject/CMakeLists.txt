cmake_minimum_required(VERSION 3.21)
project(TemplateProject LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
    # Force MSVC Code Analysis and the compiler to treat SYSTEM paths as completely external 
    # to suppress third-party dependency warnings (C6294, C26498, C26800, C26495, etc.)
    add_compile_options(/external:W0)
    add_compile_options(/external:templates-)
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
file(GLOB_RECURSE TEMPLATE_SOURCES "src/*.cpp" "src/*.h")

if(NOT TARGET Cosmic)
    list(APPEND TEMPLATE_SOURCES
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui.cpp"
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui_draw.cpp"
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui_widgets.cpp"
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui_tables.cpp"
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/implot/implot.cpp"
        "${COSMIC_SDK_DIR}/Cosmic/dependencies/implot/implot_items.cpp"
    )
endif()

add_library(TemplateProject SHARED ${TEMPLATE_SOURCES})

# -------------------------------------------------------------------
# 3. Include Paths
# -------------------------------------------------------------------
# Keep your own local source code strict
target_include_directories(TemplateProject PRIVATE 
    src
)

# Suppress all warnings from engine dependencies by marking them SYSTEM
target_include_directories(TemplateProject SYSTEM PRIVATE 
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
    target_link_libraries(TemplateProject PRIVATE Cosmic)
else()
    add_library(CosmicCore_Imported SHARED IMPORTED)
    
    if(WIN32)
        set_target_properties(CosmicCore_Imported PROPERTIES
            IMPORTED_IMPLIB_DEBUG   "${COSMIC_SDK_DIR}/build/Runtime/Debug/Cosmic.lib"
            IMPORTED_LOCATION_DEBUG "${COSMIC_SDK_DIR}/build/Runtime/Debug/Cosmic.dll"
            
            IMPORTED_IMPLIB_RELEASE   "${COSMIC_SDK_DIR}/build/Runtime/Release/Cosmic.lib"
            IMPORTED_LOCATION_RELEASE "${COSMIC_SDK_DIR}/build/Runtime/Release/Cosmic.dll"
            
            MAP_IMPORTED_CONFIG_MINSIZEREL   "Release"
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO "Release"
        )
    else()
        set_target_properties(CosmicCore_Imported PROPERTIES
            IMPORTED_LOCATION "${COSMIC_SDK_DIR}/build/Runtime/Debug/libCosmic${CMAKE_SHARED_LIBRARY_SUFFIX}"
        )
    endif()
    
    target_link_libraries(TemplateProject PRIVATE CosmicCore_Imported)
endif()

# -------------------------------------------------------------------
# 5. Output Redirection Configuration
# -------------------------------------------------------------------
set_target_properties(TemplateProject PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>"
)

# -------------------------------------------------------------------
# 6. Project-Specific Asset Management
# -------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_custom_command(TARGET TemplateProject POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>/assets/projects/TemplateProject"
        COMMENT "Syncing TemplateProject game assets to sandbox output directory..."
    )
endif()