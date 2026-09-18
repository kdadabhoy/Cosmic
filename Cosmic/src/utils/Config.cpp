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
#include <fstream>
#include <iterator>
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
	// =========================================================================
	// WO-09 / KI-49 — the table-header gate.
	//
	// toml++ 3.4's parse_table_header() hands the character after "[" / "[[" (and
	// horizontal whitespace) straight to parse_key(), whose first line is
	// TOML_ASSERT_ASSUME(is_bare_key_character(*cp) || is_string_delimiter(*cp)):
	// an assert() in a Debug build, a compiler __assume in Release. The document
	// loop guards the key-value path with that predicate but not the header
	// path, so "[!x]" — a one-character typo in any .toml — aborted a Debug
	// editor and was undefined behaviour in Release. Every parse in this file
	// runs this check first, using the parser's OWN predicates on the first
	// UTF-8 code point of each header, so nothing toml++ would accept is refused.
	// =========================================================================
	namespace
	{
		// Decode one UTF-8 code point at text[i] (malformed bytes decode to the
		// byte value, which is never a bare-key character or a delimiter).
		char32_t CodePointAt(const std::string& text, size_t i)
		{
			const unsigned char b0 = (unsigned char)text[i];
			auto cont = [&](size_t k) -> int
			{
				if (i + k >= text.size()) return -1;
				const unsigned char b = (unsigned char)text[i + k];
				return (b & 0xC0) == 0x80 ? (b & 0x3F) : -1;
			};
			if (b0 < 0x80) return b0;
			if ((b0 & 0xE0) == 0xC0) { const int c1 = cont(1); return c1 < 0 ? b0 : (char32_t)(((b0 & 0x1F) << 6) | c1); }
			if ((b0 & 0xF0) == 0xE0) { const int c1 = cont(1), c2 = cont(2); return (c1 < 0 || c2 < 0) ? b0 : (char32_t)(((b0 & 0x0F) << 12) | (c1 << 6) | c2); }
			if ((b0 & 0xF8) == 0xF0) { const int c1 = cont(1), c2 = cont(2), c3 = cont(3); return (c1 < 0 || c2 < 0 || c3 < 0) ? b0 : (char32_t)(((b0 & 0x07) << 18) | (c1 << 12) | (c2 << 6) | c3); }
			return b0;
		}

		// False (with the 1-based line) when a table header starts with a character
		// parse_key() cannot take. Table headers are the only place the parser
		// reaches parse_key() unguarded, so this is exactly the assertion's
		// precondition. Lines inside a multi-line string (""" or ''') are content, not
		// headers, so the scan tracks those two states (comments and single-line
		// strings are skipped so a quote inside them cannot open one).
		bool TableHeadersOk(const std::string& text, size_t& badLine)
		{
			enum { None, BasicML, LiteralML } ml = None;
			size_t line = 1;
			for (size_t i = 0; i < text.size(); ++line)
			{
				size_t eol = i;
				while (eol < text.size() && text[eol] != '\n') ++eol;

				if (ml == None)
				{
					// Line start: skip horizontal whitespace, then the header check.
					size_t k = i;
					while (k < eol && (text[k] == ' ' || text[k] == '\t')) ++k;
					if (k < eol && text[k] == '[')
					{
						size_t j = k + 1;
						while (j < eol && (text[j] == ' ' || text[j] == '\t')) ++j;
						if (j < eol && text[j] == '[')
						{
							++j;
							while (j < eol && (text[j] == ' ' || text[j] == '\t')) ++j;
						}
						// EOF and a premature ']' are the parser's own (safe) error paths.
						if (j < eol && text[j] != ']' && text[j] != '\r')
						{
							const char32_t c = CodePointAt(text, j);
							if (!toml::impl::is_bare_key_character(c) && !toml::impl::is_string_delimiter(c))
							{
								badLine = line;
								return false;
							}
						}
					}
				}

				// Walk the rest of the line to track multi-line string state.
				for (size_t p = i; p < eol; ++p)
				{
					const char c = text[p];
					if (ml == BasicML)
					{
						if (c == '\\') { ++p; continue; }                       // escaped char (incl. \")
						if (c == '"' && text.compare(p, 3, "\"\"\"") == 0) { ml = None; p += 2; }
						continue;
					}
					if (ml == LiteralML)
					{
						if (c == '\'' && text.compare(p, 3, "'''") == 0) { ml = None; p += 2; }
						continue;
					}
					if (c == '#') break;                                         // comment to end of line
					if (c == '"')
					{
						if (text.compare(p, 3, "\"\"\"") == 0) { ml = BasicML; p += 2; continue; }
						for (++p; p < eol && text[p] != '"'; ++p) if (text[p] == '\\') ++p;   // single-line basic
						continue;
					}
					if (c == '\'')
					{
						if (text.compare(p, 3, "'''") == 0) { ml = LiteralML; p += 2; continue; }
						for (++p; p < eol && text[p] != '\''; ++p) {}                       // single-line literal
						continue;
					}
				}
				i = eol < text.size() ? eol + 1 : eol;
			}
			return true;
		}
	}


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

		// Read the text ourselves (rather than toml::parse_file) so the KI-49 header
		// gate sees it before the parser does; the source path in errors is unchanged.
		std::string text;
		{
			std::ifstream in(std::filesystem::u8path(resolved), std::ios::binary);
			if (!in)
			{
				CS_CORE_ERROR("Config: cannot open '{0}'.", resolved);
				return nullptr;
			}
			text.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		}
		size_t badLine = 0;
		if (!TableHeadersOk(text, badLine))
		{
			CS_CORE_ERROR("Config: failed to parse '{0}': table header must start with a key (line {1})", resolved, badLine);
			return nullptr;
		}

		toml::parse_result result = toml::parse(text, resolved);
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
		size_t badLine = 0;
		if (!TableHeadersOk(tomlText, badLine))   // WO-09 / KI-49 — see the gate above
		{
			CS_CORE_ERROR("Config: failed to parse {0}: table header must start with a key (line {1})", sourceName, badLine);
			return nullptr;
		}
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
