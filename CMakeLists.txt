cmake_minimum_required(VERSION 3.21)
project(CosmicRoot LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)




# -------------------------------------------------------------------
# 1. Subdirectories
# -------------------------------------------------------------------

# We call the Engine folder first. 
# Since the dependencies live INSIDE it, the Engine CMake 
# will be the one responsible for setting up GLFW, Glad, etc.
add_subdirectory(Cosmic)

# Then we call the Sandbox, which will link to the engine.
add_subdirectory(Sandbox)