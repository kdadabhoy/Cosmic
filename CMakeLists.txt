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

# Define a global option to build ONLY the engine core and its runtime app
option(COSMIC_BUILD_ENGINE_ONLY "Build only the core engine components" OFF)

# -------------------------------------------------------------------
# 3. Host and Guest Subdirectories
# -------------------------------------------------------------------
add_subdirectory(Cosmic)
add_subdirectory(Runtime)

# If this flag is flipped on by the batch script, completely bypass games!
if(NOT COSMIC_BUILD_ENGINE_ONLY)
    add_subdirectory(Projects/DinoProject)
endif()