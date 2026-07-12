// ProjectAssets.cpp — see header. Schema-safe JSON reference retargeting (T6).

#include "ProjectAssets.h"

#include "utils/FileSystem.h"
#include "core/Log.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace Starforge::ProjectAssets
{
    namespace
    {
        bool IsRetargetable(const std::string& ext)
        {
            return ext == ".cscene" || ext == ".cprefab" || ext == ".cmat";
        }

        std::string LowerExt(const fs::path& p)
        {
            std::string e = p.extension().string();
            for (char& c : e) c = (char)std::tolower((unsigned char)c);
            return e;
        }

        // Recursively replace exact string VALUES == oldV with newV; returns the
        // number of replacements. (nlohmann default `json` sorts object keys —
        // the same ordering SceneSerializer's dump(2) produces — so re-dumping
        // reproduces the on-disk format, changing only the matched values.)
        int ReplaceInJson(json& j, const std::string& oldV, const std::string& newV)
        {
            int n = 0;
            if (j.is_string())
            {
                if (j.get_ref<const std::string&>() == oldV) { j = newV; ++n; }
            }
            else if (j.is_object() || j.is_array())
            {
                for (auto& el : j)
                    n += ReplaceInJson(el, oldV, newV);
            }
            return n;
        }

        // One walk over the project's retargetable files. When `apply`, matches
        // are rewritten and the file re-saved; otherwise it is a dry run. Returns
        // the project:// paths of files that matched (and, when applying, changed).
        std::vector<std::string> Sweep(const std::string& oldVfs, const std::string& newVfs, bool apply)
        {
            std::vector<std::string> touched;
            if (oldVfs.empty()) return touched;

            const std::string rootDisk = Cosmic::FileSystem::Resolve("project://");
            std::error_code ec;
            if (!fs::exists(rootDisk, ec)) return touched;

            for (auto it = fs::recursive_directory_iterator(rootDisk, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                if (ec) break;
                if (!it->is_regular_file(ec)) continue;
                if (!IsRetargetable(LowerExt(it->path()))) continue;

                std::ifstream in(it->path(), std::ios::binary);
                if (!in) continue;
                std::stringstream ss; ss << in.rdbuf();
                in.close();

                json j = json::parse(ss.str(), nullptr, /*allow_exceptions*/ false);
                if (j.is_discarded()) continue;   // not valid JSON — skip, never corrupt

                // A dry run replaces oldVfs with itself (a harmless no-op) just to
                // COUNT real string-value references; an apply run rewrites them.
                const int matches = ReplaceInJson(j, oldVfs, apply ? newVfs : oldVfs);
                if (matches <= 0) continue;

                std::error_code rec;
                fs::path rel = fs::relative(it->path(), rootDisk, rec);
                const std::string vfs = rec ? it->path().string()
                                            : "project://" + rel.generic_string();

                if (apply)
                {
                    std::ofstream out(it->path(), std::ios::binary | std::ios::trunc);
                    if (out) { out << j.dump(2); out.close(); touched.push_back(vfs); }
                    else CS_ERROR("ProjectAssets: could not write {}", vfs);
                }
                else
                {
                    touched.push_back(vfs);
                }
            }
            return touched;
        }
    }

    std::vector<std::string> FindReferences(const std::string& oldVfs)
    {
        return Sweep(oldVfs, oldVfs, /*apply*/ false);
    }

    std::vector<std::string> RetargetPath(const std::string& oldVfs, const std::string& newVfs)
    {
        if (oldVfs.empty() || newVfs.empty() || oldVfs == newVfs) return {};
        return Sweep(oldVfs, newVfs, /*apply*/ true);
    }
}
