cmake_minimum_required(VERSION 3.21)
project(CosmicRoot LANGUAGES C CXX)

# -------------------------------------------------------------------
# 1. Global Project Standards
# -------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC specific optimizations
# This fixes spdlog/fmt "Unicode support" errors in all sub-projects.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# -------------------------------------------------------------------
# 2. Windows-Specific Cleanups (The "Anti-Conflict" Block)
# -------------------------------------------------------------------
if(WIN32)
    # This prevents the APIENTRY redefinition warning and speeds up build times
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    # This prevents Windows macros from breaking std::min/max
    add_definitions(-DNOMINMAX) 
endif()

# -------------------------------------------------------------------
# 3. Subdirectories
# -------------------------------------------------------------------

# Now when these projects load, they already have the clean Windows environment
add_subdirectory(Cosmic)
add_subdirectory(Sandbox)