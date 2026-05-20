cmake_minimum_required(VERSION 3.21)
project(CosmicRuntime LANGUAGES C CXX)

add_executable(CosmicApp Main.cpp)

# Replaced relative "../Cosmic" pathing with the global absolute SDK root variable
target_include_directories(CosmicApp PRIVATE 
    ${COSMIC_SDK_DIR}/Cosmic/src
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/implot
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/glm
)

target_link_libraries(CosmicApp PRIVATE Cosmic)

# FIXED: Enforce absolute configuration routing matching layout standards
set_target_properties(CosmicApp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>"
)

# FIXED: Lock the debugger execution context directly inside the current configuration folder
if(MSVC)
    set_target_properties(CosmicApp PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/\$(Configuration)"
    )
endif()