cmake_minimum_required(VERSION 3.21)
project(CosmicRuntime LANGUAGES C CXX)

add_executable(CosmicApp Main.cpp)

target_include_directories(CosmicApp PRIVATE 
    ../Cosmic/src
    ../Cosmic/dependencies/imgui
    ../Cosmic/dependencies/implot
    ../Cosmic/dependencies/glm
)

target_link_libraries(CosmicApp PRIVATE Cosmic)

# FIXED: Outputs directly to workspace bin directory
set_target_properties(CosmicApp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/Runtime"
)

if(MSVC)
    set_target_properties(CosmicApp PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/build/Runtime/\$(Configuration)"
    )
endif()