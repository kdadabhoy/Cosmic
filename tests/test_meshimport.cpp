// test_meshimport.cpp — E16 mesh import: the GL-free pieces (settings, .cmeta,
// the CPU geometry transform, OBJ parse). Mesh upload (Mesh::Create) needs GL, so
// MeshImport::Import itself is exercised on-GPU in the editor; here we cover the
// deterministic logic that decides WHAT gets uploaded.

#include "doctest.h"

#include "assets/MeshImport.h"
#include "graphics/Mesh.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <fstream>
#include <filesystem>
#include <string>

using Cosmic::ImportSettings;
using Cosmic::MeshImport;
using Cosmic::Mesh;
using Cosmic::MeshData;

namespace
{
    bool Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f)
    {
        return glm::length(a - b) <= eps;
    }
}

TEST_SUITE("MeshImport (E16)")
{
    TEST_CASE("Extension is lower-cased and dot-stripped")
    {
        CHECK(MeshImport::Extension("models/Rover.STL") == "stl");
        CHECK(MeshImport::Extension("a/b/c.obj") == "obj");
        CHECK(MeshImport::Extension("noext") == "");
    }

    TEST_CASE("Per-extension unit presets (the CAD trap)")
    {
        CHECK(ImportSettings::DefaultFor("stl").Scale == doctest::Approx(0.001f)); // mm -> m
        CHECK(ImportSettings::DefaultFor("fbx").Scale == doctest::Approx(0.01f));  // cm -> m
        CHECK(ImportSettings::DefaultFor("obj").Scale == doctest::Approx(1.0f));
        CHECK(ImportSettings::DefaultFor("dae").Scale == doctest::Approx(1.0f));
    }

    TEST_CASE("OBJ is always supported; assimp formats gate on the backend")
    {
        CHECK(MeshImport::Supports("obj") == true);
        CHECK(MeshImport::Supports("fbx") == MeshImport::AssimpEnabled());
        CHECK(MeshImport::Supports("stl") == MeshImport::AssimpEnabled());
        CHECK(MeshImport::Supports("xyz") == false);
    }

    TEST_CASE(".cmeta round-trips through TOML")
    {
        ImportSettings s;
        s.Scale = 0.0254f;                 // inches -> m
        s.Up = ImportSettings::UpAxis::Z;
        s.FlipUVs = true;
        s.GenerateNormals = false;

        const std::string toml = s.ToCmetaText("thing.stl");
        ImportSettings r = ImportSettings::FromCmetaText(toml, ImportSettings::DefaultFor("stl"));

        CHECK(r.Scale == doctest::Approx(0.0254f));
        CHECK(r.Up == ImportSettings::UpAxis::Z);
        CHECK(r.FlipUVs == true);
        CHECK(r.GenerateNormals == false);
    }

    TEST_CASE("Missing keys fall back to the provided defaults")
    {
        ImportSettings fallback = ImportSettings::DefaultFor("fbx"); // scale 0.01
        ImportSettings r = ImportSettings::FromCmetaText("[import]\nflip_uvs = true\n", fallback);
        CHECK(r.Scale == doctest::Approx(0.01f));    // untouched -> fallback
        CHECK(r.FlipUVs == true);                    // overridden
    }

    TEST_CASE("ApplyTransform bakes scale + Z-up conversion into geometry")
    {
        // A single vertex at +Z, unit +Z normal.
        MeshData d;
        d.Vertices.push_back({ { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } });
        d.Indices = { 0, 0, 0 };

        // The importer's Z-up->Y-up rotation is -90 deg about X, then uniform scale.
        const float scale = 3.0f;
        glm::mat4 m = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1, 0, 0));
        m = m * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
        d.ApplyTransform(m);

        // (0,0,1) -> scale -> (0,0,3) -> Z-up rotation -> (0,3,0).
        CHECK(Near(d.Vertices[0].Position, glm::vec3{ 0.0f, scale, 0.0f }, 1e-4f));
        // Normal follows the rotation (scale drops out after renormalise): +Z -> +Y.
        CHECK(Near(d.Vertices[0].Normal, glm::vec3{ 0.0f, 1.0f, 0.0f }, 1e-4f));
    }

    TEST_CASE("BuildFromOBJ parses geometry and pairs with a baked scale")
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "cosmic_meshimport_test";
        fs::create_directories(dir);
        const fs::path obj = dir / "tri.obj";
        {
            std::ofstream out(obj);
            out << "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
                   "vn 0 0 1\n"
                   "f 1//1 2//1 3//1\n";
        }

        MeshData d = Mesh::BuildFromOBJ(obj.string());
        REQUIRE(d.Vertices.size() == 3);
        REQUIRE(d.Indices.size() == 3);

        d.ApplyTransform(glm::scale(glm::mat4(1.0f), glm::vec3(0.5f)));
        CHECK(Near(d.Vertices[1].Position, glm::vec3{ 0.5f, 0.0f, 0.0f }));
        CHECK(Near(d.Vertices[2].Position, glm::vec3{ 0.0f, 0.5f, 0.0f }));

        fs::remove_all(dir);
    }
}
