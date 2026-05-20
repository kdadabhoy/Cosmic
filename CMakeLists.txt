cmake_minimum_required(VERSION 3.21)
project(CosmicRoot LANGUAGES C CXX)

# -------------------------------------------------------------------
# 1. Global Project Standards
# -------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC specific optimizations
if(MSVC)
    add_compile_options(/utf-8)
endif()

# -------------------------------------------------------------------
# 2. Windows-Specific Cleanups (The "Anti-Conflict" Block)
# -------------------------------------------------------------------
if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX) 
endif()

# -------------------------------------------------------------------
# 3. Host and Guest Subdirectories
# -------------------------------------------------------------------
add_subdirectory(Cosmic)
add_subdirectory(Runtime)
add_subdirectory(Projects/DinoProject)