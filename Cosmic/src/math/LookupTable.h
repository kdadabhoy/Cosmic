#pragma once

// LookupTable.h
// Last Modified 7/1/2026
//
// E13 (docs/plans/03-simulation-engine-plan.md): 1D and 2D lookup tables with
// sorted breakpoints and linear/bilinear interpolation. This is how apps
// encode aero polars, motor thrust maps, gain schedules, and battery discharge
// curves — pure data in, engine does the interp. Header-only.
//
// Out-of-range policy per table: Clamp (default — hold the edge value) or
// Extrapolate (continue the edge segment's slope).
//
// Usage:
//     Cosmic::LookupTable1D thrust({{0.0f, 0.0f}, {1.0f, 14.6f}});
//     float n = thrust.Sample(0.5f);
//
//     auto polar = Cosmic::LookupTable1D::FromCSV("project://polars/cl.csv");
//
//     Cosmic::LookupTable2D gains(alphaBreaks, speedBreaks, values /*row-major [alpha][speed]*/);
//     float kp = gains.Sample(alpha, speed);

#include "core/Core.h"
#include "utils/DataExport.h"
#include "utils/FileSystem.h"
#include "core/Log.h"

#include <vector>
#include <utility>
#include <algorithm>
#include <cstddef>

namespace Cosmic
{
	enum class TableRangePolicy
	{
		Clamp,        // hold the first/last value outside the breakpoint range
		Extrapolate,  // extend the first/last segment's slope
	};

	namespace detail
	{
		// Index of the segment [i, i+1] bracketing x in ascending breakpoints;
		// clamps to the first/last segment for out-of-range x.
		inline size_t SegmentIndex(const std::vector<float>& breaks, float x)
		{
			// breaks.size() >= 2 guaranteed by the table constructors
			const auto it = std::upper_bound(breaks.begin(), breaks.end(), x);
			const ptrdiff_t idx = (it - breaks.begin()) - 1;
			return static_cast<size_t>(std::clamp<ptrdiff_t>(idx, 0, static_cast<ptrdiff_t>(breaks.size()) - 2));
		}

		// Interpolation parameter for x on segment i, honoring the range policy.
		inline float SegmentT(const std::vector<float>& breaks, size_t i, float x, TableRangePolicy policy)
		{
			const float x0 = breaks[i];
			const float x1 = breaks[i + 1];
			const float span = x1 - x0;
			float t = (span != 0.0f) ? (x - x0) / span : 0.0f;
			if (policy == TableRangePolicy::Clamp)
				t = std::clamp(t, 0.0f, 1.0f);
			return t;
		}
	}

	// =========================================================================
	// LookupTable1D — y = f(x), linear interpolation over sorted breakpoints.
	// =========================================================================
	class LookupTable1D
	{
	public:
		LookupTable1D() = default;

		// Points need not arrive sorted; they are sorted by x. Duplicate x
		// values keep their relative order (stable) — an exact-hit sample lands
		// on the LAST duplicate (upper_bound semantics), the pair forms a step.
		LookupTable1D(std::vector<std::pair<float, float>> points,
		              TableRangePolicy policy = TableRangePolicy::Clamp)
			: m_Policy(policy)
		{
			std::stable_sort(points.begin(), points.end(),
				[](const auto& a, const auto& b) { return a.first < b.first; });

			m_X.reserve(points.size());
			m_Y.reserve(points.size());
			for (const auto& [x, y] : points)
			{
				m_X.push_back(x);
				m_Y.push_back(y);
			}
		}

		// Two-column CSV (x, y), header row optional. Empty table on failure
		// (error logged by the CSV loader). The path is VFS-resolved HERE —
		// header-only, so "project://" resolves against the CALLING DLL's
		// active project (DataExport::LoadCSV itself does no resolution).
		static LookupTable1D FromCSV(const std::string& filepath,
		                             TableRangePolicy policy = TableRangePolicy::Clamp)
		{
			std::vector<std::vector<double>> cols;
			if (!DataExport::LoadCSV(FileSystem::Resolve(filepath), cols) || cols.size() < 2 || cols[0].size() < 2)
			{
				CS_CORE_ERROR("LookupTable1D: '{0}' did not yield two columns of at least two rows.", filepath);
				return {};
			}

			std::vector<std::pair<float, float>> pts;
			pts.reserve(cols[0].size());
			for (size_t i = 0; i < cols[0].size(); ++i)
				pts.emplace_back(static_cast<float>(cols[0][i]), static_cast<float>(cols[1][i]));
			return LookupTable1D(std::move(pts), policy);
		}

		bool   IsValid() const { return m_X.size() >= 2; }
		size_t Size()    const { return m_X.size(); }

		float Sample(float x) const
		{
			if (m_X.empty()) return 0.0f;
			if (m_X.size() == 1) return m_Y[0];

			const size_t i = detail::SegmentIndex(m_X, x);
			const float  t = detail::SegmentT(m_X, i, x, m_Policy);
			return m_Y[i] + (m_Y[i + 1] - m_Y[i]) * t;
		}

	private:
		std::vector<float> m_X, m_Y;
		TableRangePolicy   m_Policy = TableRangePolicy::Clamp;
	};

	// =========================================================================
	// LookupTable2D — z = f(x, y), bilinear interpolation over a rectangular
	// grid. values is row-major: values[ix * yBreaks.size() + iy].
	// =========================================================================
	class LookupTable2D
	{
	public:
		LookupTable2D() = default;

		// xBreaks and yBreaks must be ascending; values.size() must equal
		// xBreaks.size() * yBreaks.size(). An invalid shape logs and yields an
		// empty (IsValid()==false, Sample()==0) table.
		LookupTable2D(std::vector<float> xBreaks,
		              std::vector<float> yBreaks,
		              std::vector<float> values,
		              TableRangePolicy policy = TableRangePolicy::Clamp)
			: m_Policy(policy)
		{
			if (xBreaks.size() < 2 || yBreaks.size() < 2 ||
			    values.size() != xBreaks.size() * yBreaks.size() ||
			    !std::is_sorted(xBreaks.begin(), xBreaks.end()) ||
			    !std::is_sorted(yBreaks.begin(), yBreaks.end()))
			{
				CS_CORE_ERROR("LookupTable2D: invalid shape ({}x{} breaks, {} values) — table left empty.",
					xBreaks.size(), yBreaks.size(), values.size());
				return;
			}

			m_X = std::move(xBreaks);
			m_Y = std::move(yBreaks);
			m_V = std::move(values);
		}

		bool IsValid() const { return !m_V.empty(); }

		float Sample(float x, float y) const
		{
			if (m_V.empty())
				return 0.0f;

			const size_t ix = detail::SegmentIndex(m_X, x);
			const size_t iy = detail::SegmentIndex(m_Y, y);
			const float  tx = detail::SegmentT(m_X, ix, x, m_Policy);
			const float  ty = detail::SegmentT(m_Y, iy, y, m_Policy);

			const float v00 = At(ix,     iy);
			const float v01 = At(ix,     iy + 1);
			const float v10 = At(ix + 1, iy);
			const float v11 = At(ix + 1, iy + 1);

			const float v0 = v00 + (v01 - v00) * ty;   // along y at x0
			const float v1 = v10 + (v11 - v10) * ty;   // along y at x1
			return v0 + (v1 - v0) * tx;                // along x
		}

	private:
		float At(size_t ix, size_t iy) const { return m_V[ix * m_Y.size() + iy]; }

		std::vector<float> m_X, m_Y, m_V;
		TableRangePolicy   m_Policy = TableRangePolicy::Clamp;
	};
}
