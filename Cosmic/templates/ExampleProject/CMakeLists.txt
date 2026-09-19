cmake_minimum_required(VERSION 3.21)
project(TemplateProject LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_options(/external:W0)
    add_compile_options(/external:templates-)
else()
    add_compile_options(-Wall -Wextra)
endif()

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX)
endif()

# Engine build mode — kept in step with the Cosmic SDK this plugin links against
# (WO-03 / 2D stability trunk). Since AP-05 the engine's PUBLIC headers carry no
# COSMIC_2D_ONLY fences (the trunk is 2D-only source), so the definition is a
# compatibility no-op: Cosmic.dll still carries the same PUBLIC define, a plugin
# may test it, and BuildRunner's hot-reload configure still passes
# -DCOSMIC_2D_ONLY=ON explicitly. In-tree (TARGET Cosmic) the define also
# propagates PUBLIC from the engine target — defining it here too is an
# identical, harmless duplicate.
option(COSMIC_2D_ONLY "Match the 2D-only Cosmic SDK this plugin links against" ON)

# -------------------------------------------------------------------
# 1. SDK Path Resolution
# -------------------------------------------------------------------
if(NOT COSMIC_SDK_DIR)
    set(COSMIC_SDK_DIR $ENV{COSMIC_SDK})
endif()

if(NOT COSMIC_SDK_DIR)
    message(FATAL_ERROR "Cosmic Engine SDK path not found! Set COSMIC_SDK environment variable or pass -DCOSMIC_SDK_DIR=<path>.")
endif()

# -------------------------------------------------------------------
# 2. Source Files
# -------------------------------------------------------------------
file(GLOB_RECURSE TEMPLATE_SOURCES "src/*.cpp" "src/*.h")

# When building standalone (outside the engine root), bundle vendor ImGui/ImPlot sources
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

# Stamp the engine build mode onto every plugin TU (see the option above).
if(COSMIC_2D_ONLY)
    target_compile_definitions(TemplateProject PRIVATE COSMIC_2D_ONLY)
endif()

# -------------------------------------------------------------------
# 3. Include Paths
# -------------------------------------------------------------------
target_include_directories(TemplateProject PRIVATE src)

target_include_directories(TemplateProject SYSTEM PRIVATE
    ${COSMIC_SDK_DIR}/Cosmic/src
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/implot
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/glm
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/entt/src
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/spdlog/include
)

# -------------------------------------------------------------------
# 4. Linking — agnostic (works in-tree or standalone)
# -------------------------------------------------------------------
if(TARGET Cosmic)
    target_link_libraries(TemplateProject PRIVATE Cosmic)
else()
    add_library(CosmicCore_Imported SHARED IMPORTED)

    if(WIN32)
        set_target_properties(CosmicCore_Imported PROPERTIES
            IMPORTED_IMPLIB_DEBUG     "${COSMIC_SDK_DIR}/build/Runtime/Debug/Cosmic.lib"
            IMPORTED_LOCATION_DEBUG   "${COSMIC_SDK_DIR}/build/Runtime/Debug/Cosmic.dll"
            IMPORTED_IMPLIB_RELEASE   "${COSMIC_SDK_DIR}/build/Runtime/Release/Cosmic.lib"
            IMPORTED_LOCATION_RELEASE "${COSMIC_SDK_DIR}/build/Runtime/Release/Cosmic.dll"
            MAP_IMPORTED_CONFIG_MINSIZEREL    "Release"
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
# 5. Output directories — land next to CosmicApp.exe
# -------------------------------------------------------------------
set_target_properties(TemplateProject PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>"
)

# -------------------------------------------------------------------
# 6. Asset sync
# -------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_custom_command(TARGET TemplateProject POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>/assets/projects/TemplateProject"
        COMMENT "Syncing TemplateProject assets to VFS output directory..."
    )
endif()