# viper-fc — portable flight-computer library (doc 04 §1).
# Header-only C++17 INTERFACE target: the same headers compile in the ViperSim
# DLL (MSVC), the doctest unit-test runner, and the PlatformIO Teensy project
# (which consumes include/ directly — see firmware/platformio.ini).

cmake_minimum_required(VERSION 3.21)
project(viperfc LANGUAGES CXX)

add_library(viperfc INTERFACE)
target_include_directories(viperfc INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(viperfc INTERFACE cxx_std_17)

# ---------------------------------------------------------------------------
# Unit tests (plain doctest, NO engine — doc 04 §5). Built when the Cosmic
# vendored doctest is reachable (in-tree builds) and tests are enabled.
# ---------------------------------------------------------------------------
set(VIPERFC_DOCTEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../Cosmic/dependencies/doctest")
if(EXISTS "${VIPERFC_DOCTEST_DIR}/doctest.h" AND (COSMIC_BUILD_TESTS OR NOT DEFINED COSMIC_BUILD_TESTS))
    add_executable(ViperFcTests
        tests/test_main.cpp
        tests/test_math.cpp
        tests/test_mixer.cpp
        tests/test_transition.cpp
        tests/test_failsafe.cpp
        tests/test_estimator.cpp
    )
    target_link_libraries(ViperFcTests PRIVATE viperfc)
    target_include_directories(ViperFcTests PRIVATE "${VIPERFC_DOCTEST_DIR}")
    target_compile_definitions(ViperFcTests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
    if(MSVC)
        target_compile_options(ViperFcTests PRIVATE /utf-8)
    endif()

    if(DEFINED COSMIC_SDK_DIR)
        set_target_properties(ViperFcTests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>")
        if(COMMAND add_test)
            add_test(NAME ViperFcTests COMMAND ViperFcTests)
        endif()
    endif()
endif()
