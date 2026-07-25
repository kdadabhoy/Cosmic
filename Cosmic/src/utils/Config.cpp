// Config.cpp
// Last Modified 7/1/2026
//
// TOML backend for the Config facade (E10). toml++ lives ONLY in this
// translation unit — Config.h exposes no toml types.

#include "utils/Config.h"
#include "utils/FileSystem.h"
#include "core/Log.h"

// Result-based API (no exceptions escaping the parser) — parse failures come
// back as a toml::parse_result we can inspect and log.
#define TOML_EXCEPTIONS 0
#include <toml.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Cosmic
{
	// =========================================================================
	// Impl — a VIEW into a shared parsed document. The root table is owned by a
	// shared_ptr so GetTable() children remain valid after the parent Config
	// (or any intermediate Ref) is destroyed.
	// =========================================================================
	struct Config::Impl
	{
		std::shared_ptr<const toml::table> Root;   // shared document ownership
		const toml::node*                  View;   // this config's subtree (== Root.get() for the top level)
	};

	namespace
	{
		// Dotted-path lookup relative to a node ("a.b.c", "motors[1].kf").
		toml::node_view<const toml::node> AtPath(const toml::node* view, const std::string& dottedKey)
		{
			if (!view)
				return {};
			return view->at_path(dottedKey);
		}

		// Numeric coercion: TOML distinguishes ints and floats; a config author
		// writing `tau = 1` for a float parameter should not get the fallback.
		bool AsDouble(const toml::node& n, double& out)
		{
			if (auto v = n.value<double>()) { out = *v; return true; }
			return false;
		}
	}

	// =========================================================================
	// Creation
	// =========================================================================

	Config::Config(Scope<Impl> impl, std::string source)
		: m_Impl(std::move(impl)), m_Source(std::move(source))
	{
	}

	Config::~Config() = default;

	Ref<Config> Config::Load(const std::string& path)
	{
		const std::string resolved = FileSystem::Resolve(path);

		// An absent file is a NORMAL outcome, not an error. Every caller treats a
		// null return as "not configured", and the editor probes several optional
		// files on a fresh profile (starforge/layouts/active.toml, editor.toml,
		// projects.toml). Checking first also keeps the message honest: with
		// TOML_EXCEPTIONS 0, toml++ folds "cannot open the file" into the same
		// parse_result as a syntax error, so a file that was never there used to
		// be reported as CS_CORE_ERROR "failed to parse '…' (line 0, column 0)".
		// A file that EXISTS but is malformed is still a genuine error below.
		std::error_code ec;
		if (!std::filesystem::is_regular_file(resolved, ec))
		{
			CS_CORE_TRACE("Config: no file at '{0}'.", resolved);
			return nullptr;
		}

		toml::parse_result result = toml::parse_file(resolved);
		if (!result)
		{
			CS_CORE_ERROR("Config: failed to parse '{0}': {1} (line {2}, column {3})",
				resolved,
				std::string(result.error().description()),
				result.error().source().begin.line,
				result.error().source().begin.column);
			return nullptr;
		}

		auto root = std::make_shared<const toml::table>(std::move(result).table());
		auto impl = CreateScope<Impl>();
		impl->View = root.get();
		impl->Root = std::move(root);

		CS_CORE_INFO("Config: loaded '{0}'.", resolved);
		return Ref<Config>(new Config(std::move(impl), resolved));
	}

	Ref<Config> Config::Parse(const std::string& tomlText, const std::string& sourceName)
	{
		toml::parse_result result = toml::parse(tomlText, sourceName);
		if (!result)
		{
			CS_CORE_ERROR("Config: failed to parse {0}: {1} (line {2}, column {3})",
				sourceName,
				std::string(result.error().description()),
				result.error().source().begin.line,
				result.error().source().begin.column);
			return nullptr;
		}

		auto root = std::make_shared<const toml::table>(std::move(result).table());
		auto impl = CreateScope<Impl>();
		impl->View = root.get();
		impl->Root = std::move(root);

		return Ref<Config>(new Config(std::move(impl), sourceName));
	}

	// =========================================================================
	// Queries
	// =========================================================================

	bool Config::Has(const std::string& dottedKey) const
	{
		return static_cast<bool>(AtPath(m_Impl->View, dottedKey));
	}

	float Config::GetFloat(const std::string& dottedKey, float fallback) const
	{
		auto nv = AtPath(m_Impl->View, dottedKey);
		double d = 0.0;
		if (nv && AsDouble(*nv.node(), d))
			return static_cast<float>(d);
		return fallback;
	}

	double Config::GetDouble(const std::string& dottedKey, double fallback) const
	{
		auto nv = AtPath(m_Impl->View, dottedKey);
		double d = 0.0;
		if (nv && AsDouble(*nv.node(), d))
			return d;
		return fallback;
	}

	int64_t Config::GetInt(const std::string& dottedKey, int64_t fallback) const
	{
		auto nv = AtPath(m_Impl->View, dottedKey);
		if (nv)
		{
			if (auto v = nv.value<int64_t>())
				return *v;
		}
		return fallback;
	}

	bool Config::GetBool(const std::string& dottedKey, bool fallback) const
	{
		auto nv = AtPath(m_Impl->View, dottedKey);
		if (nv)
		{
			if (auto v = nv.value<bool>())
				return *v;
		}
		return fallback;
	}

	std::string Config::GetString(const std::string& dottedKey, const std::string& fallback) const
	{
		auto nv = AtPath(m_Impl->View, dottedKey);
		if (nv)
		{
			if (auto v = nv.value<std::string>())
				return *v;
		}
		return fallback;
	}

	namespace
	{
		// Shared vecN reader: TOML numeric array of exactly N -> float[N].
		template<int N>
		bool ReadVecN(const toml::node* view, const std::string& key, float* out)
		{
			auto nv = AtPath(view, key);
			const toml::array* arr = nv ? nv.as_array() : nullptr;
			if (!arr || arr->size() != static_cast<size_t>(N))
				return false;

			for (int i = 0; i < N; ++i)
			{
				double d = 0.0;
				const toml::node* elem = arr->get(static_cast<size_t>(i));
				if (!elem || !AsDouble(*elem, d))
					return false;
				out[i] = static_cast<float>(d);
			}
			return true;
		}
	}

	glm::vec2 Config::GetVec2(const std::string& dottedKey, const glm::vec2& fallback) const
	{
		float v[2];
		return ReadVecN<2>(m_Impl->View, dottedKey, v) ? glm::vec2(v[0], v[1]) : fallback;
	}

	glm::vec3 Config::GetVec3(const std::string& dottedKey, const glm::vec3& fallback) const
	{
		float v[3];
		return ReadVecN<3>(m_Impl->View, dottedKey, v) ? glm::vec3(v[0], v[1], v[2]) : fallback;
	}

	glm::vec4 Config::GetVec4(const std::string& dottedKey, const glm::vec4& fallback) const
	{
		float v[4];
		return ReadVecN<4>(m_Impl->View, dottedKey, v) ? glm::vec4(v[0], v[1], v[2], v[3]) : fallback;
	}

	std::vector<float> Config::GetFloatArray(const std::string& dottedKey) const
	{
		std::vector<float> out;
		auto nv = AtPath(m_Impl->View, dottedKey);
		const toml::array* arr = nv ? nv.as_array() : nullptr;
		if (!arr)
			return out;

		out.reserve(arr->size());
		for (const toml::node& elem : *arr)
		{
			double d = 0.0;
			if (AsDouble(elem, d))
				out.push_back(static_cast<float>(d));
		}
		return out;
	}

	std::vector<Ref<Config>> Config::GetTable(const std::string& dottedKey) const
	{
		std::vector<Ref<Config>> out;
		auto nv = AtPath(m_Impl->View, dottedKey);
		if (!nv)
			return out;

		auto makeView = [this](const toml::node* node) -> Ref<Config>
		{
			auto impl = CreateScope<Impl>();
			impl->Root = m_Impl->Root;   // share document ownership
			impl->View = node;
			return Ref<Config>(new Config(std::move(impl), m_Source));
		};

		if (const toml::array* arr = nv.as_array())
		{
			// [[motors]] — array of tables
			for (const toml::node& elem : *arr)
			{
				if (elem.is_table())
					out.push_back(makeView(&elem));
			}
		}
		else if (nv.is_table())
		{
			// plain [table] — single-element result for uniform handling
			out.push_back(makeView(nv.node()));
		}

		return out;
	}
}
