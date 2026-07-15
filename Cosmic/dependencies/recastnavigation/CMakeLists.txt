# -----------------------------------------------------------------------------
# Cosmic-vendored Recast & Detour navigation (N1, Phase 26).
#
# Recast ships its own top-level CMakeLists with RecastDemo (SDL/OpenGL), the
# Tests, Docs and an install/package protocol. We deliberately do NOT use it —
# we compile ONLY the four library module source trees (Recast, Detour,
# DetourCrowd, DetourTileCache) into ONE static lib with exactly the flags the
# engine needs. DebugUtils (RecastDebugDraw/DetourDebugDraw) is intentionally
# left out: nav debug draw is done through the engine's Renderer3D line/tri
# batch (the J8 collider-gizmo pattern), so we pull in no draw-backend glue.
#
# This mirrors dependencies/JoltPhysics/CMakeLists.txt exactly (the vendoring
# pattern of record, doc 14 J1). Pin: see README.md in this directory. Update
# the source tree + that pin together.
# -----------------------------------------------------------------------------

file(GLOB_RECURSE RECAST_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Recast/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Recast/Include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Detour/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Detour/Include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/DetourCrowd/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/DetourCrowd/Include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/DetourTileCache/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/DetourTileCache/Include/*.h")

add_library(RecastNavigation STATIC ${RECAST_SRC})
add_library(RecastNavigation::RecastNavigation ALIAS RecastNavigation)

# The modules `#include "DetourNavMesh.h"` etc. with flat names, so every
# module's Include dir must be visible. PUBLIC so the engine's nav/ TUs (the
# only consumers) can include them — but the lib is linked PRIVATE into Cosmic,
# so Recast/Detour headers never leak to game/project DLLs (the pimpl firewall).
target_include_directories(RecastNavigation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Recast/Include"
    "${CMAKE_CURRENT_SOURCE_DIR}/Detour/Include"
    "${CMAKE_CURRENT_SOURCE_DIR}/DetourCrowd/Include"
    "${CMAKE_CURRENT_SOURCE_DIR}/DetourTileCache/Include")

# Match the engine's language level (the rest of the build is /std:c++20) so
# MSVC sees ONE standard flag on the command line (the Jolt precedent). Recast
# itself is C++03-clean; compiling at 20 is harmless.
set_target_properties(RecastNavigation PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# Determinism (the J9/N4 two-run bit-match proof): Detour's path + crowd math is
# plain IEEE float and reproducible run-to-run PROVIDED the compiler does not
# reassociate / contract it. The engine does not enable /fp:fast or -ffast-math,
# so we inherit reproducible float codegen and add nothing here; if a future
# global fast-math flag is introduced, pin /fp:precise (MSVC) or -ffp-contract=off
# on this target to keep the nav determinism test green.

# 32-bit poly refs (DT_POLYREF64 off — the default). Documented so a future
# very-large tiled world knows the flip point (it changes the .cnav layout).

# Vendored third-party code: silence its warnings (the engine builds clean; a
# vendored target's warnings are not ours to fix and must never fail a build).
if(MSVC)
    target_compile_options(RecastNavigation PRIVATE /W0)
else()
    target_compile_options(RecastNavigation PRIVATE -w)
endif()
