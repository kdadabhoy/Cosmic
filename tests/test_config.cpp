// test_config.cpp - utils/Config.h (E10): TOML parse from string, typed getters
// with defaults, dotted paths, vec3 arrays, arrays-of-tables. Headless.

#include <string>
#include <vector>
#include <cstdint>

#include "doctest.h"
#include "utils/Config.h"

using Cosmic::Config;
using Cosmic::Ref;

namespace
{
	constexpr const char* kSampleToml = R"TOML(
# Viper-style airframe sample
title = "sample"

[airframe]
auw_kg = 1.49
wing_area_m2 = 0.30
aspect_ratio = 6            # integer written for a float parameter
inertia_diag = [0.02, 0.05, 0.06]
name = "viper"

[battery]
capacity_wh = 100
cells_s = 4
low_voltage_cutoff = true

[sim]
perfect_sensors = false
substeps = 8

[[motors]]
kf = 1.1e-5
tag = "left"

[[motors]]
kf = 1.3e-5
tag = "right"
)TOML";
}

TEST_SUITE("Config (E10)")
{
	TEST_CASE("parses from string and reads typed values via dotted paths")
	{
		Ref<Config> cfg = Config::Parse(kSampleToml, "test.toml");
		REQUIRE(static_cast<bool>(cfg));

		CHECK(cfg->Get<float>("airframe.auw_kg", 0.0f) == doctest::Approx(1.49f));
		CHECK(cfg->Get<double>("airframe.wing_area_m2", 0.0) == doctest::Approx(0.30));
		CHECK(cfg->Get<int>("battery.cells_s", 0) == 4);
		CHECK(cfg->Get<int64_t>("sim.substeps", 0) == 8);
		CHECK(cfg->Get<bool>("battery.low_voltage_cutoff", false) == true);
		CHECK(cfg->Get<bool>("sim.perfect_sensors", true) == false);
		CHECK(cfg->Get<std::string>("airframe.name", "") == "viper");
		CHECK(cfg->Get<std::string>("title", "") == "sample");
	}

	TEST_CASE("integer TOML values coerce to float getters")
	{
		Ref<Config> cfg = Config::Parse(kSampleToml);
		REQUIRE(static_cast<bool>(cfg));

		// aspect_ratio = 6 (TOML integer) read as float must not fall back
		CHECK(cfg->Get<float>("airframe.aspect_ratio", -1.0f) == doctest::Approx(6.0f));
		CHECK(cfg->Get<float>("battery.capacity_wh", -1.0f) == doctest::Approx(100.0f));
	}

	TEST_CASE("missing keys return the fallback")
	{
		Ref<Config> cfg = Config::Parse(kSampleToml);
		REQUIRE(static_cast<bool>(cfg));

		CHECK(cfg->Get<float>("airframe.does_not_exist", 42.5f) == doctest::Approx(42.5f));
		CHECK(cfg->Get<int>("nope.nope", -7) == -7);
		CHECK(cfg->Get<std::string>("battery.chemistry", "li-ion") == "li-ion");
		CHECK(cfg->Get<bool>("sim.headless", true) == true);

		// type mismatch also falls back (string key read as float)
		CHECK(cfg->Get<float>("airframe.name", 5.0f) == doctest::Approx(5.0f));
	}

	TEST_CASE("Has() reports key presence")
	{
		Ref<Config> cfg = Config::Parse(kSampleToml);
		REQUIRE(static_cast<bool>(cfg));

		CHECK(cfg->Has("airframe.auw_kg"));
		CHECK(cfg->Has("motors"));
		CHECK_FALSE(cfg->Has("airframe.missing"));
		CHECK_FALSE(cfg->Has("no_such_table.at_all"));
	}

	TEST_CASE("vec3 array maps to glm::vec3; wrong arity falls back")
	{
		Ref<Config> cfg = Config::Parse(kSampleToml);
		REQUIRE(static_cast<bool>(cfg));

		glm::vec3 inertia = cfg->Get<glm::vec3>("airframe.inertia_diag", glm::vec3(0.0f));
		CHECK(inertia.x == doctest::Approx(0.02f));
		CHECK(inertia.y == doctest::Approx(0.05f));
		CHECK(inertia.z == doctest::Approx(0.06f));

		// a 3-element array read as vec2 must fall back
		glm::vec2 wrong = cfg->Get<glm::vec2>("airframe.inertia_diag", glm::vec2(9.0f));
		CHECK(wrong.x == doctest::Approx(9.0f));

		std::vector<float> raw = cfg->GetFloatArray("airframe.inertia_diag");
		REQUIRE(raw.size() == 3);
		CHECK(raw[2] == doctest::Approx(0.06f));
	}

	TEST_CASE("arrays-of-tables via GetTable; views outlive the parent")
	{
		std::vector<Ref<Config>> motors;
		{
			Ref<Config> cfg = Config::Parse(kSampleToml);
			REQUIRE(static_cast<bool>(cfg));
			motors = cfg->GetTable("motors");
			// parent Ref dropped here - views share document ownership
		}

		REQUIRE(motors.size() == 2);
		CHECK(motors[0]->Get<std::string>("tag", "") == "left");
		CHECK(motors[1]->Get<std::string>("tag", "") == "right");
		CHECK(motors[1]->Get<float>("kf", 0.0f) == doctest::Approx(1.3e-5f));

		// plain table -> single-element vector
		Ref<Config> cfg2 = Config::Parse(kSampleToml);
		std::vector<Ref<Config>> battery = cfg2->GetTable("battery");
		REQUIRE(battery.size() == 1);
		CHECK(battery[0]->Get<int>("cells_s", 0) == 4);
	}

	TEST_CASE("malformed TOML returns nullptr")
	{
		Ref<Config> bad = Config::Parse("this is [not = valid toml", "bad.toml");
		CHECK_FALSE(static_cast<bool>(bad));
	}
}

