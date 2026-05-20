cmake_minimum_required(VERSION 3.21)
project(CosmicRuntime LANGUAGES C CXX)

add_executable(CosmicApp Main.cpp)

target_include_directories(CosmicApp PRIVATE 
    ${COSMIC_SDK_DIR}/Cosmic/src
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/implot
    ${COSMIC_SDK_DIR}/Cosmic/dependencies/glm
)

target_link_libraries(CosmicApp PRIVATE Cosmic)

set_target_properties(CosmicApp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>"
)

# FIXED: Removed the raw hardcoded MSVC build string "$(Configuration)" which breaks non-VS tools
if(MSVC)
    set_target_properties(CosmicApp PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>"
    )
endif()