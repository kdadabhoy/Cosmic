#pragma once
// wo09_fuzz.h — WO-09 (2D stability): the seeded, bounded document fuzzer shared
// by the C04 (Flow / Story) and C05 (scene / prefab / material / config) parser
// cases (catalog 03 "Fuzz": fixed seeds, bounded sizes/time, 2,000 cases per
// parser in the PR profile, 50,000 nightly; every failure minimized and committed
// as a regression fixture under tests/fixtures/wo09/corrupt/).
//
// Two mutation families, both driven by one std::mt19937 seeded per parser:
//   * BYTE mutations of the serialized text — truncation at a random offset, a
//     flipped/replaced byte, an inserted junk run, a deleted span, a duplicated
//     span — the F-CORRUPT "truncation / bad magic" class;
//   * STRUCTURAL mutations of the parsed JSON tree — a random node replaced by
//     an edge value (null, bools, 0, -1, 2^31, 2^63, 1e999 (= inf on parse),
//     -1e999, "", a 64 KiB string, [], {}, a 200-deep nest, a copy of the whole
//     document), a key deleted, two sibling values swapped, a key renamed —
//     the "valid JSON, wrong schema" class that byte flips almost never reach.
//
// The fuzzer never inspects the parser under test: it only produces inputs and
// enforces the per-case deadline. What "handled" means is the caller's oracle
// (returned false, or returned true and left a consistent object) — a crash or
// an uncaught exception ends the process and is the evidence.

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace Wo09Fuzz
{
    using json = nlohmann::json;

    inline std::string ReadFile(const std::filesystem::path& p)
    {
        std::ifstream in(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    inline void WriteFile(const std::filesystem::path& p, const std::string& text)
    {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        out << text;
    }

    // ---- byte-level ---------------------------------------------------------

    inline std::string MutateBytes(const std::string& src, std::mt19937& rng)
    {
        if (src.empty()) return src;
        std::string s = src;
        static const char kJunk[] = "{[\"\\:,0-eE.\x00\xff nu";   // 16 bytes incl. the embedded NUL
        std::uniform_int_distribution<int> kind(0, 5);
        const int rounds = 1 + (int)(rng() % 3);
        for (int r = 0; r < rounds && !s.empty(); ++r)
        {
            std::uniform_int_distribution<size_t> p2(0, s.size() - 1);
            const size_t at = p2(rng);
            switch (kind(rng))
            {
            case 0: s.resize(at); break;                                          // truncate
            case 1: s[at] = (char)(rng() & 0xFF); break;                          // replace a byte
            case 2: s[at] ^= (char)(1 << (rng() % 8)); break;                     // flip a bit
            case 3: { const size_t n = 1 + rng() % 16; std::string junk; for (size_t i = 0; i < n; ++i) junk += kJunk[rng() % 16]; s.insert(at, junk); break; }
            case 4: { const size_t n = 1 + rng() % 32; s.erase(at, n); break; }    // delete a span
            case 5: { const size_t n = 1 + rng() % 64; s.insert(at, s.substr(at, n)); break; }   // duplicate a span
            }
        }
        return s;
    }

    // ---- structural ---------------------------------------------------------

    inline void CollectNodes(json& j, std::vector<json*>& out, int depth = 0)
    {
        out.push_back(&j);
        if (depth > 64) return;
        if (j.is_object()) for (auto& kv : j.items()) CollectNodes(kv.value(), out, depth + 1);
        else if (j.is_array()) for (auto& v : j) CollectNodes(v, out, depth + 1);
    }

    inline json EdgeValue(std::mt19937& rng, const json& whole)
    {
        switch (rng() % 16)
        {
        case 0:  return nullptr;
        case 1:  return true;
        case 2:  return false;
        case 3:  return 0;
        case 4:  return -1;
        case 5:  return (int64_t)2147483648LL;
        case 6:  return (uint64_t)9223372036854775808ULL;
        case 7:  { json v = json::parse("1e999", nullptr, false); return v.is_discarded() ? json(1e308) : v; }   // +inf once parsed (dumps as null)
        case 8:  { json v = json::parse("-1e999", nullptr, false); return v.is_discarded() ? json(-1e308) : v; }
        case 9:  return "";
        case 10: return std::string(65536, 'A');    // oversized metadata
        case 11: return json::array();
        case 12: return json::object();
        case 13: { json n = 0; for (int i = 0; i < 200; ++i) n = json::array({ n }); return n; }   // deep nest
        case 14: return whole;                       // the whole document as a value
        default: return 0.5;
        }
    }

    inline std::string MutateStructure(const std::string& src, std::mt19937& rng)
    {
        json j = json::parse(src, nullptr, false);
        if (j.is_discarded()) return MutateBytes(src, rng);
        std::vector<json*> nodes;
        CollectNodes(j, nodes);
        if (nodes.empty()) return src;
        const int rounds = 1 + (int)(rng() % 3);
        for (int r = 0; r < rounds; ++r)
        {
            nodes.clear();
            CollectNodes(j, nodes);
            json* n = nodes[rng() % nodes.size()];
            switch (rng() % 5)
            {
            case 0: *n = EdgeValue(rng, j); break;                                 // replace a node
            case 1:                                                                // delete a key
                if (n->is_object() && !n->empty())
                {
                    auto it = n->begin(); std::advance(it, rng() % n->size());
                    n->erase(it);
                }
                else *n = EdgeValue(rng, j);
                break;
            case 2:                                                                // swap two sibling values
                if (n->is_object() && n->size() >= 2)
                {
                    auto a = n->begin(); std::advance(a, rng() % n->size());
                    auto b = n->begin(); std::advance(b, rng() % n->size());
                    std::swap(a.value(), b.value());
                }
                else if (n->is_array() && n->size() >= 2)
                {
                    std::swap((*n)[rng() % n->size()], (*n)[rng() % n->size()]);
                }
                else *n = EdgeValue(rng, j);
                break;
            case 3:                                                                // rename a key
                if (n->is_object() && !n->empty())
                {
                    auto it = n->begin(); std::advance(it, rng() % n->size());
                    const std::string key = it.key();
                    json v = it.value();
                    n->erase(it);
                    (*n)[key + (rng() % 2 ? "_x" : "")] = v;
                    if (rng() % 2) (*n)[std::string("\xC3\xA9junk\x01")] = v;   // "éjunk" + a control byte (valid UTF-8)
                }
                else *n = EdgeValue(rng, j);
                break;
            default:                                                               // append an edge element
                if (n->is_array()) n->push_back(EdgeValue(rng, j));
                else if (n->is_object()) (*n)[std::string("k") + std::to_string(rng() % 1000)] = EdgeValue(rng, j);
                else *n = EdgeValue(rng, j);
                break;
            }
        }
        // `replace`: a byte-mutated string value that is no longer valid UTF-8 must not
        // make the FUZZER throw — the parser under test is what has to cope.
        return j.dump(-1, ' ', false, json::error_handler_t::replace);
    }

    // ---- the driver ---------------------------------------------------------

    struct Stats
    {
        int cases = 0, accepted = 0, rejected = 0;
        double maxMs = 0; int maxCase = -1;
        std::string worstInputPath;
    };

    // Runs `cases` mutations of `seed` over `valid` text. `parse` returns true when
    // the parser ACCEPTED the input (its own consistency is asserted by the caller
    // inside `parse`). `evidenceDir` (optional) receives `<label>-current.txt`, the
    // input written BEFORE each parse — after a crash it holds the culprit.
    inline Stats Run(const std::string& label, const std::string& valid, uint32_t seed, int cases,
                     double deadlineMs, const std::function<bool(const std::string&)>& parse,
                     const std::string& evidenceDir, bool structural)
    {
        Stats st;
        std::mt19937 rng(seed);
        std::filesystem::path current;
        if (!evidenceDir.empty()) current = std::filesystem::path(evidenceDir) / (label + "-current.txt");
        for (int i = 0; i < cases; ++i)
        {
            std::string input = (structural && (i % 2 == 0)) ? MutateStructure(valid, rng) : MutateBytes(valid, rng);
            if (!current.empty())
            {
                std::ofstream out(current, std::ios::binary | std::ios::trunc);
                out << "# " << label << " seed=" << seed << " case=" << i << "\n" << input;
            }
            const auto t0 = std::chrono::steady_clock::now();
            const bool ok = parse(input);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            ++st.cases;
            if (ok) ++st.accepted; else ++st.rejected;
            if (ms > st.maxMs) { st.maxMs = ms; st.maxCase = i; }
            if (ms > deadlineMs)
            {
                std::fprintf(stderr, "[wo09-fuzz] %s seed=%u case=%d exceeded the parser deadline: %.1f ms\n", label.c_str(), seed, i, ms);
                if (!evidenceDir.empty()) WriteFile(std::filesystem::path(evidenceDir) / (label + "-slow-" + std::to_string(i) + ".txt"), input);
                st.worstInputPath = label + "-slow-" + std::to_string(i) + ".txt";
            }
        }
        return st;
    }
}
