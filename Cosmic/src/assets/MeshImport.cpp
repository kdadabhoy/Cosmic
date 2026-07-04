// assets/MeshImport.cpp — see MeshImport.h.
//
// ----------------------------------------------------------------------------
// Assimp vendoring (the flip-on step for FBX/STL/DAE/PLY):
//   1. Vendor assimp under Cosmic/dependencies/assimp (pin the version in a
//      README), importers trimmed to FBX/OBJ/STL/DAE/PLY, exporters + tests OFF.
//   2. In the engine CMake: add_subdirectory(dependencies/assimp) with
//      ASSIMP_BUILD_TESTS=OFF / ASSIMP_NO_EXPORT=ON / ASSIMP_BUILD_ASSIMP_TOOLS=OFF,
//      target_link_libraries(Cosmic PRIVATE assimp), and
//      target_compile_definitions(Cosmic PRIVATE COSMIC_WITH_ASSIMP).
//   3. Rebuild — the guarded block below then handles FBX/STL/DAE/PLY. No other
//      code changes: MeshImport::Supports/Import already route to it.
// It is deliberately gated off by default: assimp is a large static library and
// vendoring it is a one-time heavyweight step, best run where the full build can
// complete and FBX/STL round-trips can be verified.
// ----------------------------------------------------------------------------

#include "assets/MeshImport.h"

#include "graphics/Mesh.h"
#include "utils/Config.h"
#include "core/Log.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#ifdef COSMIC_WITH_ASSIMP
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#endif

namespace Cosmic
{
    // ------------------------------------------------------------------------
    // ImportSettings
    // ------------------------------------------------------------------------

    ImportSettings ImportSettings::DefaultFor(const std::string& extLower)
    {
        ImportSettings s;
        if (extLower == "stl")       s.Scale = 0.001f;  // STL is unitless; assume mm (the CAD norm)
        else if (extLower == "fbx")  s.Scale = 0.01f;   // FBX authored in cm
        else                         s.Scale = 1.0f;    // OBJ/DAE/PLY assumed meters
        s.Up = UpAxis::Y;
        return s;
    }

    ImportSettings ImportSettings::FromCmetaText(const std::string& tomlText, const ImportSettings& fallback)
    {
        ImportSettings s = fallback;
        Ref<Config> cfg = Config::Parse(tomlText, "<cmeta>");
        if (!cfg)
            return s;

        s.Scale           = cfg->Get<float>("import.scale", fallback.Scale);
        s.FlipUVs         = cfg->Get<bool>("import.flip_uvs", fallback.FlipUVs);
        s.GenerateNormals = cfg->Get<bool>("import.generate_normals", fallback.GenerateNormals);
        const std::string up = cfg->Get<std::string>("import.up_axis",
                                                      fallback.Up == UpAxis::Z ? "Z" : "Y");
        s.Up = (up == "Z" || up == "z") ? UpAxis::Z : UpAxis::Y;
        return s;
    }

    std::string ImportSettings::ToCmetaText(const std::string& sourceFile) const
    {
        std::ostringstream os;
        os << "# Cosmic mesh import settings (.cmeta) — edit + re-import to change.\n";
        os << "[import]\n";
        os << "source = \"" << sourceFile << "\"\n";
        os << "scale = " << Scale << "\n";
        os << "up_axis = \"" << (Up == UpAxis::Z ? "Z" : "Y") << "\"\n";
        os << "flip_uvs = " << (FlipUVs ? "true" : "false") << "\n";
        os << "generate_normals = " << (GenerateNormals ? "true" : "false") << "\n";
        return os.str();
    }

    // ------------------------------------------------------------------------
    // MeshImport
    // ------------------------------------------------------------------------

    std::string MeshImport::Extension(const std::string& path)
    {
        const size_t dot = path.find_last_of('.');
        if (dot == std::string::npos)
            return {};
        std::string ext = path.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        return ext;
    }

    bool MeshImport::AssimpEnabled()
    {
#ifdef COSMIC_WITH_ASSIMP
        return true;
#else
        return false;
#endif
    }

    bool MeshImport::Supports(const std::string& extLower)
    {
        if (extLower == "obj")
            return true;                     // engine's own parser
        if (extLower == "fbx" || extLower == "stl" || extLower == "dae" || extLower == "ply")
            return AssimpEnabled();          // assimp backend
        return false;
    }

    std::string MeshImport::CmetaPathFor(const std::string& sourcePath)
    {
        return sourcePath + ".cmeta";
    }

    ImportSettings MeshImport::LoadOrInitMeta(const std::string& resolvedSourcePath)
    {
        const std::string ext   = Extension(resolvedSourcePath);
        ImportSettings     preset = ImportSettings::DefaultFor(ext);
        const std::string cmeta = CmetaPathFor(resolvedSourcePath);

        std::ifstream in(cmeta);
        if (in.good())
        {
            std::stringstream ss;
            ss << in.rdbuf();
            return ImportSettings::FromCmetaText(ss.str(), preset);
        }

        // First time seen: write the preset so the import is reproducible.
        const std::string sourceName = resolvedSourcePath.substr(resolvedSourcePath.find_last_of("/\\") + 1);
        std::ofstream out(cmeta, std::ios::trunc);
        if (out.good())
            out << preset.ToCmetaText(sourceName);
        else
            CS_CORE_WARN("MeshImport: could not write '{0}'.", cmeta);
        return preset;
    }

    namespace
    {
        // The unit + up-axis matrix baked into imported geometry.
        glm::mat4 ImportTransform(const ImportSettings& s)
        {
            glm::mat4 m(1.0f);
            if (s.Up == ImportSettings::UpAxis::Z)
                m = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1, 0, 0)); // Z-up -> Y-up
            return m * glm::scale(glm::mat4(1.0f), glm::vec3(s.Scale));
        }

#ifdef COSMIC_WITH_ASSIMP
        MeshData ImportAssimp(const std::string& path, const ImportSettings& s)
        {
            Assimp::Importer importer;
            unsigned flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                             aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals |
                             aiProcess_PreTransformVertices;
            if (s.FlipUVs)
                flags |= aiProcess_FlipUVs;
            const aiScene* scene = importer.ReadFile(path, flags);
            if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
            {
                CS_CORE_ERROR("MeshImport(assimp): '{0}': {1}", path, importer.GetErrorString());
                return {};
            }

            MeshData data;
            for (unsigned mi = 0; mi < scene->mNumMeshes; ++mi)
            {
                const aiMesh* m = scene->mMeshes[mi];
                const uint32_t base = (uint32_t)data.Vertices.size();
                for (unsigned vi = 0; vi < m->mNumVertices; ++vi)
                {
                    MeshVertex v{};
                    v.Position = { m->mVertices[vi].x, m->mVertices[vi].y, m->mVertices[vi].z };
                    if (m->HasNormals())
                        v.Normal = { m->mNormals[vi].x, m->mNormals[vi].y, m->mNormals[vi].z };
                    if (m->HasTextureCoords(0))
                        v.TexCoord = { m->mTextureCoords[0][vi].x, m->mTextureCoords[0][vi].y };
                    if (m->HasTangentsAndBitangents())
                        v.Tangent = glm::vec4(m->mTangents[vi].x, m->mTangents[vi].y, m->mTangents[vi].z, 1.0f);
                    data.Vertices.push_back(v);
                }
                for (unsigned fi = 0; fi < m->mNumFaces; ++fi)
                {
                    const aiFace& f = m->mFaces[fi];
                    for (unsigned k = 0; k + 2 < f.mNumIndices; ++k)
                    {
                        data.Indices.push_back(base + f.mIndices[0]);
                        data.Indices.push_back(base + f.mIndices[k + 1]);
                        data.Indices.push_back(base + f.mIndices[k + 2]);
                    }
                }
            }
            return data;
        }
#endif
    }

    Ref<Mesh> MeshImport::Import(const std::string& resolvedSourcePath, const ImportSettings& settings)
    {
        const std::string ext = Extension(resolvedSourcePath);

        MeshData data;
        if (ext == "obj")
        {
            data = Mesh::BuildFromOBJ(resolvedSourcePath);
        }
        else if (ext == "fbx" || ext == "stl" || ext == "dae" || ext == "ply")
        {
#ifdef COSMIC_WITH_ASSIMP
            data = ImportAssimp(resolvedSourcePath, settings);
#else
            CS_CORE_ERROR("MeshImport: '{0}' — {1} import needs the assimp backend "
                          "(rebuild Cosmic with COSMIC_WITH_ASSIMP). See MeshImport.cpp.",
                          resolvedSourcePath, ext);
            return nullptr;
#endif
        }
        else
        {
            CS_CORE_ERROR("MeshImport: unsupported model format '.{0}' ('{1}').", ext, resolvedSourcePath);
            return nullptr;
        }

        if (data.Vertices.empty())
            return nullptr;   // the loader already logged why

        // OBJ FlipUVs is applied here (assimp does it in-loader above).
        if (settings.FlipUVs && ext == "obj")
            for (MeshVertex& v : data.Vertices)
                v.TexCoord.y = 1.0f - v.TexCoord.y;

        data.ApplyTransform(ImportTransform(settings));
        return Mesh::Create(data);
    }
}
