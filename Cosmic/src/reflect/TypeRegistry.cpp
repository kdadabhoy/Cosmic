// reflect/TypeRegistry.cpp — the process-wide registry singleton + built-in
// engine component registration (Phase 13 / E1).

#include "reflect/TypeRegistry.h"
#include "scene/Components.h"

namespace Cosmic::Reflect
{
    TypeRegistry& GetRegistry()
    {
        // Constructed once and intentionally leaked: the registry must outlive
        // every DLL that might touch it during static teardown, so we never run
        // its destructor. RegisterEngineTypes writes straight into *r (via
        // ClassIn) and never re-enters GetRegistry(), so there is no recursive
        // static-init hazard.
        static TypeRegistry* s_Registry = [] {
            auto* r = new TypeRegistry();
            RegisterEngineTypes(*r);
            return r;
        }();
        return *s_Registry;
    }

    void RegisterEngineTypes(TypeRegistry& r)
    {
        // Idempotent: re-registering just overwrites the same descriptors.
        ClassIn<TagComponent>(r, "Tag", "Core")
            .Field("Tag", &TagComponent::Tag);

        ClassIn<TransformComponent>(r, "Transform", "Core")
            .Field("Position",       &TransformComponent::Position)
            .Field("Rotation",       &TransformComponent::Rotation).Tooltip("Euler degrees (X,Y,Z); Z is 2D roll")
            .Field("Scale",          &TransformComponent::Scale)
            // Quat policy (Components.h): the two rotation representations are
            // INDEPENDENT — writing one does not sync the other. Both are
            // reflected so an editor can expose whichever an entity uses.
            .Field("RotationQuat",    &TransformComponent::RotationQuat)
            .Field("UseQuatRotation", &TransformComponent::UseQuatRotation);

        ClassIn<SpriteRendererComponent>(r, "SpriteRenderer", "Rendering")
            .Field("Color", &SpriteRendererComponent::Color).Color()
            .Field("FlipX", &SpriteRendererComponent::FlipX)
            .Field("FlipY", &SpriteRendererComponent::FlipY);

        // MeshRenderer v1 reflects the scalar fields only. The Ref<Mesh>/
        // Ref<Material> members surface as AssetPath fields once E16 gives
        // assets stable project:// paths (E1 gotcha).
        ClassIn<MeshRendererComponent>(r, "MeshRenderer", "Rendering")
            .Field("Color",       &MeshRendererComponent::Color).Color()
            .Field("CastShadows", &MeshRendererComponent::CastShadows);

        ClassIn<LODGroupComponent>(r, "LODGroup", "Rendering")
            .Field("Color",       &LODGroupComponent::Color).Color()
            .Field("CastShadows", &LODGroupComponent::CastShadows);

        ClassIn<DirectionalLightComponent>(r, "DirectionalLight", "Lighting")
            .Field("Direction", &DirectionalLightComponent::Direction).Tooltip("Direction the light travels")
            .Field("Color",     &DirectionalLightComponent::Color).Color()
            .Field("Intensity", &DirectionalLightComponent::Intensity).Range(0.0f, 10.0f);

        ClassIn<PointLightComponent>(r, "PointLight", "Lighting")
            .Field("Color",     &PointLightComponent::Color).Color()
            .Field("Intensity", &PointLightComponent::Intensity).Range(0.0f, 20.0f)
            .Field("Radius",    &PointLightComponent::Radius).Range(0.0f, 100.0f);

        ClassIn<CameraComponent>(r, "Camera", "Rendering")
            .Field("Primary",   &CameraComponent::Primary)
            .Field("ProjectionType", &CameraComponent::ProjectionType)
                .EnumValue("Perspective", 0).EnumValue("Orthographic", 1)
            .Field("FovDeg",    &CameraComponent::FovDeg).Range(10.0f, 170.0f)
            .Field("Near",      &CameraComponent::Near).Range(0.001f, 100.0f)
            .Field("Far",       &CameraComponent::Far).Range(1.0f, 100000.0f)
            .Field("OrthoSize", &CameraComponent::OrthoSize).Range(0.1f, 1000.0f);

        ClassIn<EnvironmentComponent>(r, "Environment", "Rendering")
            .Field("SunDirection", &EnvironmentComponent::SunDirection).Tooltip("Direction the sun light travels")
            .Field("SunColor",     &EnvironmentComponent::SunColor).Color()
            .Field("SunIntensity", &EnvironmentComponent::SunIntensity).Range(0.0f, 10.0f)
            .Field("Sky",          &EnvironmentComponent::Sky)
                .EnumValue("Procedural", 0).EnumValue("Detailed", 1).EnumValue("HDRI", 2)
            .Field("HdriPath",     &EnvironmentComponent::HdriPath).AsAssetPath("texture")
            .Field("TimeOfDay",    &EnvironmentComponent::TimeOfDay).Range(0.0f, 24.0f)
            .Field("Skybox",       &EnvironmentComponent::Skybox)
            .Field("IBL",          &EnvironmentComponent::IBL)
            .Field("IBLIntensity", &EnvironmentComponent::IBLIntensity).Range(0.0f, 4.0f)
            .Field("Exposure",     &EnvironmentComponent::Exposure).Range(0.0f, 8.0f)
            .Field("Fog",              &EnvironmentComponent::Fog)
            .Field("FogColor",         &EnvironmentComponent::FogColor).Color()
            .Field("FogDensity",       &EnvironmentComponent::FogDensity).Range(0.0f, 1.0f)
            .Field("FogHeightFalloff", &EnvironmentComponent::FogHeightFalloff).Range(0.0f, 2.0f)
            .Field("FogBaseHeight",    &EnvironmentComponent::FogBaseHeight)
            .Field("Bloom",            &EnvironmentComponent::Bloom)
            .Field("BloomThreshold",   &EnvironmentComponent::BloomThreshold).Range(0.0f, 8.0f)
            .Field("BloomIntensity",   &EnvironmentComponent::BloomIntensity).Range(0.0f, 4.0f)
            .Field("SSAO",             &EnvironmentComponent::SSAO)
            .Field("SsaoRadius",       &EnvironmentComponent::SsaoRadius).Range(0.0f, 4.0f)
            .Field("FXAA",             &EnvironmentComponent::FXAA)
            .Field("LensFlare",          &EnvironmentComponent::LensFlare)
            .Field("LensFlareIntensity", &EnvironmentComponent::LensFlareIntensity).Range(0.0f, 2.0f);

        // World-system holders carry only Ref<> assets (no scalar fields) — they
        // register as opaque-asset stubs so E8's Inspector shows their presence
        // and the serializer round-trips the (empty) block. Asset-path fields
        // arrive with the authoring work orders (E18).
        ClassIn<TerrainComponent>(r, "Terrain", "World");
        ClassIn<WaterComponent>(r, "Water", "World");
        ClassIn<ParticleEmitterComponent>(r, "ParticleEmitter", "World");

        // Scripting link (E11). ClassName is the only plain reflected field; the
        // dynamic script-field overrides (NativeScriptComponent::Fields) are handled
        // out-of-band by the serializer + the Inspector's script section, since they
        // depend on the ModuleRegistry's per-script descriptor.
        ClassIn<NativeScriptComponent>(r, "NativeScript", "Scripts")
            .Field("ClassName", &NativeScriptComponent::ClassName)
                .Tooltip("Registered C++ script class (ModuleRegistry).");

        // Prefab link (E14) — remembers the source .cprefab of an instantiated subtree.
        ClassIn<PrefabComponent>(r, "Prefab", "Core")
            .Field("SourcePath", &PrefabComponent::SourcePath).AsAssetPath("prefab");
    }
}
