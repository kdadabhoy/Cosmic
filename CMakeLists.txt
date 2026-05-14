cmake_minimum_required(VERSION 3.21)
project(CosmicRoot LANGUAGES C CXX)

# -------------------------------------------------------------------
# 1. Global Project Standards
# -------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC specific: Enable UTF-8 encoding globally. 
# This fixes spdlog/fmt "Unicode support" errors in all sub-projects.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# -------------------------------------------------------------------
# 2. Subdirectories
# -------------------------------------------------------------------

# Build the Engine first (Library)
add_subdirectory(Cosmic)

# Build the Sandbox (Executable)
add_subdirectory(Sandbox)