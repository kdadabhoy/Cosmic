cmake_minimum_required(VERSION 3.21)
project(CosmicRoot LANGUAGES C CXX)

# These settings flow down into Cosmic and Sandbox automatically
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -------------------------------------------------------------------
# 1. Subdirectories
# -------------------------------------------------------------------

# 1. Build the Engine first. 
# It sets up glad, glfw, and imgui for everyone else.
add_subdirectory(Cosmic)

# 2. Build the Sandbox. 
# It will link to 'Cosmic' and inherit its 'src' include path.
add_subdirectory(Sandbox)