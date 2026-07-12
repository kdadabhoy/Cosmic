// assets/MeshImport.cpp — see MeshImport.h.
//
// ----------------------------------------------------------------------------
// Assimp backend (A1, Phase 20 — vendored + default ON):
//   * dependencies/assimp — pinned v5.4.3, trimmed to the FBX/OBJ/STL/DAE/PLY
//     importers (see its README-COSMIC.md for the trim + local patches).
//   * Engine CMake: add_subdirectory + PRIVATE link + COSMIC_WITH_ASSIMP,
//     default ON via option(COSMIC_WITH_ASSIMP) — configure with
//     -DCOSMIC_WITH_ASSIMP=OFF for the OBJ/glTF-only fallback build.
//   * This file is the ONLY consumer of assimp headers (the Jolt firewall
//     pattern) — aiScene types never reach public engine headers.
// glTF/GLB use cgltf (always compiled — the S4.4b Model path's library), so the
// rich-import surface below covers every dialog format in every build.
// ----------------------------------------------------------------------------

#include "assets/MeshImport.h"

#include "graphics/Mesh.h"
#include "utils/Config.h"
#include "core/Log.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "cgltf.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <unordered_map>

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
        else                         s.Scale = 1.0f;    // OBJ/DAE/PLY/glTF assumed meters
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
        if (extLower == "gltf" || extLower == "glb")
            return true;                     // cgltf (always compiled)
        if (extLower == "fbx" || extLower == "stl" || extLower == "dae" || extLower == "ply")
            return AssimpEnabled();          // assimp backend
        return false;
    }

    std::string MeshImport::CmetaPathFor(const std::string& sourcePath)
    {
        return sourcePath + ".cmeta";
    }

    std::string MeshImport::SubmeshPath(const std::string& sourcePath, int submeshIndex)
    {
        return sourcePath + "#" + std::to_string(submeshIndex);
    }

    bool MeshImport::SplitSubmeshPath(const std::string& path, std::string& baseOut, int& submeshOut)
    {
        const size_t hash = path.find_last_of('#');
        if (hash == std::string::npos || hash + 1 >= path.size())
            return false;
        for (size_t i = hash + 1; i < path.size(); ++i)
            if (!std::isdigit((unsigned char)path[i]))
                return false;
        baseOut    = path.substr(0, hash);
        submeshOut = std::atoi(path.c_str() + hash + 1);
        return true;
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

        // Bake `xform` into geometry and fix triangle winding if it mirrors
        // (negative determinant reverses the baked winding; normals are already
        // handled by ApplyTransform's inverse-transpose).
        void BakeTransform(MeshData& d, const glm::mat4& xform)
        {
            d.ApplyTransform(xform);
            if (glm::determinant(glm::mat3(xform)) < 0.0f)
                for (size_t k = 0; k + 2 < d.Indices.size(); k += 3)
                    std::swap(d.Indices[k + 1], d.Indices[k + 2]);
        }

        // Concatenate every sub-mesh into one MeshData (the merged import path).
        MeshData MergeMeshes(ImportedModelDesc& desc)
        {
            MeshData merged;
            for (ImportedMeshDesc& sm : desc.Meshes)
            {
                const uint32_t base = (uint32_t)merged.Vertices.size();
                merged.Vertices.insert(merged.Vertices.end(),
                                       sm.Geometry.Vertices.begin(), sm.Geometry.Vertices.end());
                for (uint32_t idx : sm.Geometry.Indices)
                    merged.Indices.push_back(base + idx);
            }
            return merged;
        }

        // Derive smooth normals for meshes that came without them (accumulate
        // face normals, normalize) — the Model.cpp fallback, shared here.
        void DeriveNormalsIfMissing(MeshData& d, bool hadNormals)
        {
            if (hadNormals)
                return;
            for (size_t k = 0; k + 2 < d.Indices.size(); k += 3)
            {
                MeshVertex& v0 = d.Vertices[d.Indices[k]];
                MeshVertex& v1 = d.Vertices[d.Indices[k + 1]];
                MeshVertex& v2 = d.Vertices[d.Indices[k + 2]];
                const glm::vec3 fn = glm::cross(v1.Position - v0.Position, v2.Position - v0.Position);
                v0.Normal += fn; v1.Normal += fn; v2.Normal += fn;
            }
            for (MeshVertex& v : d.Vertices)
                v.Normal = (glm::dot(v.Normal, v.Normal) > 1e-12f)
                               ? glm::normalize(v.Normal) : glm::vec3(0.0f, 1.0f, 0.0f);
        }

        // ------------------------------------------------------------------
        // glTF / GLB reader (cgltf) — CPU-only twin of graphics/Model's import:
        // same node traversal and primitive decode, but into MeshData +
        // material/texture DESCRIPTIONS instead of GPU objects.
        // ------------------------------------------------------------------

        const cgltf_accessor* FindAttribute(const cgltf_primitive& prim,
                                            cgltf_attribute_type type, cgltf_int setIndex = 0)
        {
            for (cgltf_size i = 0; i < prim.attributes_count; ++i)
            {
                const cgltf_attribute& a = prim.attributes[i];
                if (a.type == type && a.index == setIndex)
                    return a.data;
            }
            return nullptr;
        }

        // Resolve one glTF image to a texture reference string: an external URI
        // as-authored, or "*<i>" after appending the embedded blob to `out`.
        std::string GltfTextureRef(const cgltf_data* data, const cgltf_texture* tex,
                                   ImportedModelDesc& out,
                                   std::vector<std::string>& imageRefs)
        {
            if (!tex || !tex->image)
                return {};
            const cgltf_image* img = tex->image;
            const size_t imgIndex  = (size_t)(img - data->images);
            if (imgIndex < imageRefs.size() && !imageRefs[imgIndex].empty())
                return imageRefs[imgIndex];

            std::string ref;
            if (img->uri)
            {
                if (std::strncmp(img->uri, "data:", 5) == 0)
                    CS_CORE_WARN("MeshImport(glTF): base64 image data-URI not supported — skipping.");
                else
                    ref = img->uri;
            }
            else if (img->buffer_view && img->buffer_view->buffer && img->buffer_view->buffer->data)
            {
                ImportedTextureDesc t;
                t.Name = (img->name && img->name[0]) ? img->name
                                                     : "embedded_" + std::to_string(out.EmbeddedTextures.size());
                const uint8_t* ptr = static_cast<const uint8_t*>(img->buffer_view->buffer->data)
                                     + img->buffer_view->offset;
                t.Bytes.assign(ptr, ptr + img->buffer_view->size);
                if (img->mime_type && std::strcmp(img->mime_type, "image/jpeg") == 0)
                    t.FormatHint = "jpg";
                else if (t.Bytes.size() >= 2 && t.Bytes[0] == 0xFF && t.Bytes[1] == 0xD8)
                    t.FormatHint = "jpg";
                else
                    t.FormatHint = "png";
                ref = "*" + std::to_string(out.EmbeddedTextures.size());
                out.EmbeddedTextures.push_back(std::move(t));
            }

            if (imgIndex < imageRefs.size())
                imageRefs[imgIndex] = ref;
            return ref;
        }

        bool ReadGltf(ImportedModelDesc& out, const std::string& path, const ImportSettings& s)
        {
            cgltf_options options{};
            cgltf_data*   data = nullptr;
            if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success)
            {
                CS_CORE_ERROR("MeshImport(glTF): failed to parse '{0}'.", path);
                return false;
            }
            if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success)
            {
                CS_CORE_ERROR("MeshImport(glTF): failed to load buffers for '{0}'.", path);
                cgltf_free(data);
                return false;
            }

            // Materials first, so primitives can index them 1:1 with the file.
            std::vector<std::string> imageRefs(data->images_count);
            for (cgltf_size mi = 0; mi < data->materials_count; ++mi)
            {
                const cgltf_material& m = data->materials[mi];
                ImportedMaterialDesc md;
                md.Name = m.name ? m.name : "";
                if (m.has_pbr_metallic_roughness)
                {
                    const cgltf_pbr_metallic_roughness& mr = m.pbr_metallic_roughness;
                    md.Albedo    = glm::vec4(mr.base_color_factor[0], mr.base_color_factor[1],
                                             mr.base_color_factor[2], mr.base_color_factor[3]);
                    md.Opacity   = mr.base_color_factor[3];
                    md.Metallic  = mr.metallic_factor;
                    md.Roughness = mr.roughness_factor;
                    md.AlbedoMap     = GltfTextureRef(data, mr.base_color_texture.texture, out, imageRefs);
                    md.MetalRoughMap = GltfTextureRef(data, mr.metallic_roughness_texture.texture, out, imageRefs);
                }
                md.Emissive    = glm::vec3(m.emissive_factor[0], m.emissive_factor[1], m.emissive_factor[2]);
                md.NormalMap   = GltfTextureRef(data, m.normal_texture.texture, out, imageRefs);
                md.AOMap       = GltfTextureRef(data, m.occlusion_texture.texture, out, imageRefs);
                md.EmissiveMap = GltfTextureRef(data, m.emissive_texture.texture, out, imageRefs);
                out.Materials.push_back(std::move(md));
            }

            // --- Skin 0 -> Skeleton (A2). Joint order = the skin's joint array,
            // which is exactly what JOINTS_0 indexes. Parent links only within
            // the skin; LocalBind from the node's local TRS. ---
            const cgltf_skin* skin = data->skins_count > 0 ? &data->skins[0] : nullptr;
            if (data->skins_count > 1)
                CS_CORE_WARN("MeshImport(glTF): '{0}' has {1} skins — only skin 0 imports (A2 v1).",
                             path, data->skins_count);
            if (skin)
            {
                out.Bones.ImportCorrection    = ImportTransform(s);
                out.Bones.ImportCorrectionInv = glm::inverse(out.Bones.ImportCorrection);
                out.Bones.Joints.resize(skin->joints_count);
                for (cgltf_size j = 0; j < skin->joints_count; ++j)
                {
                    const cgltf_node* jn = skin->joints[j];
                    SkeletonJoint& joint = out.Bones.Joints[j];
                    joint.Name = (jn->name && jn->name[0]) ? jn->name : "Joint_" + std::to_string(j);

                    float local[16];
                    cgltf_node_transform_local(jn, local);
                    joint.LocalBind = glm::make_mat4(local);

                    joint.Parent = -1;
                    for (cgltf_size p = 0; p < skin->joints_count; ++p)
                        if (skin->joints[p] == jn->parent)
                        {
                            joint.Parent = (int)p;
                            break;
                        }

                    if (skin->inverse_bind_matrices)
                    {
                        float ibm[16];
                        cgltf_accessor_read_float(skin->inverse_bind_matrices, j, ibm, 16);
                        joint.InverseBind = glm::make_mat4(ibm);
                    }
                }
            }

            // --- Animations -> Clips (A2). Channels targeting skin joints only;
            // LINEAR and STEP read as keyframes (STEP approximated linear),
            // CUBICSPLINE reads the value out of each in/value/out triple. ---
            for (cgltf_size ai = 0; ai < data->animations_count; ++ai)
            {
                const cgltf_animation& anim = data->animations[ai];
                AnimationClip clip;
                clip.Name = (anim.name && anim.name[0]) ? anim.name : "Clip_" + std::to_string(ai);

                std::vector<int> channelOf(skin ? skin->joints_count : 0, -1);
                for (cgltf_size ci = 0; ci < anim.channels_count; ++ci)
                {
                    const cgltf_animation_channel& ch = anim.channels[ci];
                    if (!ch.sampler || !ch.target_node || !skin)
                        continue;
                    int joint = -1;
                    for (cgltf_size j = 0; j < skin->joints_count; ++j)
                        if (skin->joints[j] == ch.target_node)
                        {
                            joint = (int)j;
                            break;
                        }
                    if (joint < 0)
                        continue;   // node animation outside the skin — v1 ignores

                    if (channelOf[(size_t)joint] < 0)
                    {
                        channelOf[(size_t)joint] = (int)clip.Channels.size();
                        AnimationChannel c;
                        c.JointIndex = joint;
                        clip.Channels.push_back(std::move(c));
                    }
                    AnimationChannel& target = clip.Channels[(size_t)channelOf[(size_t)joint]];

                    const cgltf_animation_sampler& sm = *ch.sampler;
                    if (!sm.input || !sm.output)
                        continue;
                    const bool cubic = sm.interpolation == cgltf_interpolation_type_cubic_spline;
                    const size_t keyCount = (size_t)sm.input->count;
                    for (size_t k = 0; k < keyCount; ++k)
                    {
                        float t = 0.0f;
                        cgltf_accessor_read_float(sm.input, k, &t, 1);
                        clip.Duration = std::max(clip.Duration, t);
                        // CUBICSPLINE output holds {inTangent, value, outTangent}.
                        const size_t vk = cubic ? k * 3 + 1 : k;
                        switch (ch.target_path)
                        {
                            case cgltf_animation_path_type_translation:
                            {
                                float v[3] = { 0, 0, 0 };
                                cgltf_accessor_read_float(sm.output, vk, v, 3);
                                target.PosTimes.push_back(t);
                                target.PosValues.push_back({ v[0], v[1], v[2] });
                                break;
                            }
                            case cgltf_animation_path_type_rotation:
                            {
                                float v[4] = { 0, 0, 0, 1 };
                                cgltf_accessor_read_float(sm.output, vk, v, 4);
                                target.RotTimes.push_back(t);
                                target.RotValues.push_back(glm::quat(v[3], v[0], v[1], v[2]));
                                break;
                            }
                            case cgltf_animation_path_type_scale:
                            {
                                float v[3] = { 1, 1, 1 };
                                cgltf_accessor_read_float(sm.output, vk, v, 3);
                                target.SclTimes.push_back(t);
                                target.SclValues.push_back({ v[0], v[1], v[2] });
                                break;
                            }
                            default:
                                break;   // weights (morph targets) — out of A2 scope
                        }
                    }
                }
                if (!clip.Channels.empty())
                    out.Clips.push_back(std::move(clip));
            }

            // Import the default scene's node graph (Model.cpp convention).
            std::vector<const cgltf_node*> importNodes;
            if (data->scene && data->scene->nodes_count > 0)
            {
                std::vector<const cgltf_node*> stack;
                for (cgltf_size r = 0; r < data->scene->nodes_count; ++r)
                    stack.push_back(data->scene->nodes[r]);
                while (!stack.empty())
                {
                    const cgltf_node* node = stack.back();
                    stack.pop_back();
                    importNodes.push_back(node);
                    for (cgltf_size c = 0; c < node->children_count; ++c)
                        stack.push_back(node->children[c]);
                }
            }
            else
            {
                for (cgltf_size n = 0; n < data->nodes_count; ++n)
                    importNodes.push_back(&data->nodes[n]);
            }

            const glm::mat4 importXform = ImportTransform(s);
            for (const cgltf_node* nodePtr : importNodes)
            {
                const cgltf_node& node = *nodePtr;
                if (!node.mesh)
                    continue;

                // Skinned nodes: the joints own the motion — the node's own
                // world transform is ignored per the glTF spec, so only the
                // unit/up-axis matrix bakes (the palette is conjugated by it).
                const bool skinned = skin && node.skin == skin;
                if (node.skin && node.skin != skin)
                    CS_CORE_WARN("MeshImport(glTF): '{0}': a second skin's mesh imports statically (A2 v1).",
                                 path);

                float worldArr[16];
                cgltf_node_transform_world(&node, worldArr);
                const glm::mat4 xform = skinned ? importXform
                                                : importXform * glm::make_mat4(worldArr);

                for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p)
                {
                    const cgltf_primitive& prim = node.mesh->primitives[p];
                    if (prim.type != cgltf_primitive_type_triangles)
                    {
                        CS_CORE_WARN("MeshImport(glTF): skipping non-triangle primitive in '{0}'.", path);
                        continue;
                    }
                    const cgltf_accessor* posAcc = FindAttribute(prim, cgltf_attribute_type_position);
                    if (!posAcc || posAcc->count == 0)
                        continue;
                    const cgltf_accessor* nrmAcc = FindAttribute(prim, cgltf_attribute_type_normal);
                    const cgltf_accessor* uvAcc  = FindAttribute(prim, cgltf_attribute_type_texcoord, 0);
                    const cgltf_accessor* tanAcc = FindAttribute(prim, cgltf_attribute_type_tangent);

                    ImportedMeshDesc sm;
                    sm.Name = (node.name && node.name[0]) ? node.name
                              : (node.mesh->name && node.mesh->name[0]) ? node.mesh->name
                              : "Mesh_" + std::to_string(out.Meshes.size());
                    sm.MaterialIndex = prim.material ? (int)(prim.material - data->materials) : -1;

                    MeshData& d = sm.Geometry;
                    d.Vertices.resize((size_t)posAcc->count);
                    for (size_t i = 0; i < d.Vertices.size(); ++i)
                    {
                        MeshVertex& v = d.Vertices[i];
                        float p3[3] = { 0, 0, 0 };
                        cgltf_accessor_read_float(posAcc, i, p3, 3);
                        v.Position = { p3[0], p3[1], p3[2] };
                        if (nrmAcc)
                        {
                            float n3[3] = { 0, 1, 0 };
                            cgltf_accessor_read_float(nrmAcc, i, n3, 3);
                            v.Normal = { n3[0], n3[1], n3[2] };
                        }
                        else
                            v.Normal = glm::vec3(0.0f);   // derived below
                        if (uvAcc)
                        {
                            // UVs pass through as-authored — the Model.cpp (S6.2)
                            // convention this path mirrors. FlipUVs from the
                            // .cmeta still applies for sources that need it.
                            float t2[2] = { 0, 0 };
                            cgltf_accessor_read_float(uvAcc, i, t2, 2);
                            v.TexCoord = s.FlipUVs ? glm::vec2(t2[0], 1.0f - t2[1])
                                                   : glm::vec2(t2[0], t2[1]);
                        }
                        if (tanAcc)
                        {
                            float t4[4] = { 1, 0, 0, 1 };
                            cgltf_accessor_read_float(tanAcc, i, t4, 4);
                            v.Tangent = { t4[0], t4[1], t4[2], t4[3] };
                        }
                    }

                    if (prim.indices && prim.indices->count > 0)
                    {
                        d.Indices.resize((size_t)prim.indices->count);
                        for (size_t k = 0; k < d.Indices.size(); ++k)
                            d.Indices[k] = (uint32_t)cgltf_accessor_read_index(prim.indices, k);
                    }
                    else
                    {
                        d.Indices.resize(d.Vertices.size());
                        for (size_t k = 0; k < d.Indices.size(); ++k)
                            d.Indices[k] = (uint32_t)k;
                    }

                    // A2 — joint influences for skinned primitives.
                    if (skinned)
                    {
                        const cgltf_accessor* jAcc = FindAttribute(prim, cgltf_attribute_type_joints, 0);
                        const cgltf_accessor* wAcc = FindAttribute(prim, cgltf_attribute_type_weights, 0);
                        if (jAcc && wAcc)
                        {
                            sm.Skin.resize(d.Vertices.size());
                            for (size_t i = 0; i < sm.Skin.size(); ++i)
                            {
                                cgltf_uint j4[4] = { 0, 0, 0, 0 };
                                float      w4[4] = { 0, 0, 0, 0 };
                                cgltf_accessor_read_uint(jAcc, i, j4, 4);
                                cgltf_accessor_read_float(wAcc, i, w4, 4);
                                sm.Skin[i].Joints  = { (float)j4[0], (float)j4[1], (float)j4[2], (float)j4[3] };
                                sm.Skin[i].Weights = { w4[0], w4[1], w4[2], w4[3] };
                            }
                        }
                    }

                    DeriveNormalsIfMissing(d, nrmAcc != nullptr);
                    BakeTransform(d, xform);
                    out.Meshes.push_back(std::move(sm));
                }
            }

            cgltf_free(data);
            if (out.Meshes.empty())
            {
                CS_CORE_ERROR("MeshImport(glTF): '{0}' produced no drawable triangle primitives.", path);
                return false;
            }
            return true;
        }

#ifdef COSMIC_WITH_ASSIMP
        // ------------------------------------------------------------------
        // assimp reader — node-hierarchy traversal (names + per-node transforms
        // baked) instead of aiProcess_PreTransformVertices, so multi-mesh
        // sources keep stable, addressable sub-meshes (the E16 hierarchy spec).
        // ------------------------------------------------------------------

        glm::mat4 ToGlm(const aiMatrix4x4& m)
        {
            // aiMatrix4x4 is row-major; glm is column-major.
            return glm::mat4(
                m.a1, m.b1, m.c1, m.d1,
                m.a2, m.b2, m.c2, m.d2,
                m.a3, m.b3, m.c3, m.d3,
                m.a4, m.b4, m.c4, m.d4);
        }

        MeshData ConvertAiMesh(const aiMesh* m)
        {
            MeshData d;
            d.Vertices.resize(m->mNumVertices);
            for (unsigned vi = 0; vi < m->mNumVertices; ++vi)
            {
                MeshVertex& v = d.Vertices[vi];
                v.Position = { m->mVertices[vi].x, m->mVertices[vi].y, m->mVertices[vi].z };
                if (m->HasNormals())
                    v.Normal = { m->mNormals[vi].x, m->mNormals[vi].y, m->mNormals[vi].z };
                if (m->HasTextureCoords(0))
                    v.TexCoord = { m->mTextureCoords[0][vi].x, m->mTextureCoords[0][vi].y };
                if (m->HasTangentsAndBitangents())
                    v.Tangent = glm::vec4(m->mTangents[vi].x, m->mTangents[vi].y, m->mTangents[vi].z, 1.0f);
            }
            for (unsigned fi = 0; fi < m->mNumFaces; ++fi)
            {
                const aiFace& f = m->mFaces[fi];
                for (unsigned k = 0; k + 2 < f.mNumIndices; ++k)
                {
                    d.Indices.push_back(f.mIndices[0]);
                    d.Indices.push_back(f.mIndices[k + 1]);
                    d.Indices.push_back(f.mIndices[k + 2]);
                }
            }
            DeriveNormalsIfMissing(d, m->HasNormals());
            return d;
        }

        // Per-mesh joint influences (A2): cap at the strongest 4 per vertex,
        // renormalized. `jointOf` maps a bone name to its skeleton index.
        std::vector<SkinVertex> BuildAiSkin(const aiMesh* m,
                                            const std::unordered_map<std::string, int>& jointOf)
        {
            std::vector<SkinVertex> skin(m->mNumVertices);
            std::vector<uint8_t>    count(m->mNumVertices, 0);
            for (unsigned b = 0; b < m->mNumBones; ++b)
            {
                const aiBone* bone = m->mBones[b];
                const auto it = jointOf.find(bone->mName.C_Str());
                if (it == jointOf.end())
                    continue;
                const float joint = (float)it->second;
                for (unsigned w = 0; w < bone->mNumWeights; ++w)
                {
                    const unsigned v      = bone->mWeights[w].mVertexId;
                    const float    weight = bone->mWeights[w].mWeight;
                    if (v >= skin.size() || weight <= 0.0f)
                        continue;
                    SkinVertex& sv = skin[v];
                    if (count[v] < 4)
                    {
                        sv.Joints[count[v]]  = joint;
                        sv.Weights[count[v]] = weight;
                        count[v]++;
                    }
                    else
                    {
                        // Replace the weakest influence if this one is stronger.
                        int weakest = 0;
                        for (int k = 1; k < 4; ++k)
                            if (sv.Weights[k] < sv.Weights[weakest])
                                weakest = k;
                        if (weight > sv.Weights[weakest])
                        {
                            sv.Joints[weakest]  = joint;
                            sv.Weights[weakest] = weight;
                        }
                    }
                }
            }
            return skin;   // weights renormalize in-shader
        }

        void TraverseAiNode(const aiScene* scene, const aiNode* node,
                            const glm::mat4& parentXform, const glm::mat4& importXform,
                            const std::unordered_map<std::string, int>& jointOf,
                            ImportedModelDesc& out)
        {
            const glm::mat4 world = parentXform * ToGlm(node->mTransformation);
            for (unsigned i = 0; i < node->mNumMeshes; ++i)
            {
                const aiMesh* m = scene->mMeshes[node->mMeshes[i]];
                if (!m || m->mNumVertices == 0 || m->mNumFaces == 0)
                    continue;

                ImportedMeshDesc sm;
                if (node->mName.length > 0)
                    sm.Name = node->mName.C_Str();
                else if (m->mName.length > 0)
                    sm.Name = m->mName.C_Str();
                else
                    sm.Name = "Mesh_" + std::to_string(out.Meshes.size());
                // A node holding several meshes gets one child per mesh — keep
                // the names distinct so the spawned entities are tellable apart.
                if (node->mNumMeshes > 1)
                    sm.Name += "_" + std::to_string(i);
                sm.MaterialIndex = (int)m->mMaterialIndex;

                sm.Geometry = ConvertAiMesh(m);

                // Skinned meshes: the joints own the motion — bake only the
                // unit/up-axis matrix (the palette is conjugated by it).
                const bool skinned = m->HasBones() && !jointOf.empty();
                if (skinned)
                    sm.Skin = BuildAiSkin(m, jointOf);
                BakeTransform(sm.Geometry, skinned ? importXform : importXform * world);
                out.Meshes.push_back(std::move(sm));
            }
            for (unsigned c = 0; c < node->mNumChildren; ++c)
                TraverseAiNode(scene, node->mChildren[c], world, importXform, jointOf, out);
        }

        // First texture path of any of the given types (priority order).
        std::string AiTexture(const aiMaterial* mat, std::initializer_list<aiTextureType> types)
        {
            for (aiTextureType t : types)
            {
                aiString str;
                if (mat->GetTexture(t, 0, &str) == aiReturn_SUCCESS && str.length > 0)
                    return str.C_Str();
            }
            return {};
        }

        ImportedMaterialDesc ConvertAiMaterial(const aiMaterial* mat)
        {
            ImportedMaterialDesc md;

            aiString name;
            if (mat->Get(AI_MATKEY_NAME, name) == aiReturn_SUCCESS)
                md.Name = name.C_Str();

            aiColor4D col;
            if (mat->Get(AI_MATKEY_BASE_COLOR, col) == aiReturn_SUCCESS ||
                mat->Get(AI_MATKEY_COLOR_DIFFUSE, col) == aiReturn_SUCCESS)
                md.Albedo = { col.r, col.g, col.b, col.a };

            float opacity = 1.0f;
            if (mat->Get(AI_MATKEY_OPACITY, opacity) == aiReturn_SUCCESS)
            {
                md.Opacity   = opacity;
                md.Albedo.a *= opacity;
            }

            float metallic = 0.0f;
            if (mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == aiReturn_SUCCESS)
                md.Metallic = metallic;

            float roughness = 0.0f;
            if (mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == aiReturn_SUCCESS)
            {
                md.Roughness = roughness;
            }
            else
            {
                // Phong shininess -> Beckmann-ish roughness (the standard fit).
                float shininess = 0.0f;
                if (mat->Get(AI_MATKEY_SHININESS, shininess) == aiReturn_SUCCESS && shininess > 0.0f)
                    md.Roughness = glm::clamp(std::sqrt(2.0f / (2.0f + shininess)), 0.04f, 1.0f);
            }

            aiColor3D em;
            if (mat->Get(AI_MATKEY_COLOR_EMISSIVE, em) == aiReturn_SUCCESS)
                md.Emissive = { em.r, em.g, em.b };

            md.AlbedoMap     = AiTexture(mat, { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE });
            md.NormalMap     = AiTexture(mat, { aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA,
                                                aiTextureType_HEIGHT });   // OBJ "bump" arrives as HEIGHT
            md.MetalRoughMap = AiTexture(mat, { aiTextureType_METALNESS, aiTextureType_DIFFUSE_ROUGHNESS,
                                                aiTextureType_UNKNOWN });
            md.AOMap         = AiTexture(mat, { aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP });
            md.EmissiveMap   = AiTexture(mat, { aiTextureType_EMISSIVE });
            return md;
        }

        bool ReadAssimp(ImportedModelDesc& out, const std::string& path, const ImportSettings& s)
        {
            Assimp::Importer importer;
            unsigned flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                             aiProcess_CalcTangentSpace;
            if (s.GenerateNormals)
                flags |= aiProcess_GenSmoothNormals;
            if (s.FlipUVs)
                flags |= aiProcess_FlipUVs;
            const aiScene* scene = importer.ReadFile(path, flags);
            if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
            {
                CS_CORE_ERROR("MeshImport(assimp): '{0}': {1}", path, importer.GetErrorString());
                return false;
            }

            for (unsigned mi = 0; mi < scene->mNumMaterials; ++mi)
                out.Materials.push_back(ConvertAiMaterial(scene->mMaterials[mi]));

            for (unsigned ti = 0; ti < scene->mNumTextures; ++ti)
            {
                const aiTexture* t = scene->mTextures[ti];
                ImportedTextureDesc td;
                td.Name = (t->mFilename.length > 0)
                              ? std::string(t->mFilename.C_Str()).substr(
                                    std::string(t->mFilename.C_Str()).find_last_of("/\\") + 1)
                              : "embedded_" + std::to_string(ti);
                // Strip any extension from the suggested stem; FormatHint carries it.
                if (const size_t dot = td.Name.find_last_of('.'); dot != std::string::npos)
                    td.Name = td.Name.substr(0, dot);
                if (t->mHeight == 0)
                {
                    // Compressed: pcData is the raw image file (achFormatHint = ext).
                    td.FormatHint = t->achFormatHint[0] ? t->achFormatHint : "png";
                    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(t->pcData);
                    td.Bytes.assign(bytes, bytes + t->mWidth);
                }
                else
                {
                    // Uncompressed BGRA texels -> raw RGBA bytes.
                    td.Width  = t->mWidth;
                    td.Height = t->mHeight;
                    td.Bytes.resize((size_t)t->mWidth * t->mHeight * 4);
                    for (size_t i = 0; i < (size_t)t->mWidth * t->mHeight; ++i)
                    {
                        td.Bytes[i * 4 + 0] = t->pcData[i].r;
                        td.Bytes[i * 4 + 1] = t->pcData[i].g;
                        td.Bytes[i * 4 + 2] = t->pcData[i].b;
                        td.Bytes[i * 4 + 3] = t->pcData[i].a;
                    }
                }
                out.EmbeddedTextures.push_back(std::move(td));
            }

            // --- Bones -> Skeleton (A2): the closure of every mesh's bone nodes
            // plus their ancestors, in hierarchy pre-order (parents first). ---
            std::unordered_map<std::string, int> jointOf;
            {
                std::unordered_map<const aiNode*, bool> needed;
                std::function<void(const aiNode*)> markUp = [&](const aiNode* n)
                {
                    while (n && !needed[n])
                    {
                        needed[n] = true;
                        n = n->mParent;
                    }
                };
                std::unordered_map<std::string, const aiNode*> byName;
                std::function<void(const aiNode*)> index = [&](const aiNode* n)
                {
                    byName[n->mName.C_Str()] = n;
                    for (unsigned c = 0; c < n->mNumChildren; ++c)
                        index(n->mChildren[c]);
                };
                index(scene->mRootNode);

                for (unsigned mi = 0; mi < scene->mNumMeshes; ++mi)
                    for (unsigned b = 0; b < scene->mMeshes[mi]->mNumBones; ++b)
                        if (auto it = byName.find(scene->mMeshes[mi]->mBones[b]->mName.C_Str());
                            it != byName.end())
                            markUp(it->second);

                if (!needed.empty())
                {
                    out.Bones.ImportCorrection    = ImportTransform(s);
                    out.Bones.ImportCorrectionInv = glm::inverse(out.Bones.ImportCorrection);

                    std::unordered_map<const aiNode*, int> indexOf;
                    std::function<void(const aiNode*)> build = [&](const aiNode* n)
                    {
                        if (needed.count(n) && needed.at(n))
                        {
                            SkeletonJoint joint;
                            joint.Name      = n->mName.C_Str();
                            joint.LocalBind = ToGlm(n->mTransformation);
                            joint.Parent    = (n->mParent && indexOf.count(n->mParent))
                                                  ? indexOf.at(n->mParent) : -1;
                            indexOf[n] = (int)out.Bones.Joints.size();
                            jointOf[joint.Name] = indexOf[n];
                            out.Bones.Joints.push_back(std::move(joint));
                        }
                        for (unsigned c = 0; c < n->mNumChildren; ++c)
                            build(n->mChildren[c]);
                    };
                    build(scene->mRootNode);

                    // Inverse binds from the bones themselves (structural joints
                    // that no vertex references keep identity — never sampled).
                    for (unsigned mi = 0; mi < scene->mNumMeshes; ++mi)
                        for (unsigned b = 0; b < scene->mMeshes[mi]->mNumBones; ++b)
                        {
                            const aiBone* bone = scene->mMeshes[mi]->mBones[b];
                            if (auto it = jointOf.find(bone->mName.C_Str()); it != jointOf.end())
                                out.Bones.Joints[(size_t)it->second].InverseBind = ToGlm(bone->mOffsetMatrix);
                        }
                }
            }

            // --- Animations -> Clips (A2). Key times arrive in ticks. ---
            for (unsigned ai = 0; ai < scene->mNumAnimations; ++ai)
            {
                const aiAnimation* anim = scene->mAnimations[ai];
                const double ticks = anim->mTicksPerSecond > 0.0 ? anim->mTicksPerSecond : 25.0;

                AnimationClip clip;
                clip.Name = anim->mName.length > 0 ? anim->mName.C_Str()
                                                   : "Clip_" + std::to_string(ai);
                clip.Duration = (float)(anim->mDuration / ticks);

                for (unsigned ci = 0; ci < anim->mNumChannels; ++ci)
                {
                    const aiNodeAnim* na = anim->mChannels[ci];
                    const auto it = jointOf.find(na->mNodeName.C_Str());
                    if (it == jointOf.end())
                        continue;   // animation on a non-joint node — v1 ignores

                    AnimationChannel c;
                    c.JointIndex = it->second;
                    for (unsigned k = 0; k < na->mNumPositionKeys; ++k)
                    {
                        c.PosTimes.push_back((float)(na->mPositionKeys[k].mTime / ticks));
                        const aiVector3D& v = na->mPositionKeys[k].mValue;
                        c.PosValues.push_back({ v.x, v.y, v.z });
                    }
                    for (unsigned k = 0; k < na->mNumRotationKeys; ++k)
                    {
                        c.RotTimes.push_back((float)(na->mRotationKeys[k].mTime / ticks));
                        const aiQuaternion& q = na->mRotationKeys[k].mValue;
                        c.RotValues.push_back(glm::quat(q.w, q.x, q.y, q.z));
                    }
                    for (unsigned k = 0; k < na->mNumScalingKeys; ++k)
                    {
                        c.SclTimes.push_back((float)(na->mScalingKeys[k].mTime / ticks));
                        const aiVector3D& v = na->mScalingKeys[k].mValue;
                        c.SclValues.push_back({ v.x, v.y, v.z });
                    }
                    clip.Channels.push_back(std::move(c));
                }
                if (!clip.Channels.empty())
                    out.Clips.push_back(std::move(clip));
            }

            TraverseAiNode(scene, scene->mRootNode, glm::mat4(1.0f), ImportTransform(s), jointOf, out);
            if (out.Meshes.empty())
            {
                CS_CORE_ERROR("MeshImport(assimp): '{0}' produced no drawable meshes.", path);
                return false;
            }
            return true;
        }
#endif // COSMIC_WITH_ASSIMP

        // OBJ via the engine's own parser: the pre-A1 single-mesh path, kept
        // bit-exact so every existing .cmeta scene loads identically.
        MeshData ImportObjEngineParser(const std::string& path, const ImportSettings& s)
        {
            MeshData data = Mesh::BuildFromOBJ(path);
            if (data.Vertices.empty())
                return data;   // the parser already logged why
            if (s.FlipUVs)
                for (MeshVertex& v : data.Vertices)
                    v.TexCoord.y = 1.0f - v.TexCoord.y;
            data.ApplyTransform(ImportTransform(s));
            return data;
        }
    }

    bool MeshImport::ImportModelData(ImportedModelDesc& out, const std::string& resolvedSourcePath,
                                     const ImportSettings& settings)
    {
        out = {};
        const std::string ext = Extension(resolvedSourcePath);

        if (ext == "gltf" || ext == "glb")
            return ReadGltf(out, resolvedSourcePath, settings);

#ifdef COSMIC_WITH_ASSIMP
        if (ext == "obj" || ext == "fbx" || ext == "stl" || ext == "dae" || ext == "ply")
            return ReadAssimp(out, resolvedSourcePath, settings);
#else
        if (ext == "obj")
        {
            // No assimp: describe the OBJ as one material-less mesh.
            ImportedMeshDesc sm;
            sm.Name     = resolvedSourcePath.substr(resolvedSourcePath.find_last_of("/\\") + 1);
            sm.Geometry = ImportObjEngineParser(resolvedSourcePath, settings);
            if (sm.Geometry.Vertices.empty())
                return false;
            out.Meshes.push_back(std::move(sm));
            return true;
        }
#endif

        CS_CORE_ERROR("MeshImport: unsupported model format '.{0}' ('{1}').", ext, resolvedSourcePath);
        return false;
    }

    MeshData MeshImport::ImportData(const std::string& resolvedSourcePath, const ImportSettings& settings,
                                    int submeshIndex)
    {
        const std::string ext = Extension(resolvedSourcePath);

        // The compat path: a plain (fragment-less) OBJ stays on the engine's
        // own parser — pre-A1 scenes must keep loading byte-identically.
        if (ext == "obj" && submeshIndex < 0)
            return ImportObjEngineParser(resolvedSourcePath, settings);

        ImportedModelDesc desc;
        if (!ImportModelData(desc, resolvedSourcePath, settings))
            return {};

        if (submeshIndex < 0)
            return MergeMeshes(desc);

        if (submeshIndex >= (int)desc.Meshes.size())
        {
            CS_CORE_ERROR("MeshImport: '{0}' has {1} sub-mesh(es); #{2} does not exist.",
                          resolvedSourcePath, desc.Meshes.size(), submeshIndex);
            return {};
        }
        return std::move(desc.Meshes[(size_t)submeshIndex].Geometry);
    }

    Ref<Mesh> MeshImport::Import(const std::string& resolvedSourcePath, const ImportSettings& settings,
                                 int submeshIndex)
    {
        const std::string ext = Extension(resolvedSourcePath);
        if (!Supports(ext))
        {
            CS_CORE_ERROR("MeshImport: unsupported model format '.{0}' ('{1}').", ext, resolvedSourcePath);
            return nullptr;
        }

        // The compat path: a plain (fragment-less) OBJ stays on the engine's
        // own parser (see ImportData) — and OBJ never carries skins.
        if (ext == "obj" && submeshIndex < 0)
        {
            const MeshData data = ImportData(resolvedSourcePath, settings, submeshIndex);
            return data.Vertices.empty() ? nullptr : Mesh::Create(data);
        }

        // Everything else goes through the rich description so skinned sources
        // (A2) upload with their joints/weights + skeleton attached.
        ImportedModelDesc desc;
        if (!ImportModelData(desc, resolvedSourcePath, settings))
            return nullptr;

        MeshData                data;
        std::vector<SkinVertex> skin;
        if (submeshIndex >= 0)
        {
            if (submeshIndex >= (int)desc.Meshes.size())
            {
                CS_CORE_ERROR("MeshImport: '{0}' has {1} sub-mesh(es); #{2} does not exist.",
                              resolvedSourcePath, desc.Meshes.size(), submeshIndex);
                return nullptr;
            }
            data = std::move(desc.Meshes[(size_t)submeshIndex].Geometry);
            skin = std::move(desc.Meshes[(size_t)submeshIndex].Skin);
        }
        else
        {
            // Merged: a skin survives only when EVERY sub-mesh is skinned —
            // mixing static parts into one skinned VB would give them bogus
            // joint-0 influences (a mixed file imports per-sub-mesh instead).
            bool allSkinned = !desc.Meshes.empty();
            for (const ImportedMeshDesc& sm : desc.Meshes)
                if (sm.Skin.size() != sm.Geometry.Vertices.size())
                {
                    allSkinned = false;
                    break;
                }

            for (ImportedMeshDesc& sm : desc.Meshes)
            {
                const uint32_t base = (uint32_t)data.Vertices.size();
                data.Vertices.insert(data.Vertices.end(),
                                     sm.Geometry.Vertices.begin(), sm.Geometry.Vertices.end());
                for (uint32_t idx : sm.Geometry.Indices)
                    data.Indices.push_back(base + idx);
                if (allSkinned)
                    skin.insert(skin.end(), sm.Skin.begin(), sm.Skin.end());
            }
        }

        if (data.Vertices.empty())
            return nullptr;   // the loader already logged why

        if (!skin.empty() && desc.Bones.JointCount() > 0)
            return Mesh::CreateSkinned(data, skin, std::make_shared<Skeleton>(std::move(desc.Bones)));
        return Mesh::Create(data);
    }
}
