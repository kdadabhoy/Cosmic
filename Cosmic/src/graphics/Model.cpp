// Model.cpp — S4.4b glTF 2.0 import via cgltf. See Model.h.

#include "graphics/Model.h"
#include "core/Log.h"

#include "cgltf.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <utility>
#include <vector>

namespace Cosmic
{
	namespace
	{
		// Find a primitive attribute of a given type (texcoord uses set index 0).
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

		// Convert one triangle primitive into an engine Mesh, baking `world` into
		// positions and `normalMat` into normals. Returns nullptr on unusable data.
		Ref<Mesh> BuildPrimitive(const cgltf_primitive& prim,
		                         const glm::mat4& world, const glm::mat3& normalMat)
		{
			const cgltf_accessor* posAcc = FindAttribute(prim, cgltf_attribute_type_position);
			if (!posAcc || posAcc->count == 0)
				return nullptr;

			const cgltf_accessor* nrmAcc = FindAttribute(prim, cgltf_attribute_type_normal);
			const cgltf_accessor* uvAcc  = FindAttribute(prim, cgltf_attribute_type_texcoord, 0);

			const size_t vertexCount = static_cast<size_t>(posAcc->count);

			std::vector<MeshVertex> vertices(vertexCount);
			for (size_t i = 0; i < vertexCount; ++i)
			{
				float p[3] = { 0.0f, 0.0f, 0.0f };
				cgltf_accessor_read_float(posAcc, i, p, 3);
				const glm::vec3 worldPos = glm::vec3(world * glm::vec4(p[0], p[1], p[2], 1.0f));
				vertices[i].Position = worldPos;

				if (nrmAcc)
				{
					float n[3] = { 0.0f, 1.0f, 0.0f };
					cgltf_accessor_read_float(nrmAcc, i, n, 3);
					vertices[i].Normal = glm::normalize(normalMat * glm::vec3(n[0], n[1], n[2]));
				}
				else
				{
					vertices[i].Normal = glm::vec3(0.0f); // filled below (flat/accumulated fallback)
				}

				if (uvAcc)
				{
					// UVs pass through as-authored (glTF: origin top-left). No
					// textures are imported yet — when S6.2 adds them, decide the
					// V-flip convention against stb_image's flip-on-load there.
					float t[2] = { 0.0f, 0.0f };
					cgltf_accessor_read_float(uvAcc, i, t, 2);
					vertices[i].TexCoord = glm::vec2(t[0], t[1]);
				}
				else
				{
					vertices[i].TexCoord = glm::vec2(0.0f);
				}
			}

			// Indices: explicit accessor, or identity 0..n-1 if absent.
			std::vector<uint32_t> indices;
			if (prim.indices && prim.indices->count > 0)
			{
				indices.resize(static_cast<size_t>(prim.indices->count));
				for (size_t k = 0; k < indices.size(); ++k)
					indices[k] = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, k));
			}
			else
			{
				indices.resize(vertexCount);
				for (size_t k = 0; k < vertexCount; ++k)
					indices[k] = static_cast<uint32_t>(k);
			}

			// A mirrored node transform (negative determinant) reverses triangle
			// winding when baked into the positions — swap two indices per triangle
			// so the mesh stays CCW-front-facing (matters once culling is enabled;
			// inverseTranspose already handles the normals).
			if (glm::determinant(glm::mat3(world)) < 0.0f)
			{
				for (size_t k = 0; k + 2 < indices.size(); k += 3)
					std::swap(indices[k + 1], indices[k + 2]);
			}

			// No normals in the file → derive them from the triangles (accumulate
			// adjacent face normals, then normalize — the same spirit as
			// Mesh::CreateFromOBJ's flat-normal fallback; Duck ships normals so
			// this path is not hit by the acceptance model).
			if (!nrmAcc)
			{
				for (size_t k = 0; k + 2 < indices.size(); k += 3)
				{
					const uint32_t i0 = indices[k], i1 = indices[k + 1], i2 = indices[k + 2];
					const glm::vec3& a = vertices[i0].Position;
					const glm::vec3& b = vertices[i1].Position;
					const glm::vec3& c = vertices[i2].Position;
					const glm::vec3 fn = glm::cross(b - a, c - a);
					vertices[i0].Normal += fn;
					vertices[i1].Normal += fn;
					vertices[i2].Normal += fn;
				}
				for (auto& v : vertices)
				{
					if (glm::dot(v.Normal, v.Normal) > 1e-12f)
						v.Normal = glm::normalize(v.Normal);
					else
						v.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
				}
			}

			return Mesh::Create(vertices, indices);
		}
	}

	Ref<Model> Model::CreateFromGLTF(const std::string& resolvedPath)
	{
		cgltf_options options{};
		cgltf_data*   data = nullptr;

		if (cgltf_parse_file(&options, resolvedPath.c_str(), &data) != cgltf_result_success)
		{
			CS_CORE_ERROR("Model: cgltf failed to parse '{}'", resolvedPath);
			return nullptr;
		}

		// Loads .glb chunk data, external .bin files, and base64 data URIs.
		if (cgltf_load_buffers(&options, data, resolvedPath.c_str()) != cgltf_result_success)
		{
			CS_CORE_ERROR("Model: cgltf failed to load buffers for '{}'", resolvedPath);
			cgltf_free(data);
			return nullptr;
		}

		auto model = std::make_shared<Model>();

		// Import the DEFAULT SCENE's node graph (roots + all descendants) — nodes
		// outside it (other scenes, orphans) are authoring data, not content. Files
		// with no scene fall back to every node. cgltf_node_transform_world folds
		// the full parent hierarchy into each node's matrix either way.
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

		for (const cgltf_node* nodePtr : importNodes)
		{
			const cgltf_node& node = *nodePtr;
			if (!node.mesh)
				continue;

			float worldArr[16];
			cgltf_node_transform_world(&node, worldArr);
			const glm::mat4 world     = glm::make_mat4(worldArr);
			const glm::mat3 normalMat = glm::inverseTranspose(glm::mat3(world));

			for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p)
			{
				const cgltf_primitive& prim = node.mesh->primitives[p];
				if (prim.type != cgltf_primitive_type_triangles)
				{
					CS_CORE_WARN("Model: skipping non-triangle primitive in '{}'", resolvedPath);
					continue;
				}

				Ref<Mesh> mesh = BuildPrimitive(prim, world, normalMat);
				if (!mesh)
					continue;

				ModelPart part;
				part.Geometry = mesh;
				part.Name     = node.mesh->name ? node.mesh->name : "";

				if (prim.material && prim.material->has_pbr_metallic_roughness)
				{
					const cgltf_float* bc = prim.material->pbr_metallic_roughness.base_color_factor;
					part.BaseColor = glm::vec4(bc[0], bc[1], bc[2], bc[3]);
				}
				// else default white.

				model->m_Parts.push_back(std::move(part));
			}
		}

		cgltf_free(data);

		if (model->m_Parts.empty())
		{
			CS_CORE_ERROR("Model: '{}' produced no drawable triangle primitives.", resolvedPath);
			return nullptr;
		}

		CS_CORE_INFO("Model: loaded '{}' ({} part(s)).", resolvedPath, model->m_Parts.size());
		return model;
	}
}
