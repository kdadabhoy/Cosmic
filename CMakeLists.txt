cmake_minimum_required(VERSION 3.21)
project(CosmicRoot LANGUAGES C CXX)

# Global fallback standards for newly discovered targets
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # /std:c++20 forces MSVC to activate concepts, <bit>, and <compare> templates
    add_compile_options(/utf-8 /std:c++20)

    # /MP — compile the translation units WITHIN each target across all cores.
    # This sits at the root directory scope, ahead of every add_subdirectory, so
    # it reaches the engine, the game projects, the tests and the vendored
    # dependencies alike. Until Phase 29 W8 measured it, /MP appeared exactly
    # once in the whole build — inside assimp's own CMakeLists — so assimp was
    # the only thing using more than one core and everything else compiled
    # strictly serially. `cmake --build --parallel` does not cover this: on the
    # Visual Studio generator it gives MSBuild parallel PROJECTS, not parallel
    # FILES, and this build is a deep dependency chain of few projects.
    # Measured effect on a clean Release build (16 logical cores) in §5.3 of
    # docs/plans/28-phase29-engine-split-plan.md. assimp keeps its own /MP; a
    # repeated /MP is accepted silently by cl.
    #
    # Do NOT express this as -DCMAKE_CXX_FLAGS=/MP: that REPLACES CMake's MSVC
    # defaults (/DWIN32 /D_WINDOWS /EHsc), which silently disables exceptions.
    add_compile_options(/MP)

    # Release ships console-less (/SUBSYSTEM:WINDOWS), so emit PDBs and keep them
    # linked for symbolizing crash dumps. /DEBUG would normally disable the linker
    # optimizations /O2 expects, so restore them with /OPT:REF and /OPT:ICF. The
    # PDBs stay out of the packaged dist/ folder (install rules don't copy them).
    add_compile_options($<$<CONFIG:Release>:/Zi>)
    add_link_options(
        $<$<CONFIG:Release>:/DEBUG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
else()
    # Ensure standard compliance/warnings for GCC/Clang alternatives
    add_compile_options(-Wall -Wextra -std=c++20)
endif()

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX) 
endif()

# CRITICAL: Dynamically register the absolute layout anchor location of the SDK core
set(COSMIC_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Root absolute workspace path for Cosmic SDK")

# Multi-configuration helper: Single-config generators (Ninja/Make) use CMAKE_BUILD_TYPE.
# If it's empty, we default to Debug so $<CONFIG> maps safely.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build." FORCE)
endif()

option(COSMIC_BUILD_ENGINE_ONLY "Build only the core engine components" OFF)

# Phase 29 engine split — the two engine configurations. Declared here, ahead of
# add_subdirectory(Cosmic), so the flags are in the cache before anything reads
# them: the engine's source partition, the test target and the project scanner
# below all key off COSMIC_2D_ONLY. Cosmic/CMakeLists.txt declares the same two
# options (a no-op once these cache entries exist) so that directory still
# configures on its own. See docs/plans/28-phase29-engine-split-plan.md.
option(COSMIC_2D_ONLY "Build the 2D-only engine (excludes all 3D subsystems)" ON)
option(COSMIC_WITH_JOLT "Build the Jolt physics backend" ON)

# 2D stability trunk enforcement (WO-03, KI-3). `main` is the 2D-only trunk: the
# ONLY supported shipping configuration here is COSMIC_2D_ONLY=ON, which is why
# the option now defaults ON. Reject an explicit OFF (from a stale cache carried
# over from an old 3D configure, a copied 3D command line, or a script default)
# at CONFIGURE time — before any target is generated — so no build/CI/release/
# package path on this branch can ever silently produce a 3D or mixed binary.
# This guard is the enforcement point every entry point funnels through; the
# per-script -DCOSMIC_2D_ONLY=ON flags are explicitness on top of it, not the
# gate. The full 3D engine lives on the `engine-3d` branch (and the frozen
# `cosmic-pre-2d-2026-09-16` tag), where OFF stays supported and this guard is
# absent. Do not weaken this to a warning: a warning still generates a 3D tree.
if(NOT COSMIC_2D_ONLY)
    message(FATAL_ERROR
        "COSMIC_2D_ONLY=OFF is not supported on this branch.\n"
        "  main is the 2D-only stability trunk — it builds and ships the 2D engine only.\n"
        "  A stale build/ cache from a previous 3D configure is the usual cause: delete the\n"
        "  build directory and reconfigure (it will default to the 2D engine).\n"
        "  For a full 3D build, check out the 'engine-3d' branch, where 3D is supported.")
endif()

# Distribution is no longer a separate cache flag — it is implied by the Release
# configuration (see Cosmic/CMakeLists.txt, where COSMIC_DIST is defined via a
# per-config generator expression). Any Release build is a distribution build.

# Bundle the redistributable Visual C++ runtime DLLs next to the exe so the
# packaged app runs on a clean machine without a separately installed VC++ Redist.
# Only the Release CRT is redistributable, so packaging always installs --config Release.
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
include(InstallRequiredSystemLibraries)

# -------------------------------------------------------------------
# 3. Host and Guest Subdirectories
# -------------------------------------------------------------------
add_subdirectory(Cosmic)
add_subdirectory(Runtime)

# Headless unit tests (tests/). Built by default; never installed/packaged.
option(COSMIC_BUILD_TESTS "Build the CosmicTests unit-test target" ON)
if(COSMIC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Golden-image render tests (tests/render/). OFF by default and NEVER in CI: the
# target opens a real window and compares GPU output against committed PNGs, so
# it needs a GPU and its results are driver-specific. Runs under BOTH engine
# configurations (Phase 29 §9.5) — the 2D goldens are shared, the 3D ones are
# gated inside the target. Built by hand:
#   cmake -S . -B build -A x64 -DCOSMIC_BUILD_RENDER_TESTS=ON
option(COSMIC_BUILD_RENDER_TESTS "Build the CosmicRenderTests golden-image target (needs a real GPU; local only)" OFF)
if(COSMIC_BUILD_RENDER_TESTS)
    enable_testing()
    add_subdirectory(tests/render)
endif()

# Projects the scanner ignores. The 3D flagship projects that could not build
# against the 2D engine — Frontier, Engine3DDemo, ForgeIsle and ViperSim — were
# REMOVED from this 2D-only trunk (2026-09-17): they are for 3D demonstration only
# and are preserved on the `engine-3d` branch and the `cosmic-pre-2d-2026-09-16`
# tag (the frozen full-tree snapshot). The two remaining projects, SF_Telem and
# Starforge, both build in 2D, so nothing is skipped in either mode now. The skip
# machinery is retained (still hand-overridable via -DCOSMIC_SKIP_PROJECTS="Foo;Bar")
# but both mode defaults are empty.
#
# Starforge builds in 2D with its 3D panels, menus and overlays fenced out (Phase 29
# W7, §8.5); Projects/Starforge/CMakeLists.txt drops the three 3D-only TUs.
set(COSMIC_SKIP_PROJECTS_3D "")
set(COSMIC_SKIP_PROJECTS_2D "")
if(COSMIC_2D_ONLY)
    set(SKIP_DEFAULT "${COSMIC_SKIP_PROJECTS_2D}")
    set(SKIP_OTHER   "${COSMIC_SKIP_PROJECTS_3D}")
else()
    set(SKIP_DEFAULT "${COSMIC_SKIP_PROJECTS_3D}")
    set(SKIP_OTHER   "${COSMIC_SKIP_PROJECTS_2D}")
endif()
set(COSMIC_SKIP_PROJECTS "${SKIP_DEFAULT}" CACHE STRING
    "Semicolon-separated Projects/ subdirectory names the scanner skips")
# The cache is sticky, but this default is mode-derived, so a stored value can go
# stale two ways: flipping COSMIC_2D_ONLY in an existing tree (the list is now the
# OTHER mode's), or editing a default here (W7 took Starforge off the 2D list —
# an already-configured 2D tree would have gone on skipping the editor silently).
# COSMIC_SKIP_PROJECTS_APPLIED remembers the default this tree last derived, so
# both cases are detectable and a HAND-EDITED list is still left alone.
set(COSMIC_SKIP_PROJECTS_APPLIED "${SKIP_DEFAULT}" CACHE INTERNAL
    "The mode default COSMIC_SKIP_PROJECTS was last derived from")
if(NOT COSMIC_SKIP_PROJECTS STREQUAL "${SKIP_DEFAULT}" AND
   (COSMIC_SKIP_PROJECTS STREQUAL "${SKIP_OTHER}" OR
    COSMIC_SKIP_PROJECTS STREQUAL "${COSMIC_SKIP_PROJECTS_APPLIED}"))
    set_property(CACHE COSMIC_SKIP_PROJECTS PROPERTY VALUE "${SKIP_DEFAULT}")
endif()
set(COSMIC_SKIP_PROJECTS_APPLIED "${SKIP_DEFAULT}" CACHE INTERNAL
    "The mode default COSMIC_SKIP_PROJECTS was last derived from" FORCE)

# AUTOMATED GAME PROJECT SCANNER
if(NOT COSMIC_BUILD_ENGINE_ONLY)
    file(GLOB PROJECT_SUBDIRS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/Projects" "Projects/*")

    if(COSMIC_SKIP_PROJECTS)
        message(STATUS "--> Skipping game projects: ${COSMIC_SKIP_PROJECTS}")
    endif()

    foreach(SUBDIR ${PROJECT_SUBDIRS})
        set(FULL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Projects/${SUBDIR}")

        # A skipped project drops out of the scan entirely, including the install
        # rules below — those name the target after the directory, so a project that
        # is never added must never be installed either.
        if(IS_DIRECTORY "${FULL_PATH}" AND EXISTS "${FULL_PATH}/CMakeLists.txt"
           AND NOT "${SUBDIR}" IN_LIST COSMIC_SKIP_PROJECTS)
            message(STATUS "--> Auto-detected game project: ${SUBDIR}")
            add_subdirectory("Projects/${SUBDIR}")

            # Generic per-project packaging rule — relies on the convention that the
            # project's target name matches its directory name (true for all current
            # projects). Project DLLs land in projects/ (the launcher scans there);
            # their assets mirror the assets/projects/<name>/ VFS layout the POST_BUILD
            # sync produces.
            install(TARGETS ${SUBDIR}
                RUNTIME DESTINATION projects
                LIBRARY DESTINATION projects
                OPTIONAL)
            if(EXISTS "${FULL_PATH}/assets")
                install(DIRECTORY "${FULL_PATH}/assets/"
                    DESTINATION "assets/projects/${SUBDIR}")
            endif()
        endif()
    endforeach()
endif()