cmake_minimum_required(VERSION 3.21)
project(CosmicRoot LANGUAGES C CXX)

# Global fallback standards for newly discovered targets
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # /std:c++20 forces MSVC to activate concepts, <bit>, and <compare> templates
    add_compile_options(/utf-8 /std:c++20)

    # Release ships console-less (/SUBSYSTEM:WINDOWS), so emit PDBs and keep them
    # linked for symbolizing crash dumps. /DEBUG would normally disable the linker
    # optimizations /O2 expects, so restore them with /OPT:REF and /OPT:ICF. The
    # PDBs stay out of the packaged dist/ folder (install rules don't copy them).
    add_compile_options($<$<CONFIG:Release>:/Zi>)
    add_link_options(
        $<$<CONFIG:Release>:/DEBUG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
else()
    # Ensure standard compliance/warnings for GCC/Clang alternatives
    add_compile_options(-Wall -Wextra -std=c++20)
endif()

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX) 
endif()

# CRITICAL: Dynamically register the absolute layout anchor location of the SDK core
set(COSMIC_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Root absolute workspace path for Cosmic SDK")

# Multi-configuration helper: Single-config generators (Ninja/Make) use CMAKE_BUILD_TYPE.
# If it's empty, we default to Debug so $<CONFIG> maps safely.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build." FORCE)
endif()

option(COSMIC_BUILD_ENGINE_ONLY "Build only the core engine components" OFF)

# Distribution is no longer a separate cache flag — it is implied by the Release
# configuration (see Cosmic/CMakeLists.txt, where COSMIC_DIST is defined via a
# per-config generator expression). Any Release build is a distribution build.

# Bundle the redistributable Visual C++ runtime DLLs next to the exe so the
# packaged app runs on a clean machine without a separately installed VC++ Redist.
# Only the Release CRT is redistributable, so packaging always installs --config Release.
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
include(InstallRequiredSystemLibraries)

# -------------------------------------------------------------------
# 3. Host and Guest Subdirectories
# -------------------------------------------------------------------
add_subdirectory(Cosmic)
add_subdirectory(Runtime)

# AUTOMATED GAME PROJECT SCANNER
if(NOT COSMIC_BUILD_ENGINE_ONLY)
    file(GLOB PROJECT_SUBDIRS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/Projects" "Projects/*")

    foreach(SUBDIR ${PROJECT_SUBDIRS})
        set(FULL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Projects/${SUBDIR}")

        if(IS_DIRECTORY "${FULL_PATH}" AND EXISTS "${FULL_PATH}/CMakeLists.txt")
            message(STATUS "--> Auto-detected game project: ${SUBDIR}")
            add_subdirectory("Projects/${SUBDIR}")

            # Generic per-project packaging rule — relies on the convention that the
            # project's target name matches its directory name (true for all current
            # projects). Lands the DLL next to CosmicApp.exe and mirrors the
            # assets/projects/<name>/ VFS layout the POST_BUILD sync produces.
            install(TARGETS ${SUBDIR}
                RUNTIME DESTINATION .
                LIBRARY DESTINATION .
                OPTIONAL)
            if(EXISTS "${FULL_PATH}/assets")
                install(DIRECTORY "${FULL_PATH}/assets/"
                    DESTINATION "assets/projects/${SUBDIR}")
            endif()
        endif()
    endforeach()
endif()