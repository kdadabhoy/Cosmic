cmake_minimum_required(VERSION 3.21)
project(CosmicRuntime LANGUAGES C CXX)

# CosmicApp.rc embeds app.ico + VERSIONINFO — MSVC compiles the .rc and links them
# into the exe (icon in Explorer/taskbar, version in Properties > Details).
# CosmicApp.manifest declares PerMonitorV2 DPI awareness + longPathAware; CMake
# embeds *.manifest sources automatically via mt.exe on MSVC. Harmless on non-MSVC.
add_executable(CosmicApp Main.cpp CosmicApp.rc CosmicApp.manifest)

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

    # Release: link as a Windows GUI app so NO console/terminal window opens.
    # Debug keeps the default console subsystem so spdlog output stays visible
    # while developing. /ENTRY:mainCRTStartup preserves the existing int main()
    # entry point under the WINDOWS subsystem (no WinMain needed).
    target_link_options(CosmicApp PRIVATE
        $<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS>
        $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>
    )
endif()

# Packaging (cmake --install): the host exe lands at the package root.
install(TARGETS CosmicApp RUNTIME DESTINATION .)