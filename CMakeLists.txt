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

# COSMIC_2D_ONLY — a compatibility no-op since AP-05 (App Platform packet). The
# 3D subsystems, their vendored dependencies and every in-file fence were purged
# from this trunk, so the option no longer selects anything: the engine source IS
# the 2D engine. It is kept (default ON) so every existing configure line keeps
# working unchanged — the build_*.bat scripts, CI, Starforge's BuildRunner and
# the external project CMakeLists all still pass -DCOSMIC_2D_ONLY=ON — and so the
# reject-OFF gate below can still refuse a stale 3D cache. Declared here, ahead
# of add_subdirectory(Cosmic), so the flags are in the cache before anything
# reads them; Cosmic/CMakeLists.txt declares the same two options (a no-op once
# these cache entries exist) so that directory still configures on its own.
option(COSMIC_2D_ONLY "Compatibility no-op: this trunk is the 2D-only engine (OFF is rejected)" ON)
option(COSMIC_WITH_JOLT "Build the Jolt physics backend" ON)

# 2D stability trunk enforcement (WO-03, KI-3). `main` is the 2D-only trunk and,
# since AP-05, its only source: there is no 3D engine here to build. Reject an
# explicit OFF (from a stale cache carried over from an old 3D configure, a copied
# 3D command line, or a script default) at CONFIGURE time — before any target is
# generated — so no build/CI/release/package path on this branch can ever claim
# to produce a 3D or mixed binary. This guard is the enforcement point every entry
# point funnels through; the per-script -DCOSMIC_2D_ONLY=ON flags are explicitness
# on top of it, not the gate. The full 3D engine lives on the `engine-3d` branch
# (and the frozen `cosmic-pre-2d-2026-09-16` tag), where OFF stays supported and
# this guard is absent. Do not weaken this to a warning.
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
# Starforge, both build here (Starforge lost its 3D panels, menus and overlays
# with the AP-05 purge), so nothing but the external specimen below is skipped.
# The skip machinery is retained (still hand-overridable via
# -DCOSMIC_SKIP_PROJECTS="Foo;Bar").
#
# AnalysisSample (WO-10 / X01, 2D stability) is an EXTERNAL-consumer specimen: its
# source is committed under Projects/ so the consumption path is reproducible, but
# it is configured and built on its own against COSMIC_SDK_DIR (the way the
# Starforge packager builds any external project) — never as part of this build,
# so it stays out of the scan (and out of the install rules). Its own CMakeLists
# refuses to be added in-tree.
set(SKIP_DEFAULT "AnalysisSample")
set(COSMIC_SKIP_PROJECTS "${SKIP_DEFAULT}" CACHE STRING
    "Semicolon-separated Projects/ subdirectory names the scanner skips")
# The cache is sticky, so a stored value goes stale when the default here is
# edited (W7 took Starforge off the list — an already-configured tree would have
# gone on skipping the editor silently; WO-10 added AnalysisSample). Until AP-05
# the default was also mode-derived (a 2D and a 3D list), which is where this
# migration comes from. COSMIC_SKIP_PROJECTS_APPLIED remembers the default this
# tree last derived, so an unchanged stored list follows a new default while a
# HAND-EDITED list is still left alone. (CACHE INTERNAL implies FORCE, so this
# seed must be guarded — an unguarded set here overwrote the remembered default
# BEFORE the comparison below and the migration could never fire on an edited
# default; found by WO-10 when AnalysisSample joined the list.)
if(NOT DEFINED COSMIC_SKIP_PROJECTS_APPLIED)
    set(COSMIC_SKIP_PROJECTS_APPLIED "${SKIP_DEFAULT}" CACHE INTERNAL
        "The default COSMIC_SKIP_PROJECTS was last derived from")
endif()
if(NOT COSMIC_SKIP_PROJECTS STREQUAL "${SKIP_DEFAULT}" AND
   COSMIC_SKIP_PROJECTS STREQUAL "${COSMIC_SKIP_PROJECTS_APPLIED}")
    set_property(CACHE COSMIC_SKIP_PROJECTS PROPERTY VALUE "${SKIP_DEFAULT}")
endif()
set(COSMIC_SKIP_PROJECTS_APPLIED "${SKIP_DEFAULT}" CACHE INTERNAL
    "The default COSMIC_SKIP_PROJECTS was last derived from" FORCE)

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

            # Generic per-project rule for the DEVELOPER SDK bundle — relies on the
            # convention that the project's target name matches its directory name
            # (true for all current projects). Project DLLs land in projects/ (the
            # launcher scans there); their assets mirror the assets/projects/<name>/
            # VFS layout the POST_BUILD sync produces.
            #
            # NOT A SHIPPING PATH (AP-P1): `cmake --install` stages EVERY project into
            # one multi-app bundle, which no boot.cfg, installer or shortcut targets.
            # A shipped app comes from installer/Stage-AppPackage.ps1 (package.bat
            # <App>, release.yml) or the editor packager — one app, <App>.exe, boot.cfg,
            # licenses/, user/. Dev-only targets (CosmicTests, CosmicRenderTests,
            # fixtures) carry no install rule anywhere and can never be staged.
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