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
using Cosmic::MeshVertex;

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

    TEST_CASE("OBJ/glTF are always supported; assimp formats gate on the backend")
    {
        CHECK(MeshImport::Supports("obj") == true);
        CHECK(MeshImport::Supports("gltf") == true);   // cgltf (A1)
        CHECK(MeshImport::Supports("glb") == true);
        CHECK(MeshImport::Supports("fbx") == MeshImport::AssimpEnabled());
        CHECK(MeshImport::Supports("stl") == MeshImport::AssimpEnabled());
        CHECK(MeshImport::Supports("xyz") == false);
    }

    TEST_CASE("Sub-mesh paths compose and parse (A1 fragments)")
    {
        CHECK(MeshImport::SubmeshPath("project://models/gun.fbx", 2) == "project://models/gun.fbx#2");

        std::string base; int sub = -1;
        CHECK(MeshImport::SplitSubmeshPath("project://models/gun.fbx#2", base, sub));
        CHECK(base == "project://models/gun.fbx");
        CHECK(sub == 2);

        // No fragment / malformed fragments -> false, outputs untouched.
        base = "untouched"; sub = 99;
        CHECK(MeshImport::SplitSubmeshPath("project://models/gun.fbx", base, sub) == false);
        CHECK(MeshImport::SplitSubmeshPath("models/oddname#", base, sub) == false);
        CHECK(MeshImport::SplitSubmeshPath("models/odd#name", base, sub) == false);
        CHECK(base == "untouched");
        CHECK(sub == 99);
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

    // ------------------------------------------------------------------------
    // A1 — CPU import through the real backends (ImportData / ImportModelData;
    // no GL). glTF exercises cgltf in every build; the STL/PLY/DAE/OBJ-material
    // cases are the "gated assimp cases" of the A1 acceptance line and run when
    // the backend is compiled in (COSMIC_WITH_ASSIMP — default ON since A1).
    // ------------------------------------------------------------------------

    TEST_CASE("glTF imports through cgltf at authored size (A1)")
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "cosmic_meshimport_gltf";
        fs::create_directories(dir);

        {
            // indices (3 x uint16 = 6 bytes, padded to 8) then positions (36 bytes).
            std::ofstream bin(dir / "tri.bin", std::ios::binary);
            const uint16_t idx[4]  = { 0, 1, 2, 0 };                 // 4th is padding
            const float    pos[9]  = { 0,0,0,  1,0,0,  0,1,0 };
            bin.write(reinterpret_cast<const char*>(idx), 8);
            bin.write(reinterpret_cast<const char*>(pos), 36);
        }
        {
            std::ofstream out(dir / "tri.gltf");
            out << R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0, "name": "tri"}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 1}, "indices": 0}]}],
  "buffers": [{"uri": "tri.bin", "byteLength": 44}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 6},
    {"buffer": 0, "byteOffset": 8, "byteLength": 36}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5123, "count": 3, "type": "SCALAR"},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3",
     "min": [0,0,0], "max": [1,1,0]}
  ]
})";
        }

        const std::string path = (dir / "tri.gltf").string();
        MeshData d = MeshImport::ImportData(path, ImportSettings::DefaultFor("gltf"));
        REQUIRE(d.Vertices.size() == 3);
        REQUIRE(d.Indices.size() == 3);
        CHECK(Near(d.Vertices[1].Position, glm::vec3{ 1.0f, 0.0f, 0.0f }));  // meters, x1
        CHECK(Near(d.Vertices[2].Position, glm::vec3{ 0.0f, 1.0f, 0.0f }));
        // No normals in the file -> derived (unit-length, +Z for this winding).
        CHECK(Near(d.Vertices[0].Normal, glm::vec3{ 0.0f, 0.0f, 1.0f }));

        fs::remove_all(dir);
    }

    // The assimp-backed cases guard at runtime so a -DCOSMIC_WITH_ASSIMP=OFF
    // fallback build still passes (they trivially no-op there).
    TEST_CASE("STL (mm) lands at correct meters through assimp")
    {
        if (!MeshImport::AssimpEnabled())
            return;
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "cosmic_meshimport_stl";
        fs::create_directories(dir);
        const fs::path stl = dir / "part.stl";
        {
            std::ofstream out(stl);
            out << "solid part\n"
                   "  facet normal 0 0 1\n"
                   "    outer loop\n"
                   "      vertex 0 0 0\n"
                   "      vertex 1000 0 0\n"
                   "      vertex 0 1000 0\n"
                   "    endloop\n"
                   "  endfacet\n"
                   "endsolid part\n";
        }

        // The CAD preset: STL is unitless, assumed mm -> x0.001 into meters.
        const ImportSettings s = ImportSettings::DefaultFor("stl");
        MeshData d = MeshImport::ImportData(stl.string(), s);
        REQUIRE(d.Vertices.size() == 3);
        REQUIRE(d.Indices.size() == 3);

        glm::vec3 mn{ 1e9f }, mx{ -1e9f };
        for (const MeshVertex& v : d.Vertices)
        {
            mn = glm::min(mn, v.Position);
            mx = glm::max(mx, v.Position);
        }
        CHECK(Near(mn, glm::vec3{ 0.0f }));
        CHECK(Near(mx, glm::vec3{ 1.0f, 1.0f, 0.0f }));   // 1000 mm == 1 m

        fs::remove_all(dir);
    }

    TEST_CASE("PLY imports through assimp at unit scale")
    {
        if (!MeshImport::AssimpEnabled())
            return;
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "cosmic_meshimport_ply";
        fs::create_directories(dir);
        const fs::path ply = dir / "tri.ply";
        {
            std::ofstream out(ply);
            out << "ply\n"
                   "format ascii 1.0\n"
                   "element vertex 3\n"
                   "property float x\n"
                   "property float y\n"
                   "property float z\n"
                   "element face 1\n"
                   "property list uchar int vertex_indices\n"
                   "end_header\n"
                   "0 0 0\n"
                   "2 0 0\n"
                   "0 2 0\n"
                   "3 0 1 2\n";
        }

        MeshData d = MeshImport::ImportData(ply.string(), ImportSettings::DefaultFor("ply"));
        REQUIRE(d.Vertices.size() == 3);
        CHECK(Near(d.Vertices[1].Position, glm::vec3{ 2.0f, 0.0f, 0.0f }));

        fs::remove_all(dir);
    }

    TEST_CASE("DAE (COLLADA) imports through assimp")
    {
        if (!MeshImport::AssimpEnabled())
            return;
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "cosmic_meshimport_dae";
        fs::create_directories(dir);
        const fs::path dae = dir / "tri.dae";
        {
            std::ofstream out(dae);
            out << R"(<?xml version="1.0" encoding="utf-8"?>
<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">
  <asset><unit name="meter" meter="1"/><up_axis>Y_UP</up_axis></asset>
  <library_geometries>
    <geometry id="tri" name="tri">
      <mesh>
        <source id="tri-pos">
          <float_array id="tri-pos-array" count="9">0 0 0 3 0 0 0 3 0</float_array>
          <technique_common>
            <accessor source="#tri-pos-array" count="3" stride="3">
              <param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/>
            </accessor>
          </technique_common>
        </source>
        <vertices id="tri-vtx"><input semantic="POSITION" source="#tri-pos"/></vertices>
        <triangles count="1">
          <input semantic="VERTEX" source="#tri-vtx" offset="0"/>
          <p>0 1 2</p>
        </triangles>
      </mesh>
    </geometry>
  </library_geometries>
  <library_visual_scenes>
    <visual_scene id="Scene" name="Scene">
      <node id="trinode" name="trinode"><instance_geometry url="#tri"/></node>
    </visual_scene>
  </library_visual_scenes>
  <scene><instance_visual_scene url="#Scene"/></scene>
</COLLADA>
)";
        }

        MeshData d = MeshImport::ImportData(dae.string(), ImportSettings::DefaultFor("dae"));
        REQUIRE(d.Vertices.size() == 3);
        CHECK(Near(d.Vertices[1].Position, glm::vec3{ 3.0f, 0.0f, 0.0f }));

        fs::remove_all(dir);
    }

    TEST_CASE("Multi-object OBJ + MTL -> sub-meshes with materials (A1 rich import)")
    {
        if (!MeshImport::AssimpEnabled())
            return;
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "cosmic_meshimport_multi";
        fs::create_directories(dir);
        {
            std::ofstream mtl(dir / "multi.mtl");
            mtl << "newmtl Red\nKd 1 0 0\n\nnewmtl Blue\nKd 0 0 1\n";
        }
        const fs::path obj = dir / "multi.obj";
        {
            std::ofstream out(obj);
            out << "mtllib multi.mtl\n"
                   "o RedTri\n"
                   "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
                   "usemtl Red\n"
                   "f 1 2 3\n"
                   "o BlueTri\n"
                   "v 0 0 5\nv 1 0 5\nv 0 1 5\n"
                   "usemtl Blue\n"
                   "f 4 5 6\n";
        }

        const ImportSettings s = ImportSettings::DefaultFor("obj");

        Cosmic::ImportedModelDesc desc;
        REQUIRE(MeshImport::ImportModelData(desc, obj.string(), s));
        REQUIRE(desc.Meshes.size() == 2);

        // Every sub-mesh maps to a real material; Red/Blue arrive with their Kd.
        int redMat = -1, blueMat = -1;
        for (size_t i = 0; i < desc.Materials.size(); ++i)
        {
            if (desc.Materials[i].Name == "Red")  redMat  = (int)i;
            if (desc.Materials[i].Name == "Blue") blueMat = (int)i;
        }
        REQUIRE(redMat >= 0);
        REQUIRE(blueMat >= 0);
        CHECK(desc.Materials[(size_t)redMat].Albedo.r  == doctest::Approx(1.0f));
        CHECK(desc.Materials[(size_t)redMat].Albedo.g  == doctest::Approx(0.0f));
        CHECK(desc.Materials[(size_t)blueMat].Albedo.b == doctest::Approx(1.0f));
        for (const Cosmic::ImportedMeshDesc& sm : desc.Meshes)
        {
            CHECK(sm.MaterialIndex >= 0);
            CHECK(sm.MaterialIndex < (int)desc.Materials.size());
        }

        // Sub-mesh addressing: #0 and #1 import individually and differ in Z
        // (one triangle sits at z=0, the other at z=5).
        MeshData d0 = MeshImport::ImportData(obj.string(), s, 0);
        MeshData d1 = MeshImport::ImportData(obj.string(), s, 1);
        REQUIRE(d0.Vertices.size() == 3);
        REQUIRE(d1.Vertices.size() == 3);
        const float z0 = d0.Vertices[0].Position.z;
        const float z1 = d1.Vertices[0].Position.z;
        CHECK(std::abs(z0 - z1) == doctest::Approx(5.0f));

        // Out-of-range sub-mesh -> empty (logged, not fatal).
        CHECK(MeshImport::ImportData(obj.string(), s, 7).Vertices.empty());

        fs::remove_all(dir);
    }
}
