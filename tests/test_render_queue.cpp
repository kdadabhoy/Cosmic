// test_render_queue.cpp — Phase 12 (S12.2/S12.3/S12.4) pure-logic tests:
// renderer/RenderQueue.h sort keys + auto-instancing runs, and
// LODGroupComponent::SelectLevel. Headless (no GL) — the GPU-side execution of
// the queue is accepted in Engine3DDemo's "Performance (S12)" panel.

#include "doctest.h"

#include "renderer/RenderQueue.h"
#include "scene/Components.h"
#ifndef COSMIC_2D_ONLY
#include "scene/Components3D.h"
#endif

#include <algorithm>
#include <vector>

using Cosmic::RenderQueue::Key;
using Cosmic::RenderQueue::Run;
using Cosmic::RenderQueue::OpaqueLess;
using Cosmic::RenderQueue::TransparentLess;
using Cosmic::RenderQueue::FindInstancableRuns;

namespace
{
	Key MakeKey(uintptr_t shader, uintptr_t material, uintptr_t mesh,
	            float depthSq, uint32_t seq, bool instancable = false)
	{
		Key k;
		k.Shader = shader; k.Material = material; k.Mesh = mesh;
		k.ViewDepthSq = depthSq; k.Sequence = seq; k.Instancable = instancable;
		return k;
	}
}

TEST_CASE("RenderQueue: opaque order groups state then sorts front-to-back")
{
	// Two shaders, two materials, interleaved submission order, varied depths.
	std::vector<Key> keys = {
		MakeKey(2, 20, 200, 9.0f, 0),
		MakeKey(1, 10, 100, 4.0f, 1),
		MakeKey(2, 20, 200, 1.0f, 2),
		MakeKey(1, 11, 100, 16.0f, 3),
		MakeKey(1, 10, 100, 1.0f, 4),
	};
	std::sort(keys.begin(), keys.end(), OpaqueLess);

	// Shader 1 group first, inside it material 10 before 11; equal state sorts
	// near-to-far.
	CHECK(keys[0].Shader == 1); CHECK(keys[0].Material == 10); CHECK(keys[0].ViewDepthSq == 1.0f);
	CHECK(keys[1].Shader == 1); CHECK(keys[1].Material == 10); CHECK(keys[1].ViewDepthSq == 4.0f);
	CHECK(keys[2].Shader == 1); CHECK(keys[2].Material == 11);
	CHECK(keys[3].Shader == 2); CHECK(keys[3].ViewDepthSq == 1.0f);
	CHECK(keys[4].Shader == 2); CHECK(keys[4].ViewDepthSq == 9.0f);
}

TEST_CASE("RenderQueue: opaque order is deterministic via Sequence on full ties")
{
	std::vector<Key> keys = {
		MakeKey(1, 10, 100, 4.0f, 7),
		MakeKey(1, 10, 100, 4.0f, 2),
		MakeKey(1, 10, 100, 4.0f, 5),
	};
	std::sort(keys.begin(), keys.end(), OpaqueLess);
	CHECK(keys[0].Sequence == 2);
	CHECK(keys[1].Sequence == 5);
	CHECK(keys[2].Sequence == 7);
}

TEST_CASE("RenderQueue: transparent order is strictly back-to-front")
{
	std::vector<Key> keys = {
		MakeKey(1, 10, 100, 4.0f, 0),
		MakeKey(9, 99, 900, 25.0f, 1),   // different state MUST NOT regroup it
		MakeKey(1, 10, 100, 9.0f, 2),
	};
	std::sort(keys.begin(), keys.end(), TransparentLess);
	CHECK(keys[0].ViewDepthSq == 25.0f);
	CHECK(keys[1].ViewDepthSq == 9.0f);
	CHECK(keys[2].ViewDepthSq == 4.0f);
}

TEST_CASE("RenderQueue: FindInstancableRuns finds maximal qualifying runs only")
{
	// Sorted order: [0..4] same mesh/material instancable (run of 5),
	// [5] same pair but NOT instancable, [6..7] instancable pair of 2 (below
	// minRun), [8..11] a second run of 4 on another mesh.
	std::vector<Key> sorted = {
		MakeKey(1, 10, 100, 1, 0, true),
		MakeKey(1, 10, 100, 2, 1, true),
		MakeKey(1, 10, 100, 3, 2, true),
		MakeKey(1, 10, 100, 4, 3, true),
		MakeKey(1, 10, 100, 5, 4, true),
		MakeKey(1, 10, 100, 6, 5, false),   // entityID != -1 draw: breaks out
		MakeKey(1, 10, 101, 1, 6, true),
		MakeKey(1, 10, 101, 2, 7, true),
		MakeKey(1, 10, 102, 1, 8, true),
		MakeKey(1, 10, 102, 2, 9, true),
		MakeKey(1, 10, 102, 3, 10, true),
		MakeKey(1, 10, 102, 4, 11, true),
	};

	const std::vector<Run> runs = FindInstancableRuns(sorted, 4);
	REQUIRE(runs.size() == 2);
	CHECK(runs[0].First == 0); CHECK(runs[0].Count == 5);
	CHECK(runs[1].First == 8); CHECK(runs[1].Count == 4);
}

TEST_CASE("RenderQueue: runs never span a material boundary")
{
	std::vector<Key> sorted = {
		MakeKey(1, 10, 100, 1, 0, true),
		MakeKey(1, 10, 100, 2, 1, true),
		MakeKey(1, 11, 100, 1, 2, true),   // same mesh, DIFFERENT material
		MakeKey(1, 11, 100, 2, 3, true),
	};
	CHECK(FindInstancableRuns(sorted, 2).size() == 2);
	CHECK(FindInstancableRuns(sorted, 3).empty());
}

TEST_CASE("RenderQueue: empty input and minRun clamping")
{
	CHECK(FindInstancableRuns({}, 4).empty());

	// minRun 0 is treated as 1 (a single instancable draw is a "run").
	std::vector<Key> one = { MakeKey(1, 10, 100, 1, 0, true) };
	const auto runs = FindInstancableRuns(one, 0);
	REQUIRE(runs.size() == 1);
	CHECK(runs[0].Count == 1);
}

TEST_CASE("LODGroupComponent::SelectLevel picks by ascending MaxDistance")
{
	using LOD = Cosmic::LODGroupComponent;
	std::vector<LOD::Level> levels = {
		{ nullptr, 15.0f },
		{ nullptr, 35.0f },
		{ nullptr, 90.0f },
	};

	CHECK(LOD::SelectLevel(levels, 0.0f)   == 0);
	CHECK(LOD::SelectLevel(levels, 15.0f)  == 0);   // boundary is inclusive
	CHECK(LOD::SelectLevel(levels, 15.01f) == 1);
	CHECK(LOD::SelectLevel(levels, 35.0f)  == 1);
	CHECK(LOD::SelectLevel(levels, 89.9f)  == 2);
	CHECK(LOD::SelectLevel(levels, 90.01f) == -1);  // beyond the last: culled
	CHECK(LOD::SelectLevel({}, 1.0f)       == -1);  // no levels
}
