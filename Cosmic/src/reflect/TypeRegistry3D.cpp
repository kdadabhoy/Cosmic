// reflect/TypeRegistry3D.cpp — the 3D half of the built-in engine component
// registration (Phase 29 W4).
//
// RegisterEngineTypes (TypeRegistry.cpp) registers the 19 components every engine
// configuration has; this file registers the 15 that only mean anything in a 3D
// world, and RegisterEngineTypes calls it behind the same #ifndef COSMIC_2D_ONLY
// fence Cosmic/CMakeLists.txt uses to drop this whole translation unit from a 2D
// build. The internal fence is belt-and-braces: it keeps the TU legal (and empty)
// if a build ever hands it to the 2D configuration anyway.
//
// Nothing here changed in the split beyond its address — names, categories, field
// order and every hint are the pre-split text verbatim, so saved scenes and the
// Inspector are unaffected. test_components3d_registry pins exactly that.
//
// Idempotent the same way RegisterEngineTypes is: re-registering overwrites the
// same descriptors. Note it APPENDS fields rather than replacing them (recorded by
// test_components3d_registry), so this is called once per registry — from
// RegisterEngineTypes, never on its own.

#ifndef COSMIC_2D_ONLY

#include "reflect/TypeRegistry.h"
#include "scene/Components3D.h"

namespace Cosmic::Reflect
{
    void RegisterEngine3DTypes(TypeRegistry& r)
    {
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

        ClassIn<MeshColliderComponent>(r, "MeshCollider", "Physics")
            .Field("Convex",    &MeshColliderComponent::Convex).Tooltip("On = convex hull (dynamic-capable); Off = triangle mesh (static/kinematic only)")
            .Field("IsTrigger", &MeshColliderComponent::IsTrigger)
            .Field("Enabled",   &MeshColliderComponent::Enabled).HideInInspector().OmitIfTrue();

        // TerrainCollider has no reflected fields (it derives everything from the
        // sibling TerrainComponent); registered so it serializes + shows in the Add
        // menu / Inspector as a "Physics" tag.
        ClassIn<TerrainColliderComponent>(r, "TerrainCollider", "Physics");

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
    }
}

#endif // !COSMIC_2D_ONLY
