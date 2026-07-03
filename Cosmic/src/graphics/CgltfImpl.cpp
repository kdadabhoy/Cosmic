// CgltfImpl.cpp
// Last Modified 7/2/2026
//
// The ONE translation unit that compiles the vendored cgltf implementation
// (docs/plans/05-3d-engine-plan.md S4.4b — single header, MIT, glTF 2.0 parser).
// Everything else (graphics/Model.cpp) includes cgltf.h for declarations only.

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
