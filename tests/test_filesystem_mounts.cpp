// test_filesystem_mounts.cpp — FileSystem project:// mount modes (Phase 16 / S1).
//
// NAME mode (assets/projects/<name>) vs PATH mode (an absolute project folder,
// with the assets/ subdir probed once), and the compat guarantee that scene paths
// stay project:// (no absolute-root leak) so external folders relocate for free.

#include <filesystem>
#include <string>

#include "doctest.h"
#include "utils/FileSystem.h"

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/SceneSerializer.h"

namespace fs = std::filesystem;
using Cosmic::FileSystem;

TEST_SUITE("FileSystem project mounts (S1)")
{
    TEST_CASE("NAME mode resolves under assets/projects/<name>")
    {
        FileSystem::SetActiveProject("MyProj");
        CHECK(FileSystem::Resolve("project://scenes/Main.cscene") ==
              fs::path("assets/projects/MyProj/scenes/Main.cscene").generic_string());
        CHECK(FileSystem::ActiveProjectPath().empty());
    }

    TEST_CASE("PATH mode with a flat layout resolves under the root")
    {
        const fs::path root = fs::temp_directory_path() / "cosmic_mount_flat";
        std::error_code ec; fs::remove_all(root, ec);
        fs::create_directories(root / "scenes", ec);

        FileSystem::SetActiveProjectPath(root.generic_string());
        CHECK_FALSE(FileSystem::ActiveProjectPath().empty());
        CHECK(FileSystem::Resolve("project://scenes/Main.cscene") ==
              (root / "scenes" / "Main.cscene").generic_string());
        fs::remove_all(root, ec);
    }

    TEST_CASE("PATH mode with an assets/ subdir routes under assets/")
    {
        const fs::path root = fs::temp_directory_path() / "cosmic_mount_assets";
        std::error_code ec; fs::remove_all(root, ec);
        fs::create_directories(root / "assets" / "scenes", ec);

        FileSystem::SetActiveProjectPath(root.generic_string());
        CHECK(FileSystem::Resolve("project://scenes/Main.cscene") ==
              (root / "assets" / "scenes" / "Main.cscene").generic_string());
        fs::remove_all(root, ec);
    }

    TEST_CASE("SetActiveProject clears an absolute PATH mount")
    {
        const fs::path root = fs::temp_directory_path() / "cosmic_mount_clear";
        std::error_code ec; fs::create_directories(root, ec);

        FileSystem::SetActiveProjectPath(root.generic_string());
        REQUIRE_FALSE(FileSystem::ActiveProjectPath().empty());

        FileSystem::SetActiveProject("Legacy");
        CHECK(FileSystem::ActiveProjectPath().empty());
        CHECK(FileSystem::Resolve("project://x") ==
              fs::path("assets/projects/Legacy/x").generic_string());
        fs::remove_all(root, ec);
    }

    TEST_CASE("scene paths stay project:// (no absolute-root leak)")
    {
        const fs::path root = fs::temp_directory_path() / "cosmic_mount_scene";
        std::error_code ec; fs::create_directories(root, ec);
        FileSystem::SetActiveProjectPath(root.generic_string());

        Cosmic::Ref<Cosmic::Scene> scene = Cosmic::Scene::Create();
        Cosmic::Entity e = scene->CreateEntity("Mesh");
        e.AddComponent<Cosmic::MeshRendererComponent>().MeshPath = "project://models/x.obj";

        const std::string s = Cosmic::SceneSerializer::SaveToString(*scene);
        CHECK(s.find("project://models/x.obj") != std::string::npos);
        CHECK(s.find(root.generic_string()) == std::string::npos);   // no absolute leak
        fs::remove_all(root, ec);
    }
}
