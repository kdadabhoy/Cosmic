# -----------------------------------------------------------------------------
# Cosmic-vendored Jolt Physics (J1, Phase 15).
#
# Jolt ships its own Build/CMakeLists.txt with dozens of samples/tools/tests and a
# large cache-variable protocol. We deliberately do NOT use it — we compile ONLY
# the library source tree (dependencies/JoltPhysics/Jolt) into one static lib with
# exactly the flags the engine needs. The optional Jolt .cpp files (Renderer/*,
# ObjectStream/*) are self-guarded by #ifdef, so a plain recursive glob is safe:
# they compile to nothing unless their define is set.
#
# Pin: see README.md in this directory. Update the source tree + that pin together.
# -----------------------------------------------------------------------------

file(GLOB_RECURSE JOLT_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Jolt/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Jolt/*.h")

add_library(Jolt STATIC ${JOLT_SRC})
add_library(Jolt::Jolt ALIAS Jolt)

# `#include <Jolt/...>` resolves against this directory (the repo root convention
# Jolt uses). PUBLIC so the engine's physics TUs (the only consumers) can include
# it — but Jolt is linked PRIVATE into Cosmic, so it never leaks to game DLLs.
target_include_directories(Jolt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# Match the engine's language level (the rest of the build is /std:c++20). Jolt's
# own Jolt.cmake would pin C++17; forcing 20 here keeps one standard flag on the
# command line so MSVC doesn't see a conflicting /std pair.
set_target_properties(Jolt PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# --- Instruction set (documented choice) -------------------------------------
# We intentionally stay at the MSVC x64 baseline (SSE2) for /arch and enable the
# SSE4.1/SSE4.2 code paths at the *define* level only. On MSVC, USE_AVX2 would add
# a PUBLIC /arch:AVX2 flag which — because Cosmic links Jolt — would recompile the
# ENTIRE engine for AVX2 and raise the min-spec / change shipped-app codegen. The
# SSE4.2 intrinsics Jolt selects with these defines are available on MSVC x64
# without any /arch flag, so nothing propagates to the rest of Cosmic. Flip to AVX2
# (add /arch:AVX2 + JPH_USE_AVX2/JPH_USE_AVX/JPH_USE_FMADD, mind the propagation)
# only if a profiler pass shows the narrow phase is a bottleneck.
target_compile_definitions(Jolt PUBLIC
    JPH_USE_SSE4_1
    JPH_USE_SSE4_2)

# Cross-platform floating-point determinism (disables FMA contraction + fast-math
# reassociation). Needed by the J8 two-run bit-match determinism proof. PUBLIC so
# the consuming TU compiles Jolt's inline math the same way.
target_compile_definitions(Jolt PUBLIC JPH_CROSS_PLATFORM_DETERMINISTIC)

# Asserts only in Debug (they route to the engine log via the trace/assert hooks
# PhysicsWorld installs). Release/Dist Jolt runs assert-free.
target_compile_definitions(Jolt PUBLIC $<$<CONFIG:Debug>:JPH_ENABLE_ASSERTS>)

# The debug renderer interface (line/triangle callbacks) is compiled only in Debug
# so PhysicsDebug can pull live body outlines; Release keeps Jolt lean.
target_compile_definitions(Jolt PUBLIC $<$<CONFIG:Debug>:JPH_DEBUG_RENDERER>)

# NDEBUG for the optimized configs (Jolt keys some fast paths off it, matching its
# own Build/CMakeLists). CMake already adds -DNDEBUG to Release flags; set it for
# every non-Debug config explicitly so RelWithDebInfo agrees.
target_compile_definitions(Jolt PUBLIC $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)

# Vendored third-party code: silence its warnings (the engine builds clean; Jolt's
# warnings are not ours to fix and must never fail an engine build).
if(MSVC)
    target_compile_options(Jolt PRIVATE /W0)
else()
    target_compile_options(Jolt PRIVATE -w)
    target_compile_options(Jolt PUBLIC -pthread)
    target_link_options(Jolt PUBLIC -pthread)
endif()
