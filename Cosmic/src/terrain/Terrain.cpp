// Terrain.cpp — S8 heightmap terrain: CPU heightfield + quadtree LOD renderer.
// See Terrain.h for the system contract.

#include "terrain/Terrain.h"

#include "renderer/RenderCommand.h"
#include "renderer/Renderer3D.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "math/Noise.h"
#include "core/Log.h"

#include <stb_image.h>

#include <algorithm>
#include <cmath>

namespace Cosmic
{
	namespace
	{
		// Patch tessellation: every quadtree node draws the same 32x32-quad grid,
		// so a node at depth d samples the heightfield with stride (texels/32).
		// Resolution-1 must therefore be 32 * 2^k (validated in Create).
		constexpr int kPatchQuads = 32;
		constexpr int kPatchVerts = kPatchQuads + 1;

		constexpr uint32_t kDetailTexSize = 256;
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Factory + CPU heightfield
	/////////////////////////////////////////////////////////////////////////////////

	Ref<Terrain> Terrain::Create(const TerrainSpecification& spec)
	{
		const uint32_t intervals = spec.Resolution > 1 ? spec.Resolution - 1 : 0;
		const bool sizeOk = intervals >= kPatchQuads &&
		                    intervals % kPatchQuads == 0 &&
		                    ((intervals / kPatchQuads) & ((intervals / kPatchQuads) - 1)) == 0;
		if (!sizeOk || spec.WorldSize <= 0.0f)
		{
			CS_CORE_ERROR("Terrain: bad spec — Resolution must be 32*2^k + 1 (got {}) and WorldSize > 0.",
			              spec.Resolution);
			return nullptr;
		}

		auto terrain = std::make_shared<Terrain>();
		terrain->m_Spec     = spec;
		terrain->m_CellSize = spec.WorldSize / static_cast<float>(intervals);
		terrain->m_MaxDepth = 0;
		for (uint32_t n = intervals / kPatchQuads; n > 1; n >>= 1)
			terrain->m_MaxDepth++;

		if (!terrain->BuildHeights())
			return nullptr;
		terrain->BuildNormals();

		CS_CORE_INFO("Terrain: {}x{} samples over {} m ({} quadtree levels, cell {} m).",
		             spec.Resolution, spec.Resolution, spec.WorldSize,
		             terrain->m_MaxDepth + 1, terrain->m_CellSize);
		return terrain;
	}

	Terrain::~Terrain() = default;

	bool Terrain::BuildHeights()
	{
		const uint32_t res = m_Spec.Resolution;
		m_Heights.assign(static_cast<size_t>(res) * res, 0.0f);

		if (m_Spec.HeightFunction)
		{
			// Source C (F4): an app-supplied deterministic height function. Sampled
			// at the SAME texel centers the fBm path uses (u = i/(res-1)), so
			// SampleHeight's triangle interpolation stays exact and its unit tests
			// apply unchanged. Wins over the image / fBm sources.
			for (uint32_t j = 0; j < res; ++j)
				for (uint32_t i = 0; i < res; ++i)
				{
					const float u = static_cast<float>(i) / (res - 1);
					const float v = static_cast<float>(j) / (res - 1);
					m_Heights[static_cast<size_t>(j) * res + i] =
						std::clamp(m_Spec.HeightFunction(u, v), 0.0f, 1.0f);
				}
		}
		else if (!m_Spec.HeightmapPath.empty())
		{
			// Grayscale image source. stbi_load_16 widens 8-bit files, so 16-bit
			// precision is preserved when present.
			int w = 0, h = 0, comp = 0;
			stbi_us* pixels = stbi_load_16(m_Spec.HeightmapPath.c_str(), &w, &h, &comp, 1);
			if (!pixels || w < 2 || h < 2)
			{
				CS_CORE_ERROR("Terrain: failed to load heightmap '{}'.", m_Spec.HeightmapPath);
				if (pixels)
					stbi_image_free(pixels);
				return false;
			}

			// Bilinear resample the image onto the terrain grid.
			for (uint32_t j = 0; j < res; ++j)
			{
				for (uint32_t i = 0; i < res; ++i)
				{
					const float fx = static_cast<float>(i) / (res - 1) * (w - 1);
					const float fy = static_cast<float>(j) / (res - 1) * (h - 1);
					const int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
					const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
					const float tx = fx - x0, ty = fy - y0;

					const float s00 = pixels[y0 * w + x0], s10 = pixels[y0 * w + x1];
					const float s01 = pixels[y1 * w + x0], s11 = pixels[y1 * w + x1];
					const float s = (s00 * (1 - tx) + s10 * tx) * (1 - ty)
					              + (s01 * (1 - tx) + s11 * tx) * ty;
					m_Heights[static_cast<size_t>(j) * res + i] = s / 65535.0f;
				}
			}
			stbi_image_free(pixels);
		}
		else
		{
			// Procedural fBm source (E14 noise), optional radial island falloff.
			const Noise noise(m_Spec.Seed);
			for (uint32_t j = 0; j < res; ++j)
			{
				for (uint32_t i = 0; i < res; ++i)
				{
					const float u = static_cast<float>(i) / (res - 1);
					const float v = static_cast<float>(j) / (res - 1);
					float s = 0.5f + 0.5f * noise.Fbm2D(u * m_Spec.Frequency, v * m_Spec.Frequency,
					                                    m_Spec.Octaves, m_Spec.Lacunarity, m_Spec.Gain);

					if (m_Spec.EdgeFalloff > 0.0f)
					{
						const float dx = u * 2.0f - 1.0f, dz = v * 2.0f - 1.0f;
						const float r  = std::sqrt(dx * dx + dz * dz);
						const float start = 1.0f - std::clamp(m_Spec.EdgeFalloff, 0.0f, 1.0f);
						const float t = std::clamp((r - start) / std::max(1.0f - start, 1e-4f), 0.0f, 1.0f);
						s *= 1.0f - t * t * (3.0f - 2.0f * t);   // smoothstep down to sea level
					}
					m_Heights[static_cast<size_t>(j) * res + i] = std::clamp(s, 0.0f, 1.0f);
				}
			}
		}

		auto [mn, mx] = std::minmax_element(m_Heights.begin(), m_Heights.end());
		m_MinHeight = m_Spec.BaseHeight + *mn * m_Spec.HeightScale;
		m_MaxHeight = m_Spec.BaseHeight + *mx * m_Spec.HeightScale;
		return true;
	}

	void Terrain::BuildNormals()
	{
		const int res = static_cast<int>(m_Spec.Resolution);
		m_Normals.assign(static_cast<size_t>(res) * res, glm::vec3(0.0f, 1.0f, 0.0f));

		// Central differences over the world-space surface (S8.1: CPU normals at
		// load). Border samples clamp, halving the effective step there.
		for (int j = 0; j < res; ++j)
		{
			for (int i = 0; i < res; ++i)
			{
				const float hl = HeightAtGrid(i - 1, j), hr = HeightAtGrid(i + 1, j);
				const float hd = HeightAtGrid(i, j - 1), hu = HeightAtGrid(i, j + 1);
				const float sx = (i == 0 || i == res - 1) ? m_CellSize : 2.0f * m_CellSize;
				const float sz = (j == 0 || j == res - 1) ? m_CellSize : 2.0f * m_CellSize;
				m_Normals[static_cast<size_t>(j) * res + i] =
					glm::normalize(glm::vec3(-(hr - hl) / sx, 1.0f, -(hu - hd) / sz));
			}
		}
	}

	float Terrain::HeightAtGrid(int i, int j) const
	{
		const int res = static_cast<int>(m_Spec.Resolution);
		i = std::clamp(i, 0, res - 1);
		j = std::clamp(j, 0, res - 1);
		return m_Spec.BaseHeight + m_Heights[static_cast<size_t>(j) * res + i] * m_Spec.HeightScale;
	}

	float Terrain::GetSample(uint32_t i, uint32_t j) const
	{
		const uint32_t res = m_Spec.Resolution;
		if (i >= res || j >= res)
			return 0.0f;
		return m_Heights[static_cast<size_t>(j) * res + i];
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Queries (S8.3)
	/////////////////////////////////////////////////////////////////////////////////

	bool Terrain::Contains(float x, float z) const
	{
		const float half = m_Spec.WorldSize * 0.5f;
		return x >= m_Spec.Origin.x - half && x <= m_Spec.Origin.x + half &&
		       z >= m_Spec.Origin.y - half && z <= m_Spec.Origin.y + half;
	}

	float Terrain::SampleHeight(float x, float z) const
	{
		if (!Contains(x, z) || m_Heights.empty())
			return m_Spec.BaseHeight;

		const float half = m_Spec.WorldSize * 0.5f;
		const float gx = (x - (m_Spec.Origin.x - half)) / m_CellSize;
		const float gz = (z - (m_Spec.Origin.y - half)) / m_CellSize;

		const int res = static_cast<int>(m_Spec.Resolution);
		const int i = std::clamp(static_cast<int>(gx), 0, res - 2);
		const int j = std::clamp(static_cast<int>(gz), 0, res - 2);
		const float fx = std::clamp(gx - i, 0.0f, 1.0f);
		const float fz = std::clamp(gz - j, 0.0f, 1.0f);

		const float h00 = HeightAtGrid(i,     j);
		const float h10 = HeightAtGrid(i + 1, j);
		const float h01 = HeightAtGrid(i,     j + 1);
		const float h11 = HeightAtGrid(i + 1, j + 1);

		// Interpolate on the renderer's triangle split (diagonal (i,j)->(i+1,j+1))
		// so the query matches the full-detail rendered surface exactly, not just
		// at the vertices (the S8.3 acceptance contract).
		if (fx >= fz)
			return h00 + (h10 - h00) * fx + (h11 - h10) * fz;   // lower-right triangle
		else
			return h00 + (h11 - h01) * fx + (h01 - h00) * fz;   // upper-left triangle
	}

	glm::vec3 Terrain::SampleNormal(float x, float z) const
	{
		if (!Contains(x, z) || m_Normals.empty())
			return glm::vec3(0.0f, 1.0f, 0.0f);

		const float half = m_Spec.WorldSize * 0.5f;
		const float gx = (x - (m_Spec.Origin.x - half)) / m_CellSize;
		const float gz = (z - (m_Spec.Origin.y - half)) / m_CellSize;

		const int res = static_cast<int>(m_Spec.Resolution);
		const int i = std::clamp(static_cast<int>(gx), 0, res - 2);
		const int j = std::clamp(static_cast<int>(gz), 0, res - 2);
		const float fx = std::clamp(gx - i, 0.0f, 1.0f);
		const float fz = std::clamp(gz - j, 0.0f, 1.0f);

		const glm::vec3& n00 = m_Normals[static_cast<size_t>(j) * res + i];
		const glm::vec3& n10 = m_Normals[static_cast<size_t>(j) * res + i + 1];
		const glm::vec3& n01 = m_Normals[(static_cast<size_t>(j) + 1) * res + i];
		const glm::vec3& n11 = m_Normals[(static_cast<size_t>(j) + 1) * res + i + 1];

		const glm::vec3 n = (n00 * (1 - fx) + n10 * fx) * (1 - fz)
		                  + (n01 * (1 - fx) + n11 * fx) * fz;
		return glm::normalize(n);
	}

	/////////////////////////////////////////////////////////////////////////////////
	// GPU resources (lazy)
	/////////////////////////////////////////////////////////////////////////////////

	bool Terrain::EnsureGpuResources()
	{
		if (m_GpuReady)
			return true;

		m_Shader = Shader::Create("assets/shaders/Terrain.glsl");
		if (!m_Shader)
		{
			CS_CORE_ERROR("Terrain: Terrain.glsl failed to load — terrain rendering disabled.");
			return false;
		}

		// --- Shared patch mesh: 33x33 surface grid + a perimeter skirt ring.
		// Position = (u, skirtFlag, v): the shader lifts by the fetched height and
		// pushes skirt vertices down by SkirtDepth. ---
		std::vector<MeshVertex> verts;
		std::vector<uint32_t>   idx;
		verts.reserve(kPatchVerts * kPatchVerts + kPatchVerts * 4);

		for (int j = 0; j < kPatchVerts; ++j)
			for (int i = 0; i < kPatchVerts; ++i)
			{
				MeshVertex v{};
				v.Position = { static_cast<float>(i) / kPatchQuads, 0.0f,
				               static_cast<float>(j) / kPatchQuads };
				v.Normal   = { 0.0f, 1.0f, 0.0f };
				v.TexCoord = { v.Position.x, v.Position.z };
				verts.push_back(v);
			}

		auto surfaceIndex = [](int i, int j) { return static_cast<uint32_t>(j * kPatchVerts + i); };
		for (int j = 0; j < kPatchQuads; ++j)
			for (int i = 0; i < kPatchQuads; ++i)
			{
				const uint32_t v00 = surfaceIndex(i, j),     v10 = surfaceIndex(i + 1, j);
				const uint32_t v01 = surfaceIndex(i, j + 1), v11 = surfaceIndex(i + 1, j + 1);
				// Diagonal toward +x+z — MUST match SampleHeight's triangle split.
				idx.insert(idx.end(), { v00, v11, v10 });
				idx.insert(idx.end(), { v00, v01, v11 });
			}

		// Skirt: walk the perimeter, duplicate each vertex with skirtFlag = 1 and
		// stitch quads between the ring and its duplicates.
		std::vector<uint32_t> ring;
		for (int i = 0; i < kPatchVerts; ++i)              ring.push_back(surfaceIndex(i, 0));
		for (int j = 1; j < kPatchVerts; ++j)              ring.push_back(surfaceIndex(kPatchQuads, j));
		for (int i = kPatchQuads - 1; i >= 0; --i)         ring.push_back(surfaceIndex(i, kPatchQuads));
		for (int j = kPatchQuads - 1; j >= 1; --j)         ring.push_back(surfaceIndex(0, j));

		std::vector<uint32_t> ringDup(ring.size());
		for (size_t k = 0; k < ring.size(); ++k)
		{
			MeshVertex v = verts[ring[k]];
			v.Position.y = 1.0f;                           // skirt flag
			ringDup[k] = static_cast<uint32_t>(verts.size());
			verts.push_back(v);
		}
		for (size_t k = 0; k < ring.size(); ++k)
		{
			const size_t n = (k + 1) % ring.size();
			idx.insert(idx.end(), { ring[k], ringDup[k], ringDup[n] });
			idx.insert(idx.end(), { ring[k], ringDup[n], ring[n] });
		}

		m_Patch = Mesh::Create(verts, idx);

		// --- Packed height + normal texture: R,G = 16-bit height (hi, lo byte);
		// B,A = normal.xz * 0.5 + 0.5. texelFetch'd in the vertex shader, so no
		// filtering constraint is violated by the byte packing. ---
		const uint32_t res = m_Spec.Resolution;
		std::vector<uint8_t> texels(static_cast<size_t>(res) * res * 4);
		for (uint32_t j = 0; j < res; ++j)
			for (uint32_t i = 0; i < res; ++i)
			{
				const size_t s = static_cast<size_t>(j) * res + i;
				const uint32_t h16 = static_cast<uint32_t>(
					std::lround(std::clamp(m_Heights[s], 0.0f, 1.0f) * 65535.0f));
				const glm::vec3& n = m_Normals[s];
				texels[s * 4 + 0] = static_cast<uint8_t>(h16 >> 8);
				texels[s * 4 + 1] = static_cast<uint8_t>(h16 & 0xFF);
				texels[s * 4 + 2] = static_cast<uint8_t>(std::lround((n.x * 0.5f + 0.5f) * 255.0f));
				texels[s * 4 + 3] = static_cast<uint8_t>(std::lround((n.z * 0.5f + 0.5f) * 255.0f));
			}
		m_HeightTex = Texture2D::Create(res, res);
		if (m_HeightTex)
		{
			m_HeightTex->SetData(texels.data(), static_cast<uint32_t>(texels.size()));
			m_HeightTex->SetSampling(TextureFilter::Nearest, TextureWrap::ClampToEdge);
		}

		// --- Shared procedural albedo detail (used by any layer without its own
		// texture): mid-gray fBm so LayerColor stays the perceived albedo. ---
		{
			const Noise noise(m_Spec.Seed ^ 0xA5A5A5A5u);
			std::vector<uint8_t> detail(static_cast<size_t>(kDetailTexSize) * kDetailTexSize * 4);
			for (uint32_t j = 0; j < kDetailTexSize; ++j)
				for (uint32_t i = 0; i < kDetailTexSize; ++i)
				{
					const float u = static_cast<float>(i) / kDetailTexSize;
					const float v = static_cast<float>(j) / kDetailTexSize;
					// Two octave bands tiled 8x across the texture; value noise is
					// lattice-periodic enough at this tiling to hide seams.
					const float f = 0.5f + 0.25f * noise.Fbm2D(u * 8.0f, v * 8.0f, 4, 2.0f, 0.5f);
					const uint8_t g = static_cast<uint8_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f);
					const size_t s = (static_cast<size_t>(j) * kDetailTexSize + i) * 4;
					detail[s + 0] = detail[s + 1] = detail[s + 2] = g;
					detail[s + 3] = 255;
				}
			m_DetailTex = Texture2D::Create(kDetailTexSize, kDetailTexSize);
			if (m_DetailTex)
			{
				m_DetailTex->SetData(detail.data(), static_cast<uint32_t>(detail.size()));
				m_DetailTex->SetSampling(TextureFilter::Linear, TextureWrap::Repeat);
			}
		}

		m_GpuReady = m_Patch && m_HeightTex && m_DetailTex;
		return m_GpuReady;
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Rendering (S8.1 quadtree + S8.2 material)
	/////////////////////////////////////////////////////////////////////////////////

	void Terrain::Render(const glm::vec3& cameraPos, int entityID)
	{
		if (!EnsureGpuResources())
			return;

		m_Shader->Bind();

		// Scene-level lighting resources (IBL ambient + sun shadow map) — the
		// same set DrawMesh injects for materials (reserved units, BindingPoints.h).
		Renderer3D::ApplySceneBindings(m_Shader);

		// Terrain-owned textures on low units (never collide with the reserved 8+).
		m_HeightTex->Bind(0);
		m_Shader->SetInt("u_HeightMap", 0);
		for (int layer = 0; layer < 4; ++layer)
		{
			const Ref<Texture2D>& albedo = m_Spec.Layers[layer].Albedo;
			(albedo ? albedo : m_DetailTex)->Bind(1 + layer);
			char name[24];
			std::snprintf(name, sizeof(name), "u_LayerTex%d", layer);
			m_Shader->SetInt(name, 1 + layer);
		}

		// Global heightfield mapping.
		const float half = m_Spec.WorldSize * 0.5f;
		m_Shader->SetFloat2("u_TerrainOrigin", { m_Spec.Origin.x - half, m_Spec.Origin.y - half });
		m_Shader->SetFloat("u_CellSize", m_CellSize);
		m_Shader->SetFloat("u_HeightScale", m_Spec.HeightScale);
		m_Shader->SetFloat("u_BaseHeight", m_Spec.BaseHeight);
		m_Shader->SetFloat("u_SkirtDepth", m_Spec.SkirtDepth);
		m_Shader->SetInt("u_EntityID", entityID);

		// Material params (S8.2).
		const TerrainMaterialParams& mp = m_Spec.Material;
		for (int layer = 0; layer < 4; ++layer)
		{
			char cname[24], tname[24];
			std::snprintf(cname, sizeof(cname), "u_LayerColor%d", layer);
			std::snprintf(tname, sizeof(tname), "u_LayerTiling%d", layer);
			m_Shader->SetFloat3(cname, m_Spec.Layers[layer].Color);
			m_Shader->SetFloat(tname, m_Spec.Layers[layer].Tiling);
		}
		m_Shader->SetFloat("u_SlopeThreshold", mp.SlopeRockThreshold);
		m_Shader->SetFloat("u_SlopeBlend", mp.SlopeBlend);
		m_Shader->SetFloat("u_HighHeight", mp.HighHeight);
		m_Shader->SetFloat("u_HighBlend", mp.HighBlend);
		m_Shader->SetFloat("u_LowHeight", mp.LowHeight);
		m_Shader->SetFloat("u_LowBlend", mp.LowBlend);
		m_Shader->SetFloat("u_TriplanarSharpness", mp.TriplanarSharpness);

		// Wet band (F4). Default WetDarken = 0 makes these a no-op (shipped look).
		m_Shader->SetFloat("u_WetLine", mp.WetLine);
		m_Shader->SetFloat("u_WetBand", mp.WetBand);
		m_Shader->SetFloat("u_WetDarken", mp.WetDarken);

		m_Patch->GetVertexArray()->Bind();

		std::vector<NodeDraw> cut;
		CollectCut(0, 0, static_cast<int>(m_Spec.Resolution) - 1, cameraPos, 0, cut);
		DrawCut(m_Shader, cut);
		m_LastDrawnNodes = static_cast<uint32_t>(cut.size());
	}

	// Depth-only pass for shadow casting (F4). Shares the LOD cut with Render so
	// mountain casters and their receivers select the same tessellation. Runs
	// inside ShadowMap's depth pass (BeginDepthPass already set the FBO, viewport,
	// clear, and front-face cull) — no global render-state changes here.
	void Terrain::RenderDepth(const glm::mat4& lightViewProj, const glm::vec3& cameraPos)
	{
		if (!EnsureGpuResources())
			return;

		if (!m_DepthShader)
		{
			m_DepthShader = Shader::Create("assets/shaders/TerrainDepth.glsl");
			if (!m_DepthShader)
			{
				CS_CORE_ERROR("Terrain: TerrainDepth.glsl failed to load — terrain shadow casting disabled.");
				return;
			}
		}

		m_DepthShader->Bind();
		m_DepthShader->SetMat4("u_LightViewProj", lightViewProj);

		m_HeightTex->Bind(0);
		m_DepthShader->SetInt("u_HeightMap", 0);
		m_DepthShader->SetFloat("u_HeightScale", m_Spec.HeightScale);
		m_DepthShader->SetFloat("u_BaseHeight", m_Spec.BaseHeight);
		m_DepthShader->SetFloat("u_SkirtDepth", m_Spec.SkirtDepth);

		m_Patch->GetVertexArray()->Bind();

		std::vector<NodeDraw> cut;
		CollectCut(0, 0, static_cast<int>(m_Spec.Resolution) - 1, cameraPos, 0, cut);
		DrawCut(m_DepthShader, cut);
	}

	void Terrain::CollectCut(int nodeX, int nodeZ, int nodeTexels,
	                         const glm::vec3& cameraPos, int depth, std::vector<NodeDraw>& out) const
	{
		const float half    = m_Spec.WorldSize * 0.5f;
		const float originX = m_Spec.Origin.x - half + nodeX * m_CellSize;
		const float originZ = m_Spec.Origin.y - half + nodeZ * m_CellSize;
		const float size    = nodeTexels * m_CellSize;

		// Distance-based split: subdivide while the camera is closer than
		// LodDistanceFactor node-sizes (measured to the node's center at the
		// height mid-range, so overhead views LOD sanely too).
		if (depth < m_MaxDepth)
		{
			const glm::vec3 center(originX + size * 0.5f,
			                       0.5f * (m_MinHeight + m_MaxHeight),
			                       originZ + size * 0.5f);
			if (glm::distance(cameraPos, center) < size * m_Spec.LodDistanceFactor)
			{
				const int childTexels = nodeTexels / 2;
				CollectCut(nodeX,               nodeZ,               childTexels, cameraPos, depth + 1, out);
				CollectCut(nodeX + childTexels, nodeZ,               childTexels, cameraPos, depth + 1, out);
				CollectCut(nodeX,               nodeZ + childTexels, childTexels, cameraPos, depth + 1, out);
				CollectCut(nodeX + childTexels, nodeZ + childTexels, childTexels, cameraPos, depth + 1, out);
				return;
			}
		}

		out.push_back({ nodeX, nodeZ, nodeTexels });
	}

	void Terrain::DrawCut(const Ref<Shader>& shader, const std::vector<NodeDraw>& cut)
	{
		const float half = m_Spec.WorldSize * 0.5f;
		for (const NodeDraw& n : cut)
		{
			const float originX = m_Spec.Origin.x - half + n.X * m_CellSize;
			const float originZ = m_Spec.Origin.y - half + n.Z * m_CellSize;
			const float size    = n.Texels * m_CellSize;

			shader->SetFloat2("u_NodeOrigin", { originX, originZ });
			shader->SetFloat("u_NodeSize", size);
			shader->SetFloat2("u_NodeTexelOrigin", { static_cast<float>(n.X), static_cast<float>(n.Z) });
			shader->SetFloat("u_NodeTexels", static_cast<float>(n.Texels));

			RenderCommand::DrawIndexed(m_Patch->GetVertexArray(), m_Patch->GetIndexCount());
		}
	}

	uint32_t Terrain::GetHeightTextureID() const
	{
		return m_HeightTex ? m_HeightTex->GetRendererID() : 0;
	}

	glm::vec2 Terrain::GetWorldMinCorner() const
	{
		const float half = m_Spec.WorldSize * 0.5f;
		return { m_Spec.Origin.x - half, m_Spec.Origin.y - half };
	}
}
