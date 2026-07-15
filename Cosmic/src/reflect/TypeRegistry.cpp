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

        // MeshRenderer v1 reflects the scalar fields only. The Ref<Mesh>/
        // Ref<Material> members surface as AssetPath fields once E16 gives
        // assets stable project:// paths (E1 gotcha).
        ClassIn<MeshRendererComponent>(r, "MeshRenderer", "Rendering")
            .Field("Color",       &MeshRendererComponent::Color).Color()
            .Field("CastShadows", &MeshRendererComponent::CastShadows)
            // Imported/loaded mesh slot (E16) — resolved through AssetLibrary::GetMesh.
            .Field("MeshPath",    &MeshRendererComponent::MeshPath).AsAssetPath("mesh")
            // Material slot (E17) — resolved through AssetLibrary::GetMaterial.
            .Field("MaterialPath", &MeshRendererComponent::MaterialPath).AsAssetPath("material")
            // T12 enable gate — header checkbox, not a field row (omit-if-true keeps scenes compat).
            .Field("Enabled",     &MeshRendererComponent::Enabled).HideInInspector().OmitIfTrue();

        // Skeletal-animation driver (Phase 20 / A2). Only the authored fields are
        // reflected — the resolved clip/skeleton refs and the per-frame palette
        // are runtime-only. NormalizedTime is the scrubbable play head.
        ClassIn<AnimatorComponent>(r, "Animator", "Rendering")
            .Field("ClipPath",       &AnimatorComponent::ClipPath).AsAssetPath("animation")
            .Field("Speed",          &AnimatorComponent::Speed).Range(-4.0f, 4.0f).Tooltip("Playback rate (negative = reverse)")
            .Field("Loop",           &AnimatorComponent::Loop)
            .Field("Playing",        &AnimatorComponent::Playing)
            .Field("NormalizedTime", &AnimatorComponent::NormalizedTime).Range(0.0f, 1.0f).Tooltip("Play head — scrub while paused");

        // Joint socket (M4) — attach this entity to a named joint of an animated
        // ancestor. All fields are ordinary reflected kinds, so the serializer +
        // Inspector auto-UI cover it with no special-casing (undo via CommitFieldEdit).
        ClassIn<SocketComponent>(r, "Socket", "Rendering")
            .Field("Joint",    &SocketComponent::Joint).Tooltip("Target joint name on an animated ancestor (e.g. hand.r)")
            .Field("Position", &SocketComponent::Position).Meters()
            .Field("Rotation", &SocketComponent::Rotation)
            .Field("Scale",    &SocketComponent::Scale);

        // Parametric primitive (E15). Only the shape + params are reflected; the
        // built-mesh signature (BuiltSignature) is runtime-only and left out, so a
        // scene serializes as tiny shape/param text and the mesh is rebuilt on load.
        ClassIn<PrimitiveMeshComponent>(r, "PrimitiveMesh", "Rendering")
            .Field("ShapeType", &PrimitiveMeshComponent::ShapeType)
                .EnumValue("Box", 0).EnumValue("Sphere", 1).EnumValue("Plane", 2)
                .EnumValue("Cylinder", 3).EnumValue("Cone", 4).EnumValue("Torus", 5)
            .Field("Size",       &PrimitiveMeshComponent::Size).Tooltip("Box: full extents. Plane: X=width, Z=depth.")
            .Field("Radius",     &PrimitiveMeshComponent::Radius).Range(0.01f, 100.0f)
            .Field("Height",     &PrimitiveMeshComponent::Height).Range(0.01f, 100.0f)
            .Field("TubeRadius", &PrimitiveMeshComponent::TubeRadius).Range(0.01f, 50.0f).Tooltip("Torus tube radius")
            .Field("Segments",   &PrimitiveMeshComponent::Segments).Range(3.0f, 256.0f).Tooltip("Radial / longitude subdivisions")
            .Field("Rings",      &PrimitiveMeshComponent::Rings).Range(3.0f, 256.0f).Tooltip("Sphere latitude bands / Torus tube sides");

        ClassIn<LODGroupComponent>(r, "LODGroup", "Rendering")
            .Field("Color",       &LODGroupComponent::Color).Color()
            .Field("CastShadows", &LODGroupComponent::CastShadows);

        ClassIn<DirectionalLightComponent>(r, "DirectionalLight", "Lighting")
            .Field("Direction", &DirectionalLightComponent::Direction).Tooltip("Direction the light travels")
            .Field("Color",     &DirectionalLightComponent::Color).Color()
            .Field("Intensity", &DirectionalLightComponent::Intensity).Range(0.0f, 10.0f)
            .Field("Enabled",   &DirectionalLightComponent::Enabled).HideInInspector().OmitIfTrue();

        ClassIn<PointLightComponent>(r, "PointLight", "Lighting")
            .Field("Color",     &PointLightComponent::Color).Color()
            .Field("Intensity", &PointLightComponent::Intensity).Range(0.0f, 20.0f)
            .Field("Radius",    &PointLightComponent::Radius).Range(0.0f, 100.0f).Meters()
            .Field("Enabled",   &PointLightComponent::Enabled).HideInInspector().OmitIfTrue();

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

        // World-system authoring recipes (E18). Each holds a Ref<> asset (runtime,
        // not reflected) plus a reflected recipe that Scene::SyncWorldSystems maps
        // onto the engine spec structs. UseRecipe is HideInInspector: it serializes
        // (so a loaded scene rebuilds) but is set by the WorldSystems panel, not a
        // raw checkbox. BuiltSignature + the Ref are unregistered -> runtime-only.
        ClassIn<TerrainComponent>(r, "Terrain", "World")
            .Field("UseRecipe",   &TerrainComponent::UseRecipe).HideInInspector()
            .Field("WorldSize",   &TerrainComponent::WorldSize).Range(16.0f, 8192.0f).Meters()
            .Field("Resolution",  &TerrainComponent::Resolution).Range(65.0f, 1025.0f).Tooltip("Snapped to 32*2^k + 1 (65..1025) at build")
            .Field("HeightScale", &TerrainComponent::HeightScale).Range(0.0f, 1000.0f).Meters()
            .Field("BaseHeight",  &TerrainComponent::BaseHeight).Meters()
            .Field("Seed",        &TerrainComponent::Seed)
            .Field("Octaves",     &TerrainComponent::Octaves).Range(1.0f, 12.0f)
            .Field("Frequency",   &TerrainComponent::Frequency).Range(0.1f, 32.0f).Tooltip("fBm periods across the terrain")
            .Field("Lacunarity",  &TerrainComponent::Lacunarity).Range(1.0f, 4.0f)
            .Field("Gain",        &TerrainComponent::Gain).Range(0.0f, 1.0f)
            .Field("EdgeFalloff", &TerrainComponent::EdgeFalloff).Range(0.0f, 1.0f).Tooltip("0 = none; else island edge fade")
            .Field("HeightmapPath", &TerrainComponent::HeightmapPath).AsAssetPath("texture").Tooltip("Grayscale heightmap; empty = procedural fBm")
            .Field("GrassColor",  &TerrainComponent::GrassColor)
            .Field("RockColor",   &TerrainComponent::RockColor)
            .Field("SnowColor",   &TerrainComponent::SnowColor)
            .Field("SandColor",   &TerrainComponent::SandColor)
            .Field("GrassTex",    &TerrainComponent::GrassTex).AsAssetPath("texture")
            .Field("RockTex",     &TerrainComponent::RockTex).AsAssetPath("texture")
            .Field("SnowTex",     &TerrainComponent::SnowTex).AsAssetPath("texture")
            .Field("SandTex",     &TerrainComponent::SandTex).AsAssetPath("texture")
            .Field("SnowHeight",  &TerrainComponent::SnowHeight).Tooltip("World Y where the snow layer fades in").Meters()
            .Field("SnowBlend",   &TerrainComponent::SnowBlend).Range(0.01f, 50.0f).Meters();

        ClassIn<WaterComponent>(r, "Water", "World")
            .Field("UseRecipe", &WaterComponent::UseRecipe).HideInInspector()
            .Field("Preset",    &WaterComponent::Preset)
                .EnumValue("Lake", 0).EnumValue("Ocean", 1).EnumValue("Storm", 2)
            .Field("Center",        &WaterComponent::Center)
            .Field("Extent",        &WaterComponent::Extent)
            .Field("SurfaceHeight", &WaterComponent::SurfaceHeight).Meters()
            .Field("GridResolution", &WaterComponent::GridResolution).Range(2.0f, 513.0f)
            .Field("Amplitude",     &WaterComponent::Amplitude).Range(0.0f, 4.0f).Tooltip("Scales the preset wave heights")
            .Field("Choppiness",    &WaterComponent::Choppiness).Range(0.0f, 2.0f).Tooltip("Scales the preset wave steepness")
            .Field("ShallowColor",  &WaterComponent::ShallowColor)
            .Field("DeepColor",     &WaterComponent::DeepColor)
            .Field("CausticStrength",  &WaterComponent::CausticStrength).Range(0.0f, 2.0f)
            .Field("WhitecapStrength", &WaterComponent::WhitecapStrength).Range(0.0f, 2.0f)
            .Field("SparkleStrength",  &WaterComponent::SparkleStrength).Range(0.0f, 2.0f)
            .Field("Enabled",          &WaterComponent::Enabled).HideInInspector().OmitIfTrue();

        ClassIn<ParticleEmitterComponent>(r, "ParticleEmitter", "World")
            .Field("UseRecipe",    &ParticleEmitterComponent::UseRecipe).HideInInspector()
            .Field("MaxParticles", &ParticleEmitterComponent::MaxParticles).Range(1.0f, 65536.0f)
            .Field("SpawnRate",    &ParticleEmitterComponent::SpawnRate).Range(0.0f, 5000.0f)
            .Field("Shape",        &ParticleEmitterComponent::Shape)
                .EnumValue("Point", 0).EnumValue("Sphere", 1).EnumValue("Cone", 2).EnumValue("Box", 3)
            .Field("ShapeRadius",  &ParticleEmitterComponent::ShapeRadius).Range(0.0f, 50.0f).Meters()
            .Field("ConeAngleDeg", &ParticleEmitterComponent::ConeAngleDeg).Range(0.0f, 180.0f).Degrees()
            .Field("BoxExtents",   &ParticleEmitterComponent::BoxExtents)
            .Field("SpeedMin",     &ParticleEmitterComponent::SpeedMin).Range(0.0f, 100.0f)
            .Field("SpeedMax",     &ParticleEmitterComponent::SpeedMax).Range(0.0f, 100.0f)
            .Field("LifeMin",      &ParticleEmitterComponent::LifeMin).Range(0.01f, 60.0f).Seconds()
            .Field("LifeMax",      &ParticleEmitterComponent::LifeMax).Range(0.01f, 60.0f).Seconds()
            .Field("Gravity",      &ParticleEmitterComponent::Gravity)
            .Field("Drag",         &ParticleEmitterComponent::Drag).Range(0.0f, 10.0f)
            .Field("Wind",         &ParticleEmitterComponent::Wind)
            .Field("SizeStart",    &ParticleEmitterComponent::SizeStart).Range(0.0f, 50.0f)
            .Field("SizeEnd",      &ParticleEmitterComponent::SizeEnd).Range(0.0f, 50.0f)
            .Field("ColorStart",   &ParticleEmitterComponent::ColorStart).Color()
            .Field("ColorEnd",     &ParticleEmitterComponent::ColorEnd).Color()
            .Field("Blend",        &ParticleEmitterComponent::Blend)
                .EnumValue("Alpha", 0).EnumValue("Additive", 1)
            .Field("Space",        &ParticleEmitterComponent::Space)
                .EnumValue("World", 0).EnumValue("Local", 1)
            .Field("TexturePath",  &ParticleEmitterComponent::TexturePath).AsAssetPath("texture").Tooltip("Flipbook/sprite sheet; empty = procedural puff")
            .Field("FlipbookTilesX", &ParticleEmitterComponent::FlipbookTilesX).Range(1.0f, 16.0f)
            .Field("FlipbookTilesY", &ParticleEmitterComponent::FlipbookTilesY).Range(1.0f, 16.0f)
            .Field("FlipbookFps",    &ParticleEmitterComponent::FlipbookFps).Range(0.0f, 60.0f)
            .Field("FlipbookBlend",  &ParticleEmitterComponent::FlipbookBlend)
            .Field("SoftFadeDistance",  &ParticleEmitterComponent::SoftFadeDistance).Range(0.0f, 10.0f).Meters()
            .Field("StretchByVelocity", &ParticleEmitterComponent::StretchByVelocity).Range(0.0f, 1.0f)
            .Field("NoiseEnabled",      &ParticleEmitterComponent::NoiseEnabled).Doc("Curl-noise turbulence (X3); off = byte-identical")
            .Field("NoiseStrength",     &ParticleEmitterComponent::NoiseStrength).Range(0.0f, 20.0f).Doc("Curl-noise acceleration scale")
            .Field("NoiseFrequency",    &ParticleEmitterComponent::NoiseFrequency).Range(0.01f, 4.0f).Doc("Curl-noise spatial frequency")
            .Field("NoiseOctaves",      &ParticleEmitterComponent::NoiseOctaves).Range(1.0f, 4.0f).Doc("Curl-noise fBm octaves (1..4)")
            .Field("BoundsExtents",     &ParticleEmitterComponent::BoundsExtents).Doc("Local-space clamp half-extents (X4); all-0 = off")
            .Field("BoundsWrap",        &ParticleEmitterComponent::BoundsWrap).Doc("Past the bounds: false = kill, true = wrap")
            .Field("Enabled",           &ParticleEmitterComponent::Enabled).HideInInspector().OmitIfTrue();

        // Voxel volume (Phase 18 / V1–V6). Runtime Volume/Palette/Render are
        // unregistered (runtime-only); the reflected fields are the authoring recipe
        // (palette + .cvox path + placement + flattened generation params).
        ClassIn<VoxelVolumeComponent>(r, "VoxelVolume", "World")
            .Field("PalettePath",  &VoxelVolumeComponent::PalettePath).AsAssetPath("voxel_palette").Tooltip("`.cpal` block table; empty = default palette")
            .Field("VolumePath",   &VoxelVolumeComponent::VolumePath).AsAssetPath("voxel_volume").Tooltip("`.cvox` sidecar; empty = empty/generated")
            .Field("VoxelSize",    &VoxelVolumeComponent::VoxelSize).Range(0.05f, 16.0f).Tooltip("Metres per voxel").Meters()
            .Field("ViewRadius",   &VoxelVolumeComponent::ViewRadius).Range(1.0f, 64.0f).Tooltip("Chunk radius streamed around the camera")
            .Field("Greedy",       &VoxelVolumeComponent::Greedy).Tooltip("Greedy-merged (fast) vs culled (per-face) render mesh")
            .Field("GenEnabled",   &VoxelVolumeComponent::GenEnabled).Tooltip("Procedurally stream-generate chunks in view")
            .Field("Seed",         &VoxelVolumeComponent::Seed)
            .Field("SurfaceLevel", &VoxelVolumeComponent::SurfaceLevel).Tooltip("Average ground height, in voxels (world Y)")
            .Field("Amplitude",    &VoxelVolumeComponent::Amplitude).Range(0.0f, 512.0f)
            .Field("Frequency",    &VoxelVolumeComponent::Frequency).Range(0.0001f, 1.0f).Tooltip("Noise frequency, per voxel")
            .Field("Octaves",      &VoxelVolumeComponent::Octaves).Range(1.0f, 10.0f)
            .Field("Lacunarity",   &VoxelVolumeComponent::Lacunarity).Range(1.0f, 4.0f)
            .Field("Gain",         &VoxelVolumeComponent::Gain).Range(0.0f, 1.0f)
            .Field("Ridged",       &VoxelVolumeComponent::Ridged).Tooltip("Ridged multifractal (mountains) vs fBm hills")
            .Field("CaveThreshold",&VoxelVolumeComponent::CaveThreshold).Range(0.0f, 1.0f).Tooltip("0 = no caves")
            .Field("CaveFrequency",&VoxelVolumeComponent::CaveFrequency).Range(0.001f, 1.0f)
            .Field("DirtDepth",    &VoxelVolumeComponent::DirtDepth).Range(0.0f, 32.0f)
            .Field("SandLevel",    &VoxelVolumeComponent::SandLevel).Tooltip("Surface at/below this height (voxels) is sand")
            .Field("GrassBlock",   &VoxelVolumeComponent::GrassBlock)
            .Field("DirtBlock",    &VoxelVolumeComponent::DirtBlock)
            .Field("StoneBlock",   &VoxelVolumeComponent::StoneBlock)
            .Field("SandBlock",    &VoxelVolumeComponent::SandBlock);

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

        ClassIn<MeshColliderComponent>(r, "MeshCollider", "Physics")
            .Field("Convex",    &MeshColliderComponent::Convex).Tooltip("On = convex hull (dynamic-capable); Off = triangle mesh (static/kinematic only)")
            .Field("IsTrigger", &MeshColliderComponent::IsTrigger)
            .Field("Enabled",   &MeshColliderComponent::Enabled).HideInInspector().OmitIfTrue();

        // TerrainCollider has no reflected fields (it derives everything from the
        // sibling TerrainComponent); registered so it serializes + shows in the Add
        // menu / Inspector as a "Physics" tag.
        ClassIn<TerrainColliderComponent>(r, "TerrainCollider", "Physics");

        ClassIn<CharacterControllerComponent>(r, "CharacterController", "Physics")
            .Field("Height",      &CharacterControllerComponent::Height).Range(0.2f, 10.0f).Meters()
            .Field("Radius",      &CharacterControllerComponent::Radius).Range(0.05f, 5.0f).Meters()
            .Field("MaxSlopeDeg", &CharacterControllerComponent::MaxSlopeDeg).Range(0.0f, 89.0f).Degrees()
            .Field("StepHeight",  &CharacterControllerComponent::StepHeight).Range(0.0f, 2.0f).Meters()
            .Field("Mass",        &CharacterControllerComponent::Mass).Range(1.0f, 1000.0f);

        // Navigation (N2) — the Recast bake recipe. Runtime Nav/BuiltSignature are
        // unregistered (runtime-only); the built navmesh rides a `.cnav` sidecar, not
        // the scene JSON. Reflected so the Inspector auto-UIs it + every field undoes
        // for free; the N3 "Regenerate now" button is added out-of-band (per-component).
        ClassIn<NavMeshComponent>(r, "NavMesh", "Navigation")
            .Field("SidecarPath",  &NavMeshComponent::SidecarPath).AsAssetPath("navmesh").Tooltip("`.cnav` sidecar; empty = derived beside the scene")
            .Field("SourceMode",   &NavMeshComponent::SourceMode)
                .EnumValue("From children", 0).EnumValue("Whole scene", 1)
                .Tooltip("From children = bake only this entity's descendants; Whole scene = every collidable entity")
            .Field("AutoGenerate", &NavMeshComponent::AutoGenerate).Tooltip("Rebake automatically when the recipe or source geometry changes")
            .Field("AlwaysRenderHelper", &NavMeshComponent::AlwaysRenderHelper).Tooltip("Draw the translucent nav overlay even when this entity isn't selected")
            .Field("CellSize",     &NavMeshComponent::CellSize).Range(0.05f, 4.0f).Tooltip("XZ rasterization voxel size").Meters()
            .Field("CellHeight",   &NavMeshComponent::CellHeight).Range(0.05f, 4.0f).Tooltip("Y rasterization voxel size").Meters()
            .Field("AgentRadius",  &NavMeshComponent::AgentRadius).Range(0.0f, 10.0f).Tooltip("Walkable surface is eroded by the agent radius").Meters()
            .Field("AgentHeight",  &NavMeshComponent::AgentHeight).Range(0.1f, 20.0f).Tooltip("Vertical clearance an agent needs").Meters()
            .Field("AgentMaxClimb",&NavMeshComponent::AgentMaxClimb).Range(0.0f, 10.0f).Tooltip("Max height an agent auto-steps up (stairs/ledges)").Meters()
            .Field("AgentMaxSlope",&NavMeshComponent::AgentMaxSlope).Range(0.0f, 89.0f).Tooltip("Max walkable slope").Degrees()
            .Field("RegionMinSize",   &NavMeshComponent::RegionMinSize).Range(0.0f, 150.0f).Tooltip("Discard walkable regions smaller than this (voxels)")
            .Field("RegionMergeSize", &NavMeshComponent::RegionMergeSize).Range(0.0f, 150.0f).Tooltip("Merge regions smaller than this into neighbors (voxels)")
            .Field("EdgeMaxLen",   &NavMeshComponent::EdgeMaxLen).Range(0.0f, 50.0f).Tooltip("Max contour edge length").Meters()
            .Field("EdgeMaxError", &NavMeshComponent::EdgeMaxError).Range(0.1f, 3.0f).Tooltip("Contour simplification error (voxels)")
            .Field("DetailSampleDist",     &NavMeshComponent::DetailSampleDist).Range(0.0f, 16.0f).Tooltip("Detail-mesh sample spacing (× CellSize)")
            .Field("DetailSampleMaxError", &NavMeshComponent::DetailSampleMaxError).Range(0.0f, 16.0f).Tooltip("Detail-mesh max error (× CellHeight)")
            .Field("VertsPerPoly", &NavMeshComponent::VertsPerPoly).Range(3.0f, 6.0f).Tooltip("Max vertices per navmesh polygon")
            .Field("TileSize",     &NavMeshComponent::TileSize).Range(0.0f, 256.0f).Tooltip("Tiled build hint (voxels); 0 = single-tile solo build (v1)");

        // Nav agent (N4) — DetourCrowd tuning. Steered over the baked navmesh during
        // Play; scripts drive it via Nav().SetTarget. Reflected/serialized/undoable.
        ClassIn<NavAgentComponent>(r, "NavAgent", "Navigation")
            .Field("Radius",   &NavAgentComponent::Radius).Range(0.05f, 5.0f).Tooltip("Agent footprint radius").Meters()
            .Field("Height",   &NavAgentComponent::Height).Range(0.1f, 20.0f).Tooltip("Agent height").Meters()
            .Field("MaxSpeed", &NavAgentComponent::MaxSpeed).Range(0.0f, 50.0f).Tooltip("Max movement speed (m/s)")
            .Field("MaxAccel", &NavAgentComponent::MaxAccel).Range(0.0f, 100.0f).Tooltip("Max acceleration (m/s^2)")
            .Field("StoppingDistance", &NavAgentComponent::StoppingDistance).Range(0.0f, 10.0f).Tooltip("Arrival tolerance; emits nav.arrived within this of the target").Meters()
            .Field("AutoRepath", &NavAgentComponent::AutoRepath).Tooltip("Re-plan the path when it is invalidated (crowd default)");

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
    }
}
