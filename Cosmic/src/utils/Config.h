#pragma once

// Config.h
// Last Modified 7/1/2026
//
// E10 (docs/plans/03-simulation-engine-plan.md): data-driven parameters for any
// simulation — masses, gains, noise levels — loaded from TOML files with no
// recompiles. TOML over JSON/INI: comments + human-friendly + typed. The parser
// is the vendored toml++ (dependencies/tomlplusplus, MIT), kept ENTIRELY inside
// Config.cpp — this header exposes no toml types, so a future JSON backend can
// slot in behind the same facade.
//
// Usage:
//     Ref<Config> cfg = Config::Load("project://config/vehicle.toml");
//     if (!cfg) { /* missing/unparseable — error already logged */ }
//     float wh   = cfg->Get<float>("battery.capacity_wh", 100.0f);
//     glm::vec3 inertia = cfg->Get<glm::vec3>("airframe.inertia_diag", {1,1,1});
//     for (const Ref<Config>& motor : cfg->GetTable("motors"))
//         float kf = motor->Get<float>("kf", 0.0f);
//
// Keys are dotted paths ("battery.capacity_wh"); array indices work too
// ("motors[1].kf"). Read-only by design — Save is deferred until a consumer
// exists (theme files already have their own format).

#include "core/Core.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>
#include <type_traits>

namespace Cosmic
{
	class COSMIC_API Config
	{
	public:
		// =====================================================================
		// Creation
		// =====================================================================

		// Loads and parses a TOML file. The path goes through FileSystem::Resolve,
		// so "project://", "engine://" and "user://" prefixes work. Returns
		// nullptr (and logs an error) when the file is missing or fails to parse.
		static Ref<Config> Load(const std::string& path);

		// Parses TOML from an in-memory string (unit tests, generated config).
		// sourceName appears in error messages. Returns nullptr on parse failure.
		static Ref<Config> Parse(const std::string& tomlText, const std::string& sourceName = "<string>");

		~Config();

		Config(const Config&) = delete;
		Config& operator=(const Config&) = delete;

		// =====================================================================
		// Queries
		// =====================================================================

		// True when a value exists at the dotted path (any type).
		bool Has(const std::string& dottedKey) const;

		// Typed getters with defaults — the workhorse API. A missing key OR a
		// type mismatch returns the fallback (mismatches also log a warning once
		// per call site pattern is overkill — they log every time; fix your file).
		float        GetFloat (const std::string& dottedKey, float fallback) const;
		double       GetDouble(const std::string& dottedKey, double fallback) const;
		int64_t      GetInt   (const std::string& dottedKey, int64_t fallback) const;
		bool         GetBool  (const std::string& dottedKey, bool fallback) const;
		std::string  GetString(const std::string& dottedKey, const std::string& fallback) const;

		// TOML arrays of 2/3/4 numbers -> glm vectors.
		glm::vec2    GetVec2(const std::string& dottedKey, const glm::vec2& fallback) const;
		glm::vec3    GetVec3(const std::string& dottedKey, const glm::vec3& fallback) const;
		glm::vec4    GetVec4(const std::string& dottedKey, const glm::vec4& fallback) const;

		// TOML array of numbers of any length -> vector<float> (empty fallback).
		std::vector<float> GetFloatArray(const std::string& dottedKey) const;

		// Array-of-tables ([[motors]]) -> one Config view per element. A plain
		// table at the key returns a single-element vector (uniform handling).
		// Views share ownership of the parsed document — they stay valid after
		// the parent Ref is dropped.
		std::vector<Ref<Config>> GetTable(const std::string& dottedKey) const;

		// Generic front-end: Get<float>, Get<double>, Get<int>, Get<int64_t>,
		// Get<bool>, Get<std::string>, Get<glm::vec2/3/4>.
		template<typename T>
		T Get(const std::string& dottedKey, const T& fallback = T{}) const
		{
			if constexpr (std::is_same_v<T, float>)            return GetFloat(dottedKey, fallback);
			else if constexpr (std::is_same_v<T, double>)      return GetDouble(dottedKey, fallback);
			else if constexpr (std::is_same_v<T, bool>)        return GetBool(dottedKey, fallback);
			else if constexpr (std::is_same_v<T, int>)         return static_cast<int>(GetInt(dottedKey, fallback));
			else if constexpr (std::is_same_v<T, int64_t>)     return GetInt(dottedKey, fallback);
			else if constexpr (std::is_same_v<T, uint32_t>)    return static_cast<uint32_t>(GetInt(dottedKey, fallback));
			else if constexpr (std::is_same_v<T, std::string>) return GetString(dottedKey, fallback);
			else if constexpr (std::is_same_v<T, glm::vec2>)   return GetVec2(dottedKey, fallback);
			else if constexpr (std::is_same_v<T, glm::vec3>)   return GetVec3(dottedKey, fallback);
			else if constexpr (std::is_same_v<T, glm::vec4>)   return GetVec4(dottedKey, fallback);
			else static_assert(sizeof(T) == 0, "Config::Get<T>: unsupported type — see Config.h for the supported set");
		}

		// Where this config came from ("<string>" for Parse) — for error context.
		const std::string& GetSource() const { return m_Source; }

	private:
		struct Impl;
		Config(Scope<Impl> impl, std::string source);

		Scope<Impl> m_Impl;
		std::string m_Source;
	};
}
