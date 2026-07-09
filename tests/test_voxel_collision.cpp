// test_voxel_collision.cpp — Phase 18 / V5: per-chunk static Jolt MeshShape from
// the mesher's collision variant. Headless (PhysicsWorld is GL-free): a voxel
// floor slab is authored on a VoxelVolumeComponent, a play session builds the
// chunk bodies, and the surface is probed by ray + a dropped box comes to rest.
// World-coordinate tolerances are ABSOLUTE (doctest Approx.epsilon is relative).

#include "doctest.h"

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "physics/PhysicsWorld.h"
#include "physics/ScenePhysics.h"
#include "voxel/VoxelVolume.h"
#include "voxel/BlockPalette.h"
#include "voxel/VoxelRender.h"       // VoxelRenderData (CollisionDirty)

#include <cmath>

using namespace Cosmic;

TEST_SUITE("Physics / voxel collision (V5)")
{
    static void MakeFloor(VoxelVolumeComponent& vc)
    {
        vc.Volume  = VoxelVolume::Create();
        vc.Palette = BlockPalette::CreateDefault();
        vc.Volume->SetVoxelSize(1.0f);
        vc.Volume->SetOrigin({ 0.0f, 0.0f, 0.0f });
        // A 16x16 slab, 4 voxels thick: top surface at world Y = 4.
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x)
                for (int y = 0; y < 4; ++y)
                    vc.Volume->Set(x, y, z, 3);   // stone
    }

    TEST_CASE("A ray hits the voxel floor's top face")
    {
        Scene scene;
        Entity e = scene.CreateEntity("Voxels");
        MakeFloor(e.AddComponent<VoxelVolumeComponent>());

        PhysicsWorld world;
        PhysicsSettings ps; ps.ThreadCount = 0;
        world.Init(ps);
        scene.OnPhysicsStart(world);   // builds the per-chunk static mesh bodies

        auto hit = world.RayCast({ 8.0f, 20.0f, 8.0f }, { 0.0f, -1.0f, 0.0f }, 100.0f);
        REQUIRE(hit.has_value());
        CHECK(std::fabs(hit->Point.y - 4.0f) < 0.05f);

        scene.OnPhysicsStop(world);
    }

    TEST_CASE("A dynamic box rests on the voxel floor")
    {
        Scene scene;
        Entity ground = scene.CreateEntity("Voxels");
        MakeFloor(ground.AddComponent<VoxelVolumeComponent>());

        Entity box = scene.CreateEntity("Box");
        box.GetComponent<TransformComponent>().Position = { 8.0f, 12.0f, 8.0f };
        box.AddComponent<RigidBodyComponent>(MotionType::Dynamic).Restitution = 0.0f;
        box.AddComponent<BoxColliderComponent>().HalfExtents = { 0.5f, 0.5f, 0.5f };

        PhysicsWorld world;
        PhysicsSettings ps; ps.ThreadCount = 0;
        world.Init(ps);
        scene.OnPhysicsStart(world);

        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 300; ++i)
            scene.OnPhysicsStep(dt);

        const float rest = box.GetComponent<TransformComponent>().Position.y;
        // Floor top at 4, box half-extent 0.5 -> rests near 4.5.
        CHECK(rest > 4.3f);
        CHECK(rest < 4.8f);

        scene.OnPhysicsStop(world);
    }

    TEST_CASE("Breaking voxels updates collision within a step")
    {
        Scene scene;
        Entity e = scene.CreateEntity("Voxels");
        auto& vc = e.AddComponent<VoxelVolumeComponent>();
        MakeFloor(vc);
        // Runtime render data carries the CollisionDirty set the physics step drains.
        vc.Render = std::make_shared<VoxelRenderData>();

        PhysicsWorld world;
        PhysicsSettings ps; ps.ThreadCount = 0;
        world.Init(ps);
        scene.OnPhysicsStart(world);

        // Before digging, the surface is solid at Y=4.
        auto before = world.RayCast({ 8.5f, 20.0f, 8.5f }, { 0.0f, -1.0f, 0.0f }, 100.0f);
        REQUIRE(before.has_value());
        CHECK(std::fabs(before->Point.y - 4.0f) < 0.05f);

        // Dig the whole column under (8, *, 8) to air.
        for (int y = 0; y < 4; ++y)
            vc.Volume->Set(8, y, 8, 0);
        // Mark the affected chunk for a collision rebuild (what SyncVoxelVolumes does).
        vc.Render->CollisionDirty.insert(VoxelVolume::ChunkCoord(8, 0, 8));

        scene.OnPhysicsStep(1.0f / 60.0f);   // rebuilds the chunk body this step

        // The column is now open all the way down: the ray falls through.
        auto after = world.RayCast({ 8.5f, 20.0f, 8.5f }, { 0.0f, -1.0f, 0.0f }, 100.0f);
        CHECK_FALSE(after.has_value());

        scene.OnPhysicsStop(world);
    }
}
