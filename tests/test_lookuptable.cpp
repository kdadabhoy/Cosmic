// test_lookuptable.cpp — math/LookupTable.h (E13): exact breakpoint hits,
// midpoint interpolation, clamp + extrapolate policies, 2D bilinear against
// hand values, CSV round-trip via DataExport.

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

#include "doctest.h"
#include "math/LookupTable.h"
#include "utils/DataExport.h"

using Cosmic::LookupTable1D;
using Cosmic::LookupTable2D;
using Cosmic::TableRangePolicy;

namespace
{
	std::filesystem::path ScratchDir()
	{
		auto dir = std::filesystem::temp_directory_path() / "cosmic_tests";
		std::filesystem::create_directories(dir);
		return dir;
	}
}

TEST_SUITE("LookupTable (E13)")
{
	TEST_CASE("1D: exact at breakpoints, linear at midpoints")
	{
		LookupTable1D t({ {0.0f, 0.0f}, {1.0f, 10.0f}, {2.0f, 40.0f} });
		REQUIRE(t.IsValid());

		CHECK(t.Sample(0.0f) == doctest::Approx(0.0f));
		CHECK(t.Sample(1.0f) == doctest::Approx(10.0f));
		CHECK(t.Sample(2.0f) == doctest::Approx(40.0f));

		CHECK(t.Sample(0.5f) == doctest::Approx(5.0f));
		CHECK(t.Sample(1.5f) == doctest::Approx(25.0f));
	}

	TEST_CASE("1D: unsorted input points are sorted by x")
	{
		LookupTable1D t({ {2.0f, 40.0f}, {0.0f, 0.0f}, {1.0f, 10.0f} });
		CHECK(t.Sample(0.5f) == doctest::Approx(5.0f));
		CHECK(t.Sample(1.5f) == doctest::Approx(25.0f));
	}

	TEST_CASE("1D: clamp holds edges; extrapolate continues the edge slope")
	{
		std::vector<std::pair<float, float>> pts = { {0.0f, 0.0f}, {1.0f, 10.0f} };

		LookupTable1D clamp(pts, TableRangePolicy::Clamp);
		CHECK(clamp.Sample(-1.0f) == doctest::Approx(0.0f));
		CHECK(clamp.Sample(5.0f)  == doctest::Approx(10.0f));

		LookupTable1D extrap(pts, TableRangePolicy::Extrapolate);
		CHECK(extrap.Sample(-1.0f) == doctest::Approx(-10.0f));
		CHECK(extrap.Sample(2.0f)  == doctest::Approx(20.0f));
	}

	TEST_CASE("2D: bilinear against hand-computed values")
	{
		// z = grid:      y=0   y=10
		//        x=0   [  0,   100 ]
		//        x=2   [ 20,   140 ]
		LookupTable2D t({ 0.0f, 2.0f }, { 0.0f, 10.0f },
		                { 0.0f, 100.0f,
		                  20.0f, 140.0f });
		REQUIRE(t.IsValid());

		// corners exact
		CHECK(t.Sample(0.0f, 0.0f)  == doctest::Approx(0.0f));
		CHECK(t.Sample(2.0f, 0.0f)  == doctest::Approx(20.0f));
		CHECK(t.Sample(0.0f, 10.0f) == doctest::Approx(100.0f));
		CHECK(t.Sample(2.0f, 10.0f) == doctest::Approx(140.0f));

		// centre: mean of corners = (0+20+100+140)/4 = 65
		CHECK(t.Sample(1.0f, 5.0f) == doctest::Approx(65.0f));

		// edge midpoints
		CHECK(t.Sample(1.0f, 0.0f)  == doctest::Approx(10.0f));
		CHECK(t.Sample(0.0f, 5.0f)  == doctest::Approx(50.0f));

		// clamp outside the grid
		CHECK(t.Sample(-5.0f, -5.0f) == doctest::Approx(0.0f));
		CHECK(t.Sample(99.0f, 99.0f) == doctest::Approx(140.0f));
	}

	TEST_CASE("2D: invalid shape yields an empty table")
	{
		LookupTable2D bad({ 0.0f, 1.0f }, { 0.0f, 1.0f }, { 1.0f, 2.0f, 3.0f }); // 3 != 4
		CHECK_FALSE(bad.IsValid());
		CHECK(bad.Sample(0.5f, 0.5f) == doctest::Approx(0.0f));
	}

	TEST_CASE("1D: FromCSV round-trip (with header row)")
	{
		const auto path = (ScratchDir() / "lut_roundtrip.csv").string();
		{
			std::ofstream f(path);
			f << "airspeed,thrust\n0,0\n10,5\n20,8\n";
		}

		LookupTable1D t = LookupTable1D::FromCSV(path);
		REQUIRE(t.IsValid());
		CHECK(t.Size() == 3);
		CHECK(t.Sample(10.0f) == doctest::Approx(5.0f));
		CHECK(t.Sample(15.0f) == doctest::Approx(6.5f));

		std::filesystem::remove(path);
	}

	TEST_CASE("LoadCSV: headerless data, header capture, malformed rejection")
	{
		const auto dir = ScratchDir();

		// headerless
		const auto p1 = (dir / "lc_noheader.csv").string();
		{ std::ofstream f(p1); f << "1,2\n3,4\n"; }
		std::vector<std::vector<double>> cols;
		std::vector<std::string> headers;
		REQUIRE(Cosmic::DataExport::LoadCSV(p1, cols, &headers));
		CHECK(headers.empty());
		REQUIRE(cols.size() == 2);
		CHECK(cols[1][1] == doctest::Approx(4.0));

		// header captured
		const auto p2 = (dir / "lc_header.csv").string();
		{ std::ofstream f(p2); f << "x,y\n1,2\n"; }
		REQUIRE(Cosmic::DataExport::LoadCSV(p2, cols, &headers));
		REQUIRE(headers.size() == 2);
		CHECK(headers[0] == "x");
		CHECK(cols[0].size() == 1);

		// ragged row rejected
		const auto p3 = (dir / "lc_ragged.csv").string();
		{ std::ofstream f(p3); f << "1,2\n3\n"; }
		CHECK_FALSE(Cosmic::DataExport::LoadCSV(p3, cols));

		std::filesystem::remove(p1);
		std::filesystem::remove(p2);
		std::filesystem::remove(p3);
	}
}
