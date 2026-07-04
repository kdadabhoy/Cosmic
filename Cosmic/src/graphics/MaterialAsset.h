#pragma once

// graphics/MaterialAsset.h
//
// ============================================================================
// COSMIC ENGINE — MaterialAsset (serializable PBR material description) [E17]
// ============================================================================
//
// A plain, reflected description of a physically-based material: the data that
// lives in a `.cmat` file and drives the Material Editor. It is NOT the GPU
// object — AssetLibrary::GetMaterial(path) loads one of these and builds a live
// Ref<Material> bound to the engine PBR shader (mapping each field onto the
// u_Albedo/u_Metallic/... uniform contract + the u_*Map / u_Has*Map samplers).
//
// Because every field is reflected (TypeRegistry, E1) the Material Editor's UI
// and the `.cmat` (de)serialization are BOTH generic — no per-field code. Map
// paths are project:// AssetPath strings; empty means "no map, use the factor".
// ============================================================================

#include "core/Core.h"
#include "scene/ComponentRegistry.h"   // CS_REGISTER_COMPONENT (stable cross-DLL type hash)

#include <glm/glm.hpp>
#include <string>

namespace Cosmic
{
    struct COSMIC_API MaterialAsset
    {
        // Scalar / colour factors (map 1:1 onto PBR.glsl uniforms).
        glm::vec4 Albedo{ 0.8f, 0.8f, 0.8f, 1.0f };   // base colour (linear); a = alpha
        float     Metallic  = 0.0f;
        float     Roughness = 0.5f;
        float     AO        = 1.0f;
        glm::vec3 Emissive{ 0.0f };
        bool      Transparent = false;                // back-to-front, depth-write off

        // Optional texture maps (project:// paths; empty = use the factor above).
        std::string AlbedoMap;
        std::string NormalMap;
        std::string MetalRoughMap;   // glTF packing: roughness=G, metallic=B
        std::string AOMap;
        std::string EmissiveMap;

        MaterialAsset() = default;
        MaterialAsset(const MaterialAsset&) = default;
    };
}

CS_REGISTER_COMPONENT(Cosmic::MaterialAsset)
