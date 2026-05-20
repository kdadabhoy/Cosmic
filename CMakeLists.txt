cmake_minimum_required(VERSION 3.21)
project(CosmicRoot LANGUAGES C CXX)

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

# CRITICAL: Dynamically register the absolute layout anchor location of the SDK core
set(COSMIC_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Root absolute workspace path for Cosmic SDK")

option(COSMIC_BUILD_ENGINE_ONLY "Build only the core engine components" OFF)

# -------------------------------------------------------------------
# 3. Host and Guest Subdirectories
# -------------------------------------------------------------------
add_subdirectory(Cosmic)
add_subdirectory(Runtime)

if(NOT COSMIC_BUILD_ENGINE_ONLY)
    add_subdirectory(Projects/DinoProject)
endif()