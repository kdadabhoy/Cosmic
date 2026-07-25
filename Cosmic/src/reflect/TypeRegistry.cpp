// reflect/TypeRegistry.cpp — the process-wide registry singleton + built-in
// engine component registration (Phase 13 / E1).

#include "reflect/TypeRegistry.h"
#include "scene/Components.h"
#include "scene/ui/UiComponents.h"    // U1 — in-game UI entity components
#include "graphics/MaterialAsset.h"   // E17 — .cmat reflected struct

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
            .Field("Tag", &TagComponent::Tag)
            // T13 — per-entity active flag (Hierarchy eye toggle, not an Inspector row).
            .Field("Active", &TagComponent::Active).HideInInspector().OmitIfTrue();

        ClassIn<TransformComponent>(r, "Transform", "Core")
            .Field("Position",       &TransformComponent::Position)
            .Field("Rotation",       &TransformComponent::Rotation).Tooltip("Euler degrees (X,Y,Z); Z is 2D roll").Degrees()
            .Field("Scale",          &TransformComponent::Scale)
            // Quat policy (Components.h): the two rotation representations are
            // INDEPENDENT — writing one does not sync the other. Both are
            // reflected so an editor can expose whichever an entity uses.
            .Field("RotationQuat",    &TransformComponent::RotationQuat)
            .Field("UseQuatRotation", &TransformComponent::UseQuatRotation);

        ClassIn<SpriteRendererComponent>(r, "SpriteRenderer", "Rendering")
            .Field("Color", &SpriteRendererComponent::Color).Color()
            .Field("FlipX", &SpriteRendererComponent::FlipX)
            .Field("FlipY", &SpriteRendererComponent::FlipY)
            // 2D authoring (U3).
            .Field("SourceRect",    &SpriteRendererComponent::SourceRect).Tooltip("Sampled sub-rect in normalized UV {u0,v0,u1,v1}")
            .Field("PixelsPerUnit", &SpriteRendererComponent::PixelsPerUnit).Range(1.0f, 4096.0f)
            .Field("ZOrder",        &SpriteRendererComponent::ZOrder).Tooltip("Sort order within the 2D pass")
            .Field("TexturePath",   &SpriteRendererComponent::TexturePath).AsAssetPath("texture")
            .Field("YSort",         &SpriteRendererComponent::YSort).Tooltip("Within a ZOrder, order by -Y (lower on screen draws in front) instead of Z")
            .Field("Enabled",       &SpriteRendererComponent::Enabled).HideInInspector().OmitIfTrue();

        // Tile-grid renderer (U4). Cells is NOT reflected — the SceneSerializer
        // writes it as a plain int array (custom block, diff-friendly).
        ClassIn<TilemapComponent>(r, "Tilemap", "Rendering")
            .Field("TilesetPath", &TilemapComponent::TilesetPath).AsAssetPath("texture")
            .Field("TileW",       &TilemapComponent::TileW).Range(1.0f, 1024.0f).Tooltip("Tile width in atlas texels")
            .Field("TileH",       &TilemapComponent::TileH).Range(1.0f, 1024.0f).Tooltip("Tile height in atlas texels")
            .Field("Columns",     &TilemapComponent::Columns).Range(0.0f, 1024.0f).Tooltip("Atlas columns; 0 = texture width / TileW")
            .Field("GridW",       &TilemapComponent::GridW).Range(1.0f, 1024.0f)
            .Field("GridH",       &TilemapComponent::GridH).Range(1.0f, 1024.0f)
            .Field("ZOrder",      &TilemapComponent::ZOrder).Tooltip("Sort order within the 2D pass");

        // 2D point light (X5). Additive radial light multiplied over the 2D output.
        ClassIn<Light2DComponent>(r, "Light2D", "Rendering")
            .Field("Color",     &Light2DComponent::Color).Color()
            .Field("Radius",    &Light2DComponent::Radius).Range(0.0f, 100.0f).Meters().Doc("World-unit reach of the light")
            .Field("Intensity", &Light2DComponent::Intensity).Range(0.0f, 20.0f).Doc("HDR brightness at the center")
            .Field("Falloff",   &Light2DComponent::Falloff).Range(0.1f, 8.0f).Doc("Radial falloff exponent (higher = tighter)")
            .Field("Enabled",   &Light2DComponent::Enabled).OmitIfTrue();

        // Flipbook sprite animation (U4). Elapsed is runtime-only (not reflected).
        ClassIn<SpriteAnimationComponent>(r, "SpriteAnimation", "Rendering")
            .Field("SheetPath", &SpriteAnimationComponent::SheetPath).AsAssetPath("texture")
            .Field("FrameW",    &SpriteAnimationComponent::FrameW).Range(1.0f, 4096.0f)
            .Field("FrameH",    &SpriteAnimationComponent::FrameH).Range(1.0f, 4096.0f)
            .Field("Frames",    &SpriteAnimationComponent::Frames).Range(1.0f, 4096.0f)
            .Field("Row",       &SpriteAnimationComponent::Row).Range(0.0f, 4096.0f)
            .Field("FPS",       &SpriteAnimationComponent::FPS).Range(0.0f, 120.0f)
            .Field("Playing",   &SpriteAnimationComponent::Playing)
            .Field("Loop",      &SpriteAnimationComponent::Loop);

        ClassIn<CameraComponent>(r, "Camera", "Rendering")
            .Field("Primary",   &CameraComponent::Primary)
            .Field("ProjectionType", &CameraComponent::ProjectionType)
                .EnumValue("Perspective", 0).EnumValue("Orthographic", 1)
            .Field("FovDeg",    &CameraComponent::FovDeg).Range(10.0f, 170.0f).Degrees()
            .Field("Near",      &CameraComponent::Near).Range(0.001f, 100.0f).Meters()
            .Field("Far",       &CameraComponent::Far).Range(1.0f, 100000.0f).Meters()
            .Field("OrthoSize", &CameraComponent::OrthoSize).Range(0.1f, 1000.0f);

        ClassIn<EnvironmentComponent>(r, "Environment", "Rendering")
            .Field("SunDirection", &EnvironmentComponent::SunDirection).Tooltip("Direction the sun light travels")
            .Field("SunColor",     &EnvironmentComponent::SunColor).Color()
            .Field("SunIntensity", &EnvironmentComponent::SunIntensity).Range(0.0f, 10.0f)
            .Field("Sky",          &EnvironmentComponent::Sky)
                .EnumValue("Procedural", 0).EnumValue("Detailed", 1).EnumValue("HDRI", 2).EnumValue("Physical", 3)
            .Field("HdriPath",     &EnvironmentComponent::HdriPath).AsAssetPath("hdri").Tooltip("Equirectangular .hdr; used when Sky = HDRI")
            .Field("Turbidity",     &EnvironmentComponent::Turbidity).Range(1.0f, 10.0f).Doc("Physical sky haze: scales Mie density (used when Sky = Physical)")
            .Field("RayleighScale", &EnvironmentComponent::RayleighScale).Range(0.0f, 4.0f).Doc("Physical sky: scales Rayleigh (blue) scattering")
            .Field("MieScale",      &EnvironmentComponent::MieScale).Range(0.0f, 4.0f).Doc("Physical sky: scales Mie (white haze / sun halo) scattering")
            .Field("MieG",          &EnvironmentComponent::MieG).Range(0.0f, 0.99f).Doc("Physical sky: Mie phase asymmetry (sun-halo tightness)")
            .Field("TimeOfDay",    &EnvironmentComponent::TimeOfDay).Range(0.0f, 24.0f).Doc("Hours (0-24); drives the procedural sun when scrubbed")
            .Field("Skybox",       &EnvironmentComponent::Skybox)
            .Field("IBL",          &EnvironmentComponent::IBL)
            .Field("IBLIntensity", &EnvironmentComponent::IBLIntensity).Range(0.0f, 4.0f)
            .Field("Exposure",     &EnvironmentComponent::Exposure).Range(0.0f, 8.0f)
            .Field("AmbientIntensity", &EnvironmentComponent::AmbientIntensity).Range(0.0f, 4.0f).Doc("Scales the ambient/IBL lighting term (1 = unchanged)")
            .Field("Gamma",            &EnvironmentComponent::Gamma).Range(1.0f, 3.0f).Doc("Tonemap output gamma (2.2 = the shipped sRGB curve)")
            .Field("SunAngularSize",   &EnvironmentComponent::SunAngularSize).Range(0.1f, 10.0f).Doc("Sun-disc diameter in degrees (Detailed/Physical sky). Real sun ~0.53")
            .Field("Ambient2D",        &EnvironmentComponent::Ambient2D).Color().Doc("2D lighting ambient (X5); white = no darkening (byte-identical)")
            .Field("Fog",              &EnvironmentComponent::Fog)
            .Field("FogColor",         &EnvironmentComponent::FogColor).Color()
            .Field("FogDensity",       &EnvironmentComponent::FogDensity).Range(0.0f, 1.0f)
            .Field("FogHeightFalloff", &EnvironmentComponent::FogHeightFalloff).Range(0.0f, 2.0f)
            .Field("FogBaseHeight",    &EnvironmentComponent::FogBaseHeight).Meters()
            .Field("Bloom",            &EnvironmentComponent::Bloom)
            .Field("BloomThreshold",   &EnvironmentComponent::BloomThreshold).Range(0.0f, 8.0f)
            .Field("BloomIntensity",   &EnvironmentComponent::BloomIntensity).Range(0.0f, 4.0f)
            .Field("SSAO",             &EnvironmentComponent::SSAO)
            .Field("SsaoRadius",       &EnvironmentComponent::SsaoRadius).Range(0.0f, 4.0f)
            .Field("FXAA",             &EnvironmentComponent::FXAA)
            .Field("LensFlare",          &EnvironmentComponent::LensFlare)
            .Field("LensFlareIntensity", &EnvironmentComponent::LensFlareIntensity).Range(0.0f, 2.0f)
            .Field("Vignette",         &EnvironmentComponent::Vignette).Doc("Post-tonemap edge darkening (Q5); off = byte-identical")
            .Field("VignetteAmount",   &EnvironmentComponent::VignetteAmount).Range(0.0f, 1.0f)
            .Field("VignetteRadius",   &EnvironmentComponent::VignetteRadius).Range(0.0f, 1.5f)
            .Field("VignetteFeather",  &EnvironmentComponent::VignetteFeather).Range(0.0f, 1.0f)
            .Field("VignetteColor",    &EnvironmentComponent::VignetteColor).Color();

        // Physics (J3) — rigid body + colliders + character. Reflected so the
        // Inspector groups them under "Physics", they serialize, and every field is
        // undoable for free (E7/E8). Runtime body ids are NOT here (Scene-owned).
        ClassIn<RigidBodyComponent>(r, "RigidBody", "Physics")
            .Field("Motion", &RigidBodyComponent::Motion)
                .EnumValue("Static", 0).EnumValue("Kinematic", 1).EnumValue("Dynamic", 2)
                .Tooltip("Static = world/ground; Kinematic = script-moved; Dynamic = simulated")
            .Field("Mass",           &RigidBodyComponent::Mass).Range(0.001f, 10000.0f).Tooltip("kg (dynamic only)")
            .Field("Friction",       &RigidBodyComponent::Friction).Range(0.0f, 2.0f)
            .Field("Restitution",    &RigidBodyComponent::Restitution).Range(0.0f, 1.0f).Tooltip("0 = no bounce, 1 = elastic")
            .Field("LinearDamping",  &RigidBodyComponent::LinearDamping).Range(0.0f, 10.0f)
            .Field("AngularDamping", &RigidBodyComponent::AngularDamping).Range(0.0f, 10.0f)
            .Field("GravityFactor",  &RigidBodyComponent::GravityFactor).Range(0.0f, 4.0f).Tooltip("0 = floats, 1 = normal gravity")
            .Field("CCD",            &RigidBodyComponent::CCD).Tooltip("Continuous collision for fast small bodies")
            .Field("StartAsleep",    &RigidBodyComponent::StartAsleep)
            .Field("CollisionCategory", &RigidBodyComponent::CollisionCategory).Tooltip("16-bit category bits this body belongs to")
            .Field("CollidesWith",      &RigidBodyComponent::CollidesWith).Tooltip("16-bit mask of categories this body collides with");

        ClassIn<BoxColliderComponent>(r, "BoxCollider", "Physics")
            .Field("HalfExtents", &BoxColliderComponent::HalfExtents).Tooltip("Half-size per axis (pre-scale)")
            .Field("Offset",      &BoxColliderComponent::Offset)
            .Field("IsTrigger",   &BoxColliderComponent::IsTrigger).Tooltip("Sensor: overlap events, no contact response")
            .Field("Enabled",     &BoxColliderComponent::Enabled).HideInInspector().OmitIfTrue();

        ClassIn<SphereColliderComponent>(r, "SphereCollider", "Physics")
            .Field("Radius",    &SphereColliderComponent::Radius).Range(0.001f, 1000.0f).Meters()
            .Field("Offset",    &SphereColliderComponent::Offset)
            .Field("IsTrigger", &SphereColliderComponent::IsTrigger)
            .Field("Enabled",   &SphereColliderComponent::Enabled).HideInInspector().OmitIfTrue();

        ClassIn<CapsuleColliderComponent>(r, "CapsuleCollider", "Physics")
            .Field("Radius",     &CapsuleColliderComponent::Radius).Range(0.001f, 1000.0f).Meters()
            .Field("HalfHeight", &CapsuleColliderComponent::HalfHeight).Range(0.0f, 1000.0f).Tooltip("Half the cylinder part (excludes the caps)").Meters()
            .Field("Offset",     &CapsuleColliderComponent::Offset)
            .Field("IsTrigger",  &CapsuleColliderComponent::IsTrigger)
            .Field("Enabled",    &CapsuleColliderComponent::Enabled).HideInInspector().OmitIfTrue();

        ClassIn<CharacterControllerComponent>(r, "CharacterController", "Physics")
            .Field("Height",      &CharacterControllerComponent::Height).Range(0.2f, 10.0f).Meters()
            .Field("Radius",      &CharacterControllerComponent::Radius).Range(0.05f, 5.0f).Meters()
            .Field("MaxSlopeDeg", &CharacterControllerComponent::MaxSlopeDeg).Range(0.0f, 89.0f).Degrees()
            .Field("StepHeight",  &CharacterControllerComponent::StepHeight).Range(0.0f, 2.0f).Meters()
            .Field("Mass",        &CharacterControllerComponent::Mass).Range(1.0f, 1000.0f);

        // Scripting link (E11). ClassName is the only plain reflected field; the
        // dynamic script-field overrides (NativeScriptComponent::Fields) are handled
        // out-of-band by the serializer + the Inspector's script section, since they
        // depend on the ModuleRegistry's per-script descriptor.
        ClassIn<NativeScriptComponent>(r, "NativeScript", "Scripts")
            .Field("ClassName", &NativeScriptComponent::ClassName)
                .Tooltip("Registered C++ script class (ModuleRegistry).");

        // SystemScript link (H9) — logic over a CLASS of entities. Like NativeScript,
        // ClassName is the only plain field; the reflected overrides are handled
        // out-of-band by the serializer via the SystemDescriptor.
        ClassIn<SystemScriptComponent>(r, "SystemScript", "Systems")
            .Field("ClassName", &SystemScriptComponent::ClassName)
                .Tooltip("Registered C++ SystemScript class (ModuleRegistry).");

        // Prefab link (E14) — remembers the source .cprefab of an instantiated subtree.
        ClassIn<PrefabComponent>(r, "Prefab", "Core")
            .Field("SourcePath", &PrefabComponent::SourcePath).AsAssetPath("prefab");

        // In-game UI (U1) — canvas + rect-transform + image/text/button. Engine-
        // generic; the Inspector groups them under "UI", they serialize + undo for
        // free. Runtime-only fields (Resolved*, State/Armed) are unregistered.
        ClassIn<CanvasComponent>(r, "Canvas", "UI")
            .Field("ScaleMode", &CanvasComponent::ScaleMode)
                .EnumValue("ConstantPixel", 0).EnumValue("ScaleWithHeight", 1)
                .Tooltip("ScaleWithHeight multiplies offsets by viewportH/ReferenceHeight")
            .Field("ReferenceHeight", &CanvasComponent::ReferenceHeight).Range(1.0f, 8192.0f)
            .Field("SortOrder",       &CanvasComponent::SortOrder).Tooltip("Lower draws first (further back)");

        ClassIn<RectTransformComponent>(r, "RectTransform", "UI")
            .Field("AnchorMin", &RectTransformComponent::AnchorMin).Tooltip("0..1 of parent rect (top-left)")
            .Field("AnchorMax", &RectTransformComponent::AnchorMax).Tooltip("0..1 of parent rect (bottom-right)")
            .Field("OffsetMin", &RectTransformComponent::OffsetMin).Tooltip("Pixels from the min anchor point")
            .Field("OffsetMax", &RectTransformComponent::OffsetMax).Tooltip("Pixels from the max anchor point")
            .Field("Pivot",     &RectTransformComponent::Pivot)
            .Field("ZOrder",    &RectTransformComponent::ZOrder).Tooltip("Draw + hit order within a canvas");

        ClassIn<UiImageComponent>(r, "UiImage", "UI")
            .Field("TexturePath",    &UiImageComponent::TexturePath).AsAssetPath("texture").Tooltip("Empty = solid Tint quad")
            .Field("Tint",           &UiImageComponent::Tint).Color()
            .Field("NineSlice",      &UiImageComponent::NineSlice).Tooltip("l, t, r, b border in texels (0 = plain stretch)")
            .Field("PreserveAspect", &UiImageComponent::PreserveAspect);

        ClassIn<UiTextComponent>(r, "UiText", "UI")
            .Field("Text",     &UiTextComponent::Text)
            .Field("FontPath", &UiTextComponent::FontPath).Tooltip("Font stem or VFS path; empty = default face")
            .Field("SizePx",   &UiTextComponent::SizePx).Range(1.0f, 512.0f)
            .Field("Color",    &UiTextComponent::Color).Color()
            .Field("HAlign",   &UiTextComponent::HAlign)
                .EnumValue("Left", 0).EnumValue("Center", 1).EnumValue("Right", 2)
            .Field("VAlign",   &UiTextComponent::VAlign)
                .EnumValue("Top", 0).EnumValue("Middle", 1).EnumValue("Bottom", 2)
            .Field("Wrap",     &UiTextComponent::Wrap).Tooltip("Word-wrap is a v2 follow-up; honors explicit newlines");

        ClassIn<UiButtonComponent>(r, "UiButton", "UI")
            .Field("Signal",       &UiButtonComponent::Signal).Tooltip("Emitted on the scene EventBus on release-inside")
            .Field("NormalTint",   &UiButtonComponent::NormalTint).Color()
            .Field("HoverTint",    &UiButtonComponent::HoverTint).Color()
            .Field("PressedTint",  &UiButtonComponent::PressedTint).Color()
            .Field("DisabledTint", &UiButtonComponent::DisabledTint).Color()
            .Field("Interactable", &UiButtonComponent::Interactable);

        // World-anchored UI (X6): pin an element to a world position (nameplates).
        ClassIn<UiWorldAnchorComponent>(r, "UiWorldAnchor", "UI")
            .Field("TargetEntity",      &UiWorldAnchorComponent::TargetEntity).Tooltip("World entity to track (empty = absolute point)")
            .Field("WorldOffset",       &UiWorldAnchorComponent::WorldOffset).Doc("Added to the target's world position")
            .Field("ScreenOffset",      &UiWorldAnchorComponent::ScreenOffset).Doc("Canvas-pixel nudge after projection")
            .Field("HideWhenOffscreen", &UiWorldAnchorComponent::HideWhenOffscreen);

        // PBR material asset (E17) — the reflected `.cmat` struct (not an entity
        // component; registered so the Material Editor UI + serialization are generic).
        ClassIn<MaterialAsset>(r, "Material", "Material")
            .Field("Albedo",        &MaterialAsset::Albedo).Color()
            .Field("Metallic",      &MaterialAsset::Metallic).Range(0.0f, 1.0f)
            .Field("Roughness",     &MaterialAsset::Roughness).Range(0.0f, 1.0f)
            .Field("AO",            &MaterialAsset::AO).Range(0.0f, 1.0f)
            .Field("Emissive",      &MaterialAsset::Emissive).Tooltip("Emissive radiance (can exceed 1 for HDR)")
            .Field("Transparent",   &MaterialAsset::Transparent)
            .Field("AlbedoMap",     &MaterialAsset::AlbedoMap).AsAssetPath("texture")
            .Field("NormalMap",     &MaterialAsset::NormalMap).AsAssetPath("texture")
            .Field("MetalRoughMap", &MaterialAsset::MetalRoughMap).AsAssetPath("texture").Tooltip("glTF pack: roughness=G, metallic=B")
            .Field("AOMap",         &MaterialAsset::AOMap).AsAssetPath("texture")
            .Field("EmissiveMap",   &MaterialAsset::EmissiveMap).AsAssetPath("texture");

        // ...and the 3D half (W4). Terrain/water/particles/voxels, meshes and LODs,
        // skeletal animation and sockets, the 3D lights, the two geometry-derived
        // colliders and navigation all live in reflect/TypeRegistry3D.cpp, which the
        // 2D configuration does not compile. Splitting the call changes no output:
        // the registry is keyed by entt type hash, so a descriptor is identical
        // wherever it was registered from, and the only two consumers of iteration
        // order (the Inspector's Add menu, the SceneSerializer's component blocks)
        // both re-sort by name.
#ifndef COSMIC_2D_ONLY
        RegisterEngine3DTypes(r);
#endif
    }
}
