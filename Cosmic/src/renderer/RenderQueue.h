#pragma once

// RenderQueue.h
// Last Modified: 7/3/2026

/**
 * ============================================================================
 * COSMIC ENGINE — render-queue sort keys + batching runs (S12.2/S12.3)
 * ============================================================================
 * [pure, header-only]
 *
 * The sorting brain of Renderer3D's mesh queue, factored out of the GL-coupled
 * renderer so it is unit-testable headless (tests/test_render_queue.cpp) —
 * same convention as math/Frustum.h and water/GerstnerWave.h.
 *
 * Renderer3D::DrawMesh no longer draws immediately (S12.2): submissions are
 * recorded with a Key and executed at Flush()/EndScene() in sorted order.
 *
 *   OPAQUE      — state-change key first (Shader -> Material -> Mesh), then
 *                 near-to-far (early-z / overdraw win), then Sequence (stable).
 *   TRANSPARENT — strictly far-to-near, then Sequence. No state grouping:
 *                 blending makes order part of the result.
 *
 * Identity fields are opaque pointer values (uintptr_t) — the queue only needs
 * "same or different", never dereferences. 0 is a legal identity (the Lambert
 * color path has no Material).
 *
 * FindInstancableRuns (S12.3) walks keys ALREADY in opaque-sorted order and
 * returns the maximal consecutive runs that one hardware-instanced draw can
 * replace: every member flagged Instancable (material has an instancing twin
 * AND entityID == -1 — per-instance entity IDs are not in the instance SSBO,
 * so auto-batching a picked entity would break ID picking) and all sharing the
 * same Material + Mesh. Runs shorter than minRun are not worth the SSBO upload
 * + twin-shader bind and stay single draws.
 * ============================================================================
 */

#include <cstdint>
#include <vector>

namespace Cosmic::RenderQueue
{
	struct Key
	{
		// State identity (pointer values; never dereferenced). Shader first —
		// program changes are the most expensive state switch.
		uintptr_t Shader   = 0;
		uintptr_t Material = 0;   // 0 = the Lambert color path (no material)
		uintptr_t Mesh     = 0;

		float    ViewDepthSq = 0.0f;   // squared eye -> world-AABB-center distance
		uint32_t Sequence    = 0;      // submission index (deterministic tiebreak)
		bool     Instancable = false;  // eligible for an S12.3 auto-instanced run
	};

	/** @brief Opaque order: Shader -> Material -> Mesh -> near-to-far -> Sequence. */
	inline bool OpaqueLess(const Key& a, const Key& b)
	{
		if (a.Shader   != b.Shader)   return a.Shader   < b.Shader;
		if (a.Material != b.Material) return a.Material < b.Material;
		if (a.Mesh     != b.Mesh)     return a.Mesh     < b.Mesh;
		if (a.ViewDepthSq != b.ViewDepthSq) return a.ViewDepthSq < b.ViewDepthSq;
		return a.Sequence < b.Sequence;
	}

	/** @brief Transparent order: far-to-near -> Sequence. */
	inline bool TransparentLess(const Key& a, const Key& b)
	{
		if (a.ViewDepthSq != b.ViewDepthSq) return a.ViewDepthSq > b.ViewDepthSq;
		return a.Sequence < b.Sequence;
	}

	/** @brief One auto-instancable stretch of the sorted opaque list:
	 *  sorted[First .. First+Count) collapses to one instanced draw. */
	struct Run
	{
		uint32_t First = 0;
		uint32_t Count = 0;
	};

	/**
	 * @brief Find the auto-instancable runs in `sorted` (keys in OpaqueLess
	 * order). A run = maximal consecutive span with identical Material + Mesh
	 * where every key is Instancable; spans shorter than `minRun` are dropped.
	 * OpaqueLess already clusters equal (Shader, Material, Mesh), so a single
	 * linear walk finds every maximal run.
	 */
	inline std::vector<Run> FindInstancableRuns(const std::vector<Key>& sorted, uint32_t minRun)
	{
		std::vector<Run> runs;
		if (minRun == 0)
			minRun = 1;

		const uint32_t n = static_cast<uint32_t>(sorted.size());
		uint32_t i = 0;
		while (i < n)
		{
			if (!sorted[i].Instancable)
			{
				++i;
				continue;
			}

			uint32_t j = i + 1;
			while (j < n && sorted[j].Instancable &&
			       sorted[j].Material == sorted[i].Material &&
			       sorted[j].Mesh     == sorted[i].Mesh)
				++j;

			if (j - i >= minRun)
				runs.push_back({ i, j - i });
			i = j;
		}
		return runs;
	}
}
