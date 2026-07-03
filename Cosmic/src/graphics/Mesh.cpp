// Mesh.cpp
// Last Modified: 7/1/2026

#include "graphics/Mesh.h"
#include "graphics/Buffer.h"
#include "core/Log.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////
	// GPU Upload
	/////////////////////////////////////////////////////////////////////////////////

	Mesh::Mesh(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices)
		: m_VertexCount(static_cast<uint32_t>(vertices.size()))
		, m_IndexCount(static_cast<uint32_t>(indices.size()))
	{
		// Local-space AABB over the vertex positions (S5.2 frame-to-fit, S5.4
		// selection/picking, S12 culling). Kept CPU-side — cheap at construction,
		// and the vertex data is not otherwise retained after upload.
		if (!vertices.empty())
		{
			m_LocalMin = m_LocalMax = vertices[0].Position;
			for (const MeshVertex& v : vertices)
			{
				m_LocalMin = glm::min(m_LocalMin, v.Position);
				m_LocalMax = glm::max(m_LocalMax, v.Position);
			}
		}

		m_VertexArray = VertexArray::Create();

		// Static geometry: upload once through the data-constructor path.
		auto vertexBuffer = VertexBuffer::Create(
			const_cast<float*>(reinterpret_cast<const float*>(vertices.data())),
			static_cast<uint32_t>(vertices.size() * sizeof(MeshVertex)));

		// THE canonical mesh layout — every mesh shader declares exactly this.
		vertexBuffer->SetLayout({
			{ ShaderDataType::Float3, "a_Position" },
			{ ShaderDataType::Float3, "a_Normal"   },
			{ ShaderDataType::Float2, "a_TexCoord" }
			});
		m_VertexArray->AddVertexBuffer(vertexBuffer);

		auto indexBuffer = IndexBuffer::Create(
			const_cast<uint32_t*>(indices.data()),
			static_cast<uint32_t>(indices.size()));
		m_VertexArray->SetIndexBuffer(indexBuffer);
	}

	Ref<Mesh> Mesh::Create(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices)
	{
		if (vertices.empty() || indices.empty())
		{
			CS_CORE_ERROR("Mesh::Create called with empty vertex or index data.");
			return nullptr;
		}
		if (indices.size() % 3 != 0)
			CS_CORE_WARN("Mesh::Create: index count {0} is not a multiple of 3.", indices.size());

		return Ref<Mesh>(new Mesh(vertices, indices));
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Primitives
	/////////////////////////////////////////////////////////////////////////////////

	Ref<Mesh> Mesh::CreateBox(const glm::vec3& size)
	{
		const glm::vec3 h = size * 0.5f;

		// 24 vertices (4 per face) so each face gets its own flat normal + UVs.
		const glm::vec3 n[6] = {
			{  0,  0,  1 }, {  0,  0, -1 }, {  1,  0,  0 },
			{ -1,  0,  0 }, {  0,  1,  0 }, {  0, -1,  0 }
		};
		// Per-face tangent/bitangent used to lay out corners CCW from outside.
		const glm::vec3 t[6] = {
			{  1,  0,  0 }, { -1,  0,  0 }, {  0,  0, -1 },
			{  0,  0,  1 }, {  1,  0,  0 }, {  1,  0,  0 }
		};
		const glm::vec3 b[6] = {
			{  0,  1,  0 }, {  0,  1,  0 }, {  0,  1,  0 },
			{  0,  1,  0 }, {  0,  0, -1 }, {  0,  0,  1 }
		};

		std::vector<MeshVertex> vertices;
		std::vector<uint32_t>   indices;
		vertices.reserve(24);
		indices.reserve(36);

		for (int f = 0; f < 6; ++f)
		{
			const glm::vec3 center = n[f] * h;
			const glm::vec3 du     = t[f] * h;
			const glm::vec3 dv     = b[f] * h;

			const uint32_t base = static_cast<uint32_t>(vertices.size());
			vertices.push_back({ center - du - dv, n[f], { 0.0f, 0.0f } });
			vertices.push_back({ center + du - dv, n[f], { 1.0f, 0.0f } });
			vertices.push_back({ center + du + dv, n[f], { 1.0f, 1.0f } });
			vertices.push_back({ center - du + dv, n[f], { 0.0f, 1.0f } });

			indices.insert(indices.end(), { base, base + 1, base + 2,  base, base + 2, base + 3 });
		}

		return Create(vertices, indices);
	}

	Ref<Mesh> Mesh::CreatePlane(float width, float depth)
	{
		const float hw = width * 0.5f;
		const float hd = depth * 0.5f;
		const glm::vec3 up(0.0f, 1.0f, 0.0f);

		// CCW seen from +Y (the visible side of the ground plane).
		std::vector<MeshVertex> vertices = {
			{ { -hw, 0.0f,  hd }, up, { 0.0f, 0.0f } },
			{ {  hw, 0.0f,  hd }, up, { 1.0f, 0.0f } },
			{ {  hw, 0.0f, -hd }, up, { 1.0f, 1.0f } },
			{ { -hw, 0.0f, -hd }, up, { 0.0f, 1.0f } },
		};
		std::vector<uint32_t> indices = { 0, 1, 2,  0, 2, 3 };

		return Create(vertices, indices);
	}

	Ref<Mesh> Mesh::CreateCylinder(float radius, float height, uint32_t segments)
	{
		segments = segments < 3 ? 3 : segments;
		const float hh  = height * 0.5f;
		const float tau = glm::two_pi<float>();

		std::vector<MeshVertex> vertices;
		std::vector<uint32_t>   indices;

		// --- Side wall: duplicated ring verts (seg+1 for the UV seam), smooth normals.
		for (uint32_t i = 0; i <= segments; ++i)
		{
			const float a = tau * static_cast<float>(i) / static_cast<float>(segments);
			const float c = std::cos(a), s = std::sin(a);
			const glm::vec3 normal(c, 0.0f, s);
			const float u = static_cast<float>(i) / static_cast<float>(segments);

			vertices.push_back({ { radius * c, -hh, radius * s }, normal, { u, 0.0f } });
			vertices.push_back({ { radius * c,  hh, radius * s }, normal, { u, 1.0f } });
		}
		for (uint32_t i = 0; i < segments; ++i)
		{
			const uint32_t b0 = i * 2, t0 = i * 2 + 1, b1 = (i + 1) * 2, t1 = (i + 1) * 2 + 1;
			indices.insert(indices.end(), { b0, t1, t0,  b0, b1, t1 });
		}

		// --- Caps: separate fans with flat ±Y normals.
		for (int cap = 0; cap < 2; ++cap)
		{
			const float     y      = cap == 0 ? hh : -hh;
			const glm::vec3 normal = cap == 0 ? glm::vec3(0, 1, 0) : glm::vec3(0, -1, 0);

			const uint32_t centerIdx = static_cast<uint32_t>(vertices.size());
			vertices.push_back({ { 0.0f, y, 0.0f }, normal, { 0.5f, 0.5f } });

			for (uint32_t i = 0; i <= segments; ++i)
			{
				const float a = tau * static_cast<float>(i) / static_cast<float>(segments);
				const float c = std::cos(a), s = std::sin(a);
				vertices.push_back({ { radius * c, y, radius * s }, normal,
				                     { 0.5f + 0.5f * c, 0.5f + 0.5f * s } });
			}
			for (uint32_t i = 0; i < segments; ++i)
			{
				const uint32_t r0 = centerIdx + 1 + i, r1 = centerIdx + 2 + i;
				// Wind CCW as seen from outside the cap (+Y cap from above, -Y from below).
				if (cap == 0) indices.insert(indices.end(), { centerIdx, r1, r0 });
				else          indices.insert(indices.end(), { centerIdx, r0, r1 });
			}
		}

		return Create(vertices, indices);
	}

	Ref<Mesh> Mesh::CreateCone(float radius, float height, uint32_t segments)
	{
		segments = segments < 3 ? 3 : segments;
		const float hh  = height * 0.5f;
		const float tau = glm::two_pi<float>();

		std::vector<MeshVertex> vertices;
		std::vector<uint32_t>   indices;

		// Slant normal: for a cone the outward normal tilts up by atan(radius/height).
		// Per-segment apex vertices keep the fan's normals smooth around the rim.
		const float slantLen = std::sqrt(radius * radius + height * height);
		const float ny       = radius / slantLen;   // vertical component
		const float nr       = height / slantLen;   // radial component

		for (uint32_t i = 0; i <= segments; ++i)
		{
			const float a = tau * static_cast<float>(i) / static_cast<float>(segments);
			const float c = std::cos(a), s = std::sin(a);
			const glm::vec3 normal(nr * c, ny, nr * s);
			const float u = static_cast<float>(i) / static_cast<float>(segments);

			vertices.push_back({ { radius * c, -hh, radius * s }, normal, { u, 0.0f } }); // rim
			vertices.push_back({ { 0.0f,        hh, 0.0f       }, normal, { u, 1.0f } }); // apex
		}
		for (uint32_t i = 0; i < segments; ++i)
		{
			const uint32_t r0 = i * 2, a0 = i * 2 + 1, r1 = (i + 1) * 2;
			indices.insert(indices.end(), { r0, a0, r1 });
		}

		// Base cap (flat -Y).
		const glm::vec3 down(0.0f, -1.0f, 0.0f);
		const uint32_t centerIdx = static_cast<uint32_t>(vertices.size());
		vertices.push_back({ { 0.0f, -hh, 0.0f }, down, { 0.5f, 0.5f } });
		for (uint32_t i = 0; i <= segments; ++i)
		{
			const float a = tau * static_cast<float>(i) / static_cast<float>(segments);
			const float c = std::cos(a), s = std::sin(a);
			vertices.push_back({ { radius * c, -hh, radius * s }, down,
			                     { 0.5f + 0.5f * c, 0.5f + 0.5f * s } });
		}
		for (uint32_t i = 0; i < segments; ++i)
		{
			const uint32_t r0 = centerIdx + 1 + i, r1 = centerIdx + 2 + i;
			indices.insert(indices.end(), { centerIdx, r0, r1 }); // CCW from below
		}

		return Create(vertices, indices);
	}

	Ref<Mesh> Mesh::CreateUVSphere(float radius, uint32_t rings, uint32_t segments)
	{
		rings    = rings    < 3 ? 3 : rings;
		segments = segments < 3 ? 3 : segments;
		const float pi  = glm::pi<float>();
		const float tau = glm::two_pi<float>();

		std::vector<MeshVertex> vertices;
		std::vector<uint32_t>   indices;
		vertices.reserve((rings + 1) * (segments + 1));

		// Latitude bands from +Y pole (ring 0) to -Y pole (ring == rings).
		for (uint32_t r = 0; r <= rings; ++r)
		{
			const float phi = pi * static_cast<float>(r) / static_cast<float>(rings);
			const float y   = std::cos(phi);
			const float rr  = std::sin(phi);

			for (uint32_t s = 0; s <= segments; ++s)
			{
				const float theta = tau * static_cast<float>(s) / static_cast<float>(segments);
				const glm::vec3 dir(rr * std::sin(theta), y, rr * std::cos(theta));
				vertices.push_back({ dir * radius, dir,
				                     { static_cast<float>(s) / static_cast<float>(segments),
				                       1.0f - static_cast<float>(r) / static_cast<float>(rings) } });
			}
		}

		const uint32_t stride = segments + 1;
		for (uint32_t r = 0; r < rings; ++r)
		{
			for (uint32_t s = 0; s < segments; ++s)
			{
				const uint32_t a = r * stride + s;
				const uint32_t b = a + stride;
				// Two triangles per quad; degenerate at the poles is harmless
				// (zero-area triangles) and keeps the indexing uniform.
				indices.insert(indices.end(), { a, b, a + 1,  a + 1, b, b + 1 });
			}
		}

		return Create(vertices, indices);
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Wavefront OBJ Import
	/////////////////////////////////////////////////////////////////////////////////

	namespace
	{
		// OBJ indices are 1-based; negative values are relative to the end of the
		// respective array. Returns SIZE_MAX on out-of-range.
		size_t ResolveObjIndex(long long idx, size_t count)
		{
			if (idx > 0 && static_cast<size_t>(idx) <= count)  return static_cast<size_t>(idx - 1);
			if (idx < 0 && static_cast<size_t>(-idx) <= count) return count - static_cast<size_t>(-idx);
			return SIZE_MAX;
		}
	}

	Ref<Mesh> Mesh::CreateFromOBJ(const std::string& resolvedPath)
	{
		std::ifstream file(resolvedPath);
		if (!file.is_open())
		{
			CS_CORE_ERROR("Mesh::CreateFromOBJ: cannot open '{0}'.", resolvedPath);
			return nullptr;
		}

		std::vector<glm::vec3> positions;
		std::vector<glm::vec3> normals;
		std::vector<glm::vec2> uvs;

		std::vector<MeshVertex> vertices;
		std::vector<uint32_t>   indices;

		// One corner of a face as parsed: indices into the pools (SIZE_MAX = absent).
		struct Corner { size_t p, t, n; };

		std::string line;
		size_t lineNo = 0;
		while (std::getline(file, line))
		{
			++lineNo;
			if (line.empty() || line[0] == '#')
				continue;

			std::istringstream ss(line);
			std::string tag;
			ss >> tag;

			if (tag == "v")
			{
				glm::vec3 p{};
				ss >> p.x >> p.y >> p.z;
				positions.push_back(p);
			}
			else if (tag == "vn")
			{
				glm::vec3 n{};
				ss >> n.x >> n.y >> n.z;
				normals.push_back(n);
			}
			else if (tag == "vt")
			{
				glm::vec2 t{};
				ss >> t.x >> t.y;
				uvs.push_back(t);
			}
			else if (tag == "f")
			{
				// Parse every corner token: v, v/vt, v//vn, or v/vt/vn.
				std::vector<Corner> corners;
				std::string token;
				while (ss >> token)
				{
					long long pi = 0, ti = 0, ni = 0;
					int fields = 0;
					if (token.find("//") != std::string::npos)
						fields = std::sscanf(token.c_str(), "%lld//%lld", &pi, &ni) == 2 ? 5 : 0;
					else if (token.find('/') != std::string::npos)
					{
						if (std::sscanf(token.c_str(), "%lld/%lld/%lld", &pi, &ti, &ni) == 3) fields = 7;
						else if (std::sscanf(token.c_str(), "%lld/%lld", &pi, &ti) == 2)      fields = 3;
					}
					else
						fields = std::sscanf(token.c_str(), "%lld", &pi) == 1 ? 1 : 0;

					if (fields == 0 || pi == 0)
					{
						CS_CORE_ERROR("Mesh::CreateFromOBJ: malformed face token '{0}' ({1}:{2}).",
							token, resolvedPath, lineNo);
						return nullptr;
					}

					Corner c;
					c.p = ResolveObjIndex(pi, positions.size());
					c.t = (fields & 2) ? ResolveObjIndex(ti, uvs.size())     : SIZE_MAX;
					c.n = (fields & 4) ? ResolveObjIndex(ni, normals.size()) : SIZE_MAX;

					if (c.p == SIZE_MAX)
					{
						CS_CORE_ERROR("Mesh::CreateFromOBJ: position index out of range ({0}:{1}).",
							resolvedPath, lineNo);
						return nullptr;
					}
					corners.push_back(c);
				}

				if (corners.size() < 3)
					continue; // degenerate face — skip silently

				// Fan-triangulate. Vertices are emitted per-corner (no dedup) —
				// simple, correct for flat + smooth normals alike, and sim-scale
				// models are small enough that the duplication cost is irrelevant.
				for (size_t i = 1; i + 1 < corners.size(); ++i)
				{
					const Corner tri[3] = { corners[0], corners[i], corners[i + 1] };

					// Faces without vn records get a computed flat normal.
					glm::vec3 flatNormal(0.0f, 1.0f, 0.0f);
					if (tri[0].n == SIZE_MAX || tri[1].n == SIZE_MAX || tri[2].n == SIZE_MAX)
					{
						const glm::vec3 e1 = positions[tri[1].p] - positions[tri[0].p];
						const glm::vec3 e2 = positions[tri[2].p] - positions[tri[0].p];
						const glm::vec3 cr = glm::cross(e1, e2);
						const float len = glm::length(cr);
						if (len > 1e-12f)
							flatNormal = cr / len;
					}

					for (const Corner& c : tri)
					{
						MeshVertex v{};
						v.Position = positions[c.p];
						v.Normal   = c.n != SIZE_MAX ? normals[c.n] : flatNormal;
						v.TexCoord = c.t != SIZE_MAX ? uvs[c.t]     : glm::vec2(0.0f);
						indices.push_back(static_cast<uint32_t>(vertices.size()));
						vertices.push_back(v);
					}
				}
			}
			// Everything else (mtllib/usemtl/o/g/s/l/...) is deliberately ignored.
		}

		if (vertices.empty())
		{
			CS_CORE_ERROR("Mesh::CreateFromOBJ: '{0}' contained no triangle geometry.", resolvedPath);
			return nullptr;
		}

		CS_CORE_INFO("Mesh::CreateFromOBJ: '{0}' -> {1} vertices, {2} triangles.",
			resolvedPath, vertices.size(), indices.size() / 3);

		return Create(vertices, indices);
	}

	/////////////////////////////////////////////////////////////////////////////////
}
