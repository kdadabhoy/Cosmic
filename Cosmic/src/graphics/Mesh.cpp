// Mesh.cpp
// Last Modified: 7/1/2026

#include "graphics/Mesh.h"
#include "graphics/Buffer.h"
#include "core/Log.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////
	// Tangent generation (S6.2)
	/////////////////////////////////////////////////////////////////////////////////

	namespace
	{
		// Compute a per-vertex tangent basis from positions, UVs and normals using
		// the standard accumulate-per-triangle then Gram-Schmidt approach (the
		// spirit of MikkTSpace without the full spec). Meshes with no meaningful
		// UVs (all-zero, degenerate parameterisation) fall back to an arbitrary
		// perpendicular so the TBN stays well-formed. Writes MeshVertex::Tangent
		// (xyz = tangent aligned to +U, w = bitangent handedness sign).
		void ComputeTangents(std::vector<MeshVertex>& v, const std::vector<uint32_t>& idx)
		{
			std::vector<glm::vec3> tan(v.size(), glm::vec3(0.0f));
			std::vector<glm::vec3> bit(v.size(), glm::vec3(0.0f));

			for (size_t i = 0; i + 2 < idx.size(); i += 3)
			{
				const uint32_t i0 = idx[i], i1 = idx[i + 1], i2 = idx[i + 2];

				const glm::vec3 e1 = v[i1].Position - v[i0].Position;
				const glm::vec3 e2 = v[i2].Position - v[i0].Position;
				const glm::vec2 d1 = v[i1].TexCoord - v[i0].TexCoord;
				const glm::vec2 d2 = v[i2].TexCoord - v[i0].TexCoord;

				const float denom = d1.x * d2.y - d2.x * d1.y;
				if (std::fabs(denom) < 1e-9f)
					continue;   // degenerate UVs on this triangle — handled by the fallback below
				const float r = 1.0f / denom;

				const glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r;
				const glm::vec3 b = (e2 * d1.x - e1 * d2.x) * r;

				tan[i0] += t; tan[i1] += t; tan[i2] += t;
				bit[i0] += b; bit[i1] += b; bit[i2] += b;
			}

			for (size_t i = 0; i < v.size(); ++i)
			{
				const glm::vec3 n = v[i].Normal;
				glm::vec3 t = tan[i] - n * glm::dot(n, tan[i]);   // Gram-Schmidt orthogonalise

				if (glm::dot(t, t) < 1e-12f)
				{
					// No usable tangent (flat UVs / isolated vertex): pick any axis
					// not parallel to the normal and build a perpendicular.
					const glm::vec3 axis = std::fabs(n.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
					t = glm::normalize(glm::cross(axis, n));
					v[i].Tangent = glm::vec4(t, 1.0f);
					continue;
				}

				t = glm::normalize(t);
				const float w = (glm::dot(glm::cross(n, t), bit[i]) < 0.0f) ? -1.0f : 1.0f;
				v[i].Tangent = glm::vec4(t, w);
			}
		}
	}

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

		// Generate the tangent basis (S6.2) into a working copy — every producer
		// (primitives / OBJ / glTF) gets a consistent TBN for normal mapping.
		std::vector<MeshVertex> verts = vertices;
		ComputeTangents(verts, indices);

		m_VertexArray = VertexArray::Create();

		// Static geometry: upload once through the data-constructor path.
		auto vertexBuffer = VertexBuffer::Create(
			reinterpret_cast<float*>(verts.data()),
			static_cast<uint32_t>(verts.size() * sizeof(MeshVertex)));

		// THE canonical mesh layout — every mesh shader declares exactly this.
		vertexBuffer->SetLayout({
			{ ShaderDataType::Float3, "a_Position" },
			{ ShaderDataType::Float3, "a_Normal"   },
			{ ShaderDataType::Float2, "a_TexCoord" },
			{ ShaderDataType::Float4, "a_Tangent"  }
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

	Ref<Mesh> Mesh::Create(const MeshData& data)
	{
		Ref<Mesh> mesh = Create(data.Vertices, data.Indices);
		if (mesh)
			mesh->m_Submeshes = data.Submeshes;   // M5 — per-material ranges (empty = single)
		return mesh;
	}

	uint32_t Mesh::GetMaterialSlotCount() const
	{
		uint32_t slots = 0;
		for (const Submesh& s : m_Submeshes)
			slots = std::max(slots, s.MaterialIndex + 1);
		return slots;
	}

	Ref<Mesh> Mesh::CreateSkinned(const MeshData& data, const std::vector<SkinVertex>& skin,
	                              const Ref<Skeleton>& skeleton)
	{
		Ref<Mesh> mesh = Create(data.Vertices, data.Indices);
		if (!mesh)
			return nullptr;

		mesh->m_Submeshes = data.Submeshes;   // M5 — per-material ranges (empty = single)

		if (!skeleton || skin.size() != data.Vertices.size())
		{
			CS_CORE_WARN("Mesh::CreateSkinned: {0} skin vertices for {1} positions (skeleton {2}) — "
			             "uploading as a static mesh.",
			             skin.size(), data.Vertices.size(), skeleton ? "set" : "null");
			return mesh;   // static fallback keeps the asset usable
		}

		// Second vertex buffer: joints/weights at attribute locations 4/5 —
		// the additive layout extension the canonical-layout contract allows
		// (static shaders never read locations 4/5).
		auto skinBuffer = VertexBuffer::Create(
			reinterpret_cast<float*>(const_cast<SkinVertex*>(skin.data())),
			static_cast<uint32_t>(skin.size() * sizeof(SkinVertex)));
		skinBuffer->SetLayout({
			{ ShaderDataType::Float4, "a_Joints"  },
			{ ShaderDataType::Float4, "a_Weights" }
			});
		mesh->m_VertexArray->AddVertexBuffer(skinBuffer);
		mesh->m_Skeleton = skeleton;
		return mesh;
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Primitives — pure Build* geometry (GL-free), thin Create* uploaders (E15)
	/////////////////////////////////////////////////////////////////////////////////

	Ref<Mesh> Mesh::CreateBox(const glm::vec3& size)       { return Create(BuildBox(size)); }
	Ref<Mesh> Mesh::CreatePlane(float w, float d)          { return Create(BuildPlane(w, d)); }
	Ref<Mesh> Mesh::CreateCylinder(float r, float h, uint32_t s) { return Create(BuildCylinder(r, h, s)); }
	Ref<Mesh> Mesh::CreateCone(float r, float h, uint32_t s)     { return Create(BuildCone(r, h, s)); }
	Ref<Mesh> Mesh::CreateUVSphere(float r, uint32_t ri, uint32_t s) { return Create(BuildUVSphere(r, ri, s)); }
	Ref<Mesh> Mesh::CreateTorus(float r, float tr, uint32_t s, uint32_t si) { return Create(BuildTorus(r, tr, s, si)); }

	MeshData Mesh::BuildBox(const glm::vec3& size)
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

		return { std::move(vertices), std::move(indices) };
	}

	MeshData Mesh::BuildPlane(float width, float depth)
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

		return { std::move(vertices), std::move(indices) };
	}

	MeshData Mesh::BuildCylinder(float radius, float height, uint32_t segments)
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

		return { std::move(vertices), std::move(indices) };
	}

	MeshData Mesh::BuildCone(float radius, float height, uint32_t segments)
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

		return { std::move(vertices), std::move(indices) };
	}

	MeshData Mesh::BuildUVSphere(float radius, uint32_t rings, uint32_t segments)
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

		return { std::move(vertices), std::move(indices) };
	}

	MeshData Mesh::BuildTorus(float radius, float tubeRadius, uint32_t segments, uint32_t sides)
	{
		segments = segments < 3 ? 3 : segments;   // steps around the ring (major circle)
		sides    = sides    < 3 ? 3 : sides;      // steps around the tube (minor circle)
		const float tau = glm::two_pi<float>();

		std::vector<MeshVertex> vertices;
		std::vector<uint32_t>   indices;
		vertices.reserve((segments + 1) * (sides + 1));

		// u sweeps the ring in the XZ plane; v sweeps the tube cross-section. The
		// tube centre at angle u is C(u) = radius*(cos u, 0, sin u); a surface point
		// is C(u) + tubeRadius*(cos v * cos u, sin v, cos v * sin u). The normal is
		// the unit tube-radial (cos v cos u, sin v, cos v sin u) — it points straight
		// out from the centre circle, which is what BuildTorus's unit test checks.
		for (uint32_t i = 0; i <= segments; ++i)
		{
			const float u  = tau * static_cast<float>(i) / static_cast<float>(segments);
			const float cu = std::cos(u), su = std::sin(u);

			for (uint32_t j = 0; j <= sides; ++j)
			{
				const float v  = tau * static_cast<float>(j) / static_cast<float>(sides);
				const float cv = std::cos(v), sv = std::sin(v);

				const glm::vec3 normal(cv * cu, sv, cv * su);
				const glm::vec3 pos((radius + tubeRadius * cv) * cu,
				                    tubeRadius * sv,
				                    (radius + tubeRadius * cv) * su);

				vertices.push_back({ pos, normal,
				                     { static_cast<float>(i) / static_cast<float>(segments),
				                       static_cast<float>(j) / static_cast<float>(sides) } });
			}
		}

		const uint32_t stride = sides + 1;
		for (uint32_t i = 0; i < segments; ++i)
		{
			for (uint32_t j = 0; j < sides; ++j)
			{
				const uint32_t a = i * stride + j;
				const uint32_t b = a + stride;
				// CCW seen from outside so the outward normals face the viewer.
				indices.insert(indices.end(), { a, a + 1, b,  a + 1, b + 1, b });
			}
		}

		return { std::move(vertices), std::move(indices) };
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

	MeshData Mesh::BuildFromOBJ(const std::string& resolvedPath)
	{
		std::ifstream file(resolvedPath);
		if (!file.is_open())
		{
			CS_CORE_ERROR("Mesh::BuildFromOBJ: cannot open '{0}'.", resolvedPath);
			return {};
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
						CS_CORE_ERROR("Mesh::BuildFromOBJ: malformed face token '{0}' ({1}:{2}).",
							token, resolvedPath, lineNo);
						return {};
					}

					Corner c;
					c.p = ResolveObjIndex(pi, positions.size());
					c.t = (fields & 2) ? ResolveObjIndex(ti, uvs.size())     : SIZE_MAX;
					c.n = (fields & 4) ? ResolveObjIndex(ni, normals.size()) : SIZE_MAX;

					if (c.p == SIZE_MAX)
					{
						CS_CORE_ERROR("Mesh::BuildFromOBJ: position index out of range ({0}:{1}).",
							resolvedPath, lineNo);
						return {};
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
			CS_CORE_ERROR("Mesh::BuildFromOBJ: '{0}' contained no triangle geometry.", resolvedPath);
			return {};
		}

		CS_CORE_INFO("Mesh::BuildFromOBJ: '{0}' -> {1} vertices, {2} triangles.",
			resolvedPath, vertices.size(), indices.size() / 3);

		return { std::move(vertices), std::move(indices) };
	}

	Ref<Mesh> Mesh::CreateFromOBJ(const std::string& resolvedPath)
	{
		MeshData data = BuildFromOBJ(resolvedPath);
		if (data.Vertices.empty())
			return nullptr;   // BuildFromOBJ already logged the reason
		return Create(data);
	}

	/////////////////////////////////////////////////////////////////////////////////
}
